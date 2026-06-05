#!/usr/bin/env python3
"""Publish stale Home Assistant discovery configs to exercise the firmware's
discovery-prune path and, with a burst of them, its MQTT QoS in-flight
back-pressure.

Background
----------
After each connect the firmware subscribes to

    homeassistant/+/<device_id>/+/config

for ~60 seconds. Any *retained, non-empty* config on that pattern that the
device did not itself publish is considered stale: the device deletes it by
publishing an empty retained message to the same topic. Each delete is a QoS-1
publish that flows through the in-flight back-pressure, so a burst of ~100 of
these reliably drives the budget to its ceiling and surfaces the slot-wait
(pause/resume) lines in the ACK_DBG log - which you otherwise almost never see,
because normal traffic never fills the budget.

This writes N retained configs. The device prunes them the next time it connects
(within its 60s discovery window); re-run to test again, or use --clear to
remove them yourself if the device is offline.

Usage
-----
    python publish_stale_discovery.py 0x983daef2c858
    python publish_stale_discovery.py 0x983daef2c858 --count 100
    python publish_stale_discovery.py 0x983daef2c858 --clear

Requires: pip install paho-mqtt

Broker connection settings default to these environment variables (override any
of them on the command line):

    MQTT_BROKER    broker host (default mosquitto)
    MQTT_PORT      broker port (default 1883)
    MQTT_USER      username (default mqtt)
    MQTT_PASSWORD  password (no default; required unless --password is given)
"""
import argparse
import json
import os
import sys

import paho.mqtt.client as mqtt

DEFAULT_BROKER = os.environ.get("MQTT_BROKER", "mosquitto")
DEFAULT_PORT = int(os.environ.get("MQTT_PORT", "1883"))
DEFAULT_USER = os.environ.get("MQTT_USER", "mqtt")
DEFAULT_PASSWORD = os.environ.get("MQTT_PASSWORD")


def make_client():
    # paho-mqtt 2.x requires an explicit callback API version; fall back for 1.x.
    try:
        from paho.mqtt.enums import CallbackAPIVersion

        return mqtt.Client(CallbackAPIVersion.VERSION2, protocol=mqtt.MQTTv5)
    except ImportError:
        return mqtt.Client(protocol=mqtt.MQTTv5)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("device_id", help="device id, e.g. 0x983daef2c858")
    parser.add_argument("--count", type=int, default=100, help="number of stale configs (default 100)")
    parser.add_argument("--component", default="sensor", help="HA component segment, the first '+' (default sensor)")
    parser.add_argument("--clear", action="store_true", help="delete the stale configs instead of creating them")
    parser.add_argument("--broker", default=DEFAULT_BROKER)
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--user", default=DEFAULT_USER)
    parser.add_argument("--password", default=DEFAULT_PASSWORD)
    args = parser.parse_args()

    if not args.password:
        sys.exit("No broker password: set MQTT_PASSWORD or pass --password.")

    client = make_client()
    client.username_pw_set(args.user, args.password)

    try:
        client.connect(args.broker, args.port)
    except OSError as e:
        sys.exit(f"Could not connect to {args.broker}:{args.port}: {e}")

    client.loop_start()

    for i in range(args.count):
        object_id = f"stale_{i:03d}"
        topic = f"homeassistant/{args.component}/{args.device_id}/{object_id}/config"
        # An empty retained payload clears the retained message; a non-empty one
        # is what the device sees as a stale config to prune.
        if args.clear:
            payload = ""
        else:
            payload = json.dumps(
                {
                    "name": object_id,
                    "unique_id": f"{args.device_id}_{object_id}",
                    "state_topic": f"homeassistant/{args.component}/{args.device_id}/{object_id}/state",
                }
            )
        info = client.publish(topic, payload, qos=1, retain=True)
        info.wait_for_publish()
        if (i + 1) % 25 == 0:
            print(f"  {'cleared' if args.clear else 'published'} {i + 1}/{args.count}")

    client.loop_stop()
    client.disconnect()

    verb = "Cleared" if args.clear else "Published"
    print(
        f"{verb} {args.count} retained configs under "
        f"homeassistant/{args.component}/{args.device_id}/stale_*/config."
    )
    if not args.clear:
        print(
            "The device prunes them on its next connect (within its 60s discovery "
            "window). Watch the console for ACK_DBG pause/resume (slot-wait) lines."
        )


if __name__ == "__main__":
    main()
