# Audio pipeline review

Review of the Intercom firmware's audio processing and streaming, requested 2026-07-12.
Scope: everything from the INMP441 microphone to the UDP wire and back to the speaker —
`I2SRecordingDevice`, `I2SPlaybackDevice`, `AudioMixer`, `AutoVolume`, `RingBuffer`,
`UDPServer`, `Device`/`Application` wiring, `sdkconfig.defaults`, and the WiFi setup in
`esp-network-support`.

Documents:

- **[pipeline-overview.md](pipeline-overview.md)** — how the pipeline actually works
  end-to-end (data flow, AEC reference timing, latency budget, task layout). Written as a
  reference, since audio isn't your home turf.
- **[findings.md](findings.md)** — all findings with severity, file/line references, and
  concrete fixes.

## Verdict

The architecture is genuinely good. The AEC reference-timing design (timestamped
reference samples synced against the recording clock) is the hard part of a device like
this and it is done correctly; the buffers and locking are sound; audio memory is
correctly pinned to internal RAM; and runtime-tunable audio config over MQTT is a feature
many commercial devices lack. Nothing here is structurally wrong.

The hiccups you hear are most likely **not** "just what a device like this does." Three
independent causes stack up, all fixable:

1. **WiFi modem sleep is on** (H1) — the ESP-IDF default. The radio naps between beacons
   and incoming audio arrives in bursts with gaps that can reach your entire 200 ms
   jitter budget. One line to fix: `esp_wifi_set_ps(WIFI_PS_NONE)`.
2. **Every lost or reordered packet permanently shrinks the jitter buffer** (H2). The
   mixer appends packets back-to-back, so a loss shifts the whole stream earlier instead
   of leaving a 20 ms gap. Margin erodes until the stream underruns, hiccups, and
   rebuffers — the classic "fine for a while, then a stumble" signature.
3. **An underrun costs ~300 ms, not 20 ms** (H3) — playback tears down and restarts with
   a full DMA silence preload plus the 200 ms jitter lead.

Secondary suspects: dynamic frequency scaling enabled under a real-time AEC workload
(H4) and the 6-packet lwIP UDP mailbox (H5).

## Recommended order of work

| # | Action | Effort | Finding |
|---|---|---|---|
| R0 | Add drop/underrun/stale counters to `get_state()` so effects are measurable | small | O7 |
| R1 | `esp_wifi_set_ps(WIFI_PS_NONE)` after WiFi start | one line | H1 |
| R2 | `CONFIG_LWIP_UDP_RECVMBOX_SIZE=32`; drop `CONFIG_PM_ENABLE` and `CONFIG_I2S_ENABLE_DEBUG_LOG` from `sdkconfig.defaults` | config only | H4, H5, L2 |
| R3 | Sequence-based packet placement in `AudioMixer` (loss → silence gap, reorder tolerated, margin stable) | the one real rework | H2, Q6, L1 |
| R4 | Grace period on underrun instead of stop/restart | small | H3 |
| R5 | Playback polish: per-sample gain ramp, no reset on volume change, volume applied when AGC is off | small each | Q3–Q5 |
| R6 | Record path: lower `microphone_gain_bits`, fix auto-volume release time constant | small | Q1, Q2 |
| R7 | Latency reclaim once stable: smaller DMA preload, then try `audio_buffer_ms` ≈ 100 | small | L6 |

R0–R2 are an evening's work including flashing, and I'd expect them alone to noticeably
reduce the hiccups. R3 is the one change that needs real design care; findings H2/L1
sketch the approach including the edge cases.

Quick hygiene fixes independent of the above: the AFE-input dump doesn't compile (L3),
`start()` returns the wrong value (L4), AFE config asserts should be assignments (L5),
README pin table is stale (L8).
