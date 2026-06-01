# SEED-NOTES — Intercom

First architecture artifact (batch 2 of the producer backfill). See
`../../BATCH-2-FINDINGS.md` and `../../REVIEW-2.md` for the shared rationale; this
file records what is specific to this repo.

- **Producer id:** `intercom`  ·  **introduced:** `2025-02-15` (repo's first commit, full clone).
- **What this repo owns:** the FIRMWARE «SoftwareProduct» only. The physical device is registered in IoT Support and will be generated there as a `device:` instance — not modeled here.
- **Universal MDM edges (no boundBy):** firmware → `svc:iotsupport-api` (device API),
  → `cap:pub-sub-broker` (MQTT), → `cap:iam` (Keycloak M2M). Addresses are written
  into device NVS by IoT Support from its own config, so the firmware carries no
  `boundBy` recipe; IoT Support's planned device generator will emit the realized
  per-device `Serving` edges. Modeled as element kind **SystemSoftware** (bare-metal
  firmware) so a later `device: —Assignment→ ss:` resolves cleanly.
- **As-if-migrated:** not yet on esp-mdm; modeled as if migrated (operator direction).
- **Intercom relay edge (D4).** The firmware → `svc:intercom` (the relay service owned
  by the intercom-server producer), spoken over the fixed MQTT topic contract that is
  its `if:intercom-mqtt` interface. Mechanics: on boot the device fetches
  `http://iotsupport.home/esp32/config/<mac>.json` (carrying `mqtt.endpoint` +
  credentials — its only given address), connects to that broker and subscribes to
  `intercom/client/<id>/set/+`; IntercomServer, on the same broker, drives it by
  publishing peer `ip:port`s to `set/add_endpoint`/`set/remove_endpoint`, and the
  device streams audio over UDP to those advertised addresses. The topic namespace is
  hardcoded in both peers → no boundBy; the UDP targets are runtime values, not a
  separate surface. Two units today, more planned.
  (Note: the pre-MDM config JSON carrying `mqtt.endpoint` is the one real example of a
  provider address delivered in device config — but it's the universal broker edge,
  handled specially, not a `field:` case. See REVIEW-2.md D2/Realization.)
- **Home Assistant MQTT integration:** → `svc:home-assistant-mqtt` (self-producer, `home-automation.yaml`). Publishes HA MQTT discovery so the device's entities surface in Home Assistant; topic convention hardcoded → no boundBy.
- **Validation:** `./scripts/arch-validate.py docs/architecture/architecture.yaml` → OK.
