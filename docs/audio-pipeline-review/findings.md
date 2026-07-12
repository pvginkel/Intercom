# Findings

Severity legend:
- **H (high)** — plausibly causes the hiccups you hear, or breaks a feature.
- **Q (quality)** — audible audio-quality impact, not a dropout.
- **L (low)** — hygiene, latent bugs, small wins.

File references are to the state of the tree at the time of review (commit `4b77e9c`).
Paths starting with `esp-libs/` refer to the shared components checked out next to this
repository (see `main/idf_component.yml`).

---

## H1 — WiFi modem power save is never disabled

**Where:** `esp-libs/esp-network-support/src/NetworkConnection.cpp` (no
`esp_wifi_set_ps()` call anywhere in the app or shared libs).

ESP-IDF defaults station mode to `WIFI_PS_MIN_MODEM`: the radio sleeps between DTIM
beacons. Downlink packets are buffered at the access point until the next beacon, so
incoming UDP audio arrives in bursts with gaps that regularly reach 100–300 ms depending
on the AP's DTIM interval. That is at or beyond your 200 ms jitter lead, and it also
causes uplink latency spikes and extra loss under buffer pressure. This is the single
most common cause of "mostly fine, occasionally hiccups" audio streaming on ESP32.

**Fix:** after `esp_wifi_start()` (or in `Application::do_network_available()` if you
want to keep the change out of the shared lib):

```cpp
ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
```

Costs ~50–80 mA extra average current — irrelevant for a mains-powered intercom.

*Hardware v1 caveat:* v1 clamps TX power to 11 dBm because the board picks up its own RF.
`WIFI_PS_NONE` keeps the receiver on continuously but does not increase TX activity, so it
should be safe, but verify on a v1 unit.

## H2 — Packet loss permanently erodes the jitter buffer (append-only mixer)

**Where:** `main/AudioMixer.cpp:29-95` (`append`).

The packet counter is only used to drop stale packets (`packet_index <
write_offset.packet_index`). Otherwise every packet is appended back-to-back at the
source's write offset. Consequences:

- A **lost** packet does not leave a gap — all subsequent audio shifts earlier by one
  packet duration. The write offset advances less than real time, so the margin between
  write head and read position shrinks by ~15–30 ms per lost packet **and never
  recovers**.
- A **reordered** packet (arrives after a newer one) is dropped as stale — same erosion,
  even though the data actually arrived.
- After enough accumulated loss (roughly 6–12 lost packets with defaults) the stream
  underruns → playback stops → 200 ms+ rebuffer (see H3). To a listener: a periodic
  hiccup that gets more frequent the worse the WiFi is, then resets.

**Fix:** place packets by sequence number instead of appending. Per source, record the
first packet's `(index₀, offset₀)` and write packet *n* at
`offset₀ + (n − index₀) × payload_len` (payload length is constant per stream). Lost
packets then play as ~20 ms of silence (the ring is pre-zeroed — you get gap concealment
for free), late/reordered packets slot into their correct position if not yet consumed,
and the jitter margin stays constant for the life of the stream.

Guard rails needed: drop packets whose slot has already been read; drop/clamp packets
beyond the ring capacity; treat a large index jump (or an index *decrease* below the
stale window) as a new stream and re-anchor. This also subsumes finding L5.

## H3 — An underrun stops playback entirely and magnifies a late packet into a ~300 ms gap

**Where:** `main/I2SPlaybackDevice.cpp:151-184` (`write_task` loop),
`main/Device.cpp:35` (`on_buffer_exhausted` → `stop()`), `main/AudioMixer.cpp:134`.

When the mixer runs dry — even by one packet, once H2 has eaten the margin — the write
task exits, the I2S channel is disabled, and MQTT state churns. The next packet restarts
the whole path: 10 ms settle + ~90 ms DMA silence preload + 200 ms jitter lead before
sound resumes. A single late packet therefore costs ~300 ms of silence plus a click-like
discontinuity, instead of the ≤20 ms it needed to.

**Fix:** decouple "buffer momentarily empty" from "stream ended":

- Keep the I2S channel running and feed zeros while dry, for a grace period (e.g.
  2 × `audio_buffer_ms`). Resume normally when data reappears; only stop and tear down
  after the grace period expires.
- Optionally resume as soon as the write offset is a *threshold* ahead (e.g. 50 ms)
  instead of a full `audio_buffer_ms` lead, so recovery is fast.

With H1 + H2 fixed, underruns should become rare; this makes the remaining ones cheap.

## H4 — Dynamic frequency scaling is enabled on a real-time audio device

**Where:** `sdkconfig.defaults:34-36` (`CONFIG_PM_ENABLE=y`, `CONFIG_PM_DFS_INIT_AUTO=y`).

With PM enabled the CPUs scale between max and min frequency based on load. The I2S
sample clocks are PLL-derived and unaffected, and the I2S driver holds an APB lock while
a channel is enabled — but the **CPU** frequency can still drop to 80 MHz between
audio bursts, and each DFS transition briefly stalls both cores and shifts interrupt
latency. AEC (`VOIP_HIGH_PERF`) + NS is a heavy load on core 1; a downclocked window at
the wrong moment shows up as exactly this kind of intermittent glitch.

**Fix (pick one):**
1. Simplest: remove `CONFIG_PM_ENABLE`/`CONFIG_PM_DFS_INIT_AUTO` from
   `sdkconfig.defaults`. A mains-powered intercom gains nothing from DFS.
2. Keep PM but acquire `ESP_PM_CPU_FREQ_MAX` (and release) around active
   recording/playback.

## H5 — Default lwIP UDP mailbox is 6 packets; post-sleep bursts overflow it

**Where:** not set in `sdkconfig.defaults`; lwIP default `CONFIG_LWIP_UDP_RECVMBOX_SIZE=6`.

Each UDP socket queues at most 6 received datagrams. Audio packets arrive every
~15–30 ms; any stall of the `udp_server` task (core 0 is shared with WiFi at prio 23,
`write_task` and `forward_task` at prio 5) or an AP-side burst after a power-save wake
(H1) delivers more than 6 packets at once and lwIP silently drops the excess.

**Fix:** add to `sdkconfig.defaults`:

```
CONFIG_LWIP_UDP_RECVMBOX_SIZE=32
```

Cheap (a few hundred bytes) and removes a hard cliff. Raising the `udp_server` task
priority above the other prio-5 audio tasks (e.g. to 10) is a reasonable companion
change: its only job is to drain the socket into the mixer.

---

## Q1 — Mic path hard-clips the top ~18 dB before the AFE ever sees it

**Where:** `main/I2SRecordingDevice.cpp:356-358`.

```cpp
const auto raw_sample = (source[i] << 1) >> (16 - _microphone_gain_bits);
const auto sample = (int16_t)clamp<int32_t>(scaled_sample, INT16_MIN, INT16_MAX);
```

With the default `microphone_gain_bits = 3` and `recording_auto_volume_enabled = false`,
the 24-bit sample is reduced to a 19-bit range and clamped to 16 bits: everything above
−18 dBFS of the mic's full scale hard-clips. For the INMP441 (120 dBSPL full scale)
clipping starts around **102 dBSPL at the mic** — reachable by someone talking loudly
close to the unit. Hard clipping *before* AEC/NS/AGC is doubly bad: the AGC can't undo
it, and clipped echo makes the AEC's job harder.

**Fix options** (in order of preference):
1. Lower `microphone_gain_bits` to 0–1 (headroom instead of gain) and let the AFE's
   WebRTC AGC (already enabled, 9 dB compression gain) bring speech up. Digital gain
   before the AFE adds no information — the AGC can do the same lifting after NS.
2. If the level without gain is too low in practice, enable the recording auto-volume as
   a limiter — but only after fixing Q2.

## Q2 — Recording auto-volume releases in ~0.6 ms → audible distortion when enabled

**Where:** `main/I2SRecordingDevice.cpp:190-215` (`scale_sample`),
default `recording_smoothing_factor = 0.1`.

The smoothed peak decays per *sample*: `peak = peak·0.9 + |x|·0.1` at 16 kHz is a time
constant of ~10 samples (0.6 ms). Gain therefore modulates at audio rate whenever the
signal is above full scale, which is amplitude-modulation distortion, not gentle
compression. (Attack-to-max instantly is fine — that part is correct limiter behavior.)

**Fix:** make the release time-based and slow. For a release time constant τ (e.g.
150 ms): `factor = 1 − expf(−1.0f / (16000.0f × 0.150f))` ≈ 4.2e-4. Either change the
default and document the unit, or better, store the config as a release time in ms and
derive the factor.

## Q3 — Volume control does nothing when playback auto-volume is disabled

**Where:** `main/I2SPlaybackDevice.cpp:170-172` and `46-54`.

`set_volume()` only feeds `AutoVolume::set_offset_db()`, and `process_block()` is the
only place that applies it. With `playback_auto_volume_enabled = false` the block is
skipped entirely, so samples play at full scale regardless of the volume setting. The
default config enables auto-volume, which is why this hasn't bitten yet — but it's a trap
for exactly the kind of config experimentation the MQTT interface invites.

**Fix:** apply the static offset gain unconditionally (simple per-sample multiply when
auto-volume is off), or always call `process_block` with the AGC stage bypassed.

## Q4 — AutoVolume gain steps once per 20 ms block → zipper artifacts on transients

**Where:** `main/AutoVolume.cpp:96-119`.

`output_gain_linear` is computed once per block and applied uniformly to all 320
samples. With a 10 ms attack and 20 ms blocks, consecutive blocks can differ by several
dB, and the step lands as a small click ("zipper noise"), most audible on speech onsets.
The 2 ms look-ahead delay line can't help across a block boundary, so as implemented it
adds 2 ms of latency for little benefit. (Also: the header comments say 1 ms look-ahead
and −0.1 dBFS "post limiter"; the constant is 2 ms and the limiter is a hard clip —
worth reconciling while in there.)

**Fix:** interpolate the linear gain per sample from the previous block's value to the
new one across the block (one multiply-add per sample). That removes the stepping, at
which point the delay line can be dropped entirely (removing 2 ms of latency) — per-block
detection with intra-block ramping is a standard, well-behaved AGC shape.

## Q5 — Every volume change resets the AGC and clears the delay line → click + loudness jump

**Where:** `main/AutoVolume.h:44-52` (`set_target_db`/`set_offset_db` both call `reset()`),
`main/I2SPlaybackDevice.cpp:46-54`.

Turning the volume knob mid-stream zeroes the envelope, gain, and look-ahead buffer:
2 ms of silence is spliced in (click) and the AGC re-converges from unity gain (brief
loudness pump). Neither state depends on the offset, so there is nothing to reset.

**Fix:** just assign the new offset; reserve `reset()` for stream start.

## Q6 — Mixer keeps the tail of a partially-fitting packet instead of the head

**Where:** `main/AudioMixer.cpp:70-80`.

When the ring has room for only part of a packet, `buffer_offset = buffer_len − copy`
keeps the **last** `copy` bytes and mixes them at the position where the **first** bytes
belong — the kept audio plays early by the discarded amount, adding a discontinuity on
top of the unavoidable truncation.

**Fix:** keep the head (`buffer_offset = 0`) so the retained samples land at their
correct time. (With H2's sequence-based placement this path changes anyway.)

Related nit in the same function: `available` is `size_t`, so `if (available <= 0)` only
catches exactly 0. It can't currently go negative because appends are capped and both
sides run under the playback lock, but the comparison documents an intent the type can't
express — make it signed or compare `== 0`.

---

## L1 — Restarting a recording session can mute its first ~200 ms at the receiver

**Where:** `main/Device.cpp:120-129` (`_next_packet_index = 0` on start),
`main/AudioMixer.cpp:50-55`.

The receiver's stale-packet check is keyed only by source IP:port. If a new recording
session (counter reset to 0) starts while the receiver is still draining the previous
session's entry, all packets are dropped as stale (`0 < old index`) until the entry
underruns and is erased. Add a session/stream id to the header (random 32-bit chosen at
`start()`), or treat a large backwards index jump as a new stream. Folds naturally into
the H2 rework.

## L2 — `CONFIG_I2S_ENABLE_DEBUG_LOG=y` is in production defaults

**Where:** `sdkconfig.defaults:22`.

This turns on verbose debug logging inside the I2S driver. Log output goes through the
console (and your MQTT-routed logger) and adds work on timing-sensitive paths. Remove it;
re-enable locally when debugging the driver.

## L3 — The AFE-input dump feature doesn't compile

**Where:** `main/I2SRecordingDevice.cpp:377-379`.

```cpp
_udp_server.send((sockaddr*)&_dump_target, sizeof(_dump_target), feed_buffer, feed_buffer_len);
```

`feed_buffer`/`feed_buffer_len` don't exist (renamed to `_work_buffer`/`_work_buffer_len`
at some point). Anyone enabling `CONFIG_DEVICE_DUMP_AFE_INPUT` gets a build error. Fix
the names — this dump is exactly the tool you'll want when tuning the AEC (finding O1).

## L4 — `I2SRecordingDevice::start()` always returns true

**Where:** `main/I2SRecordingDevice.cpp:248`.

`return true;` should be `return result;` (as `stop()` does). Callers currently ignore
the value, so it's latent.

## L5 — AFE config asserts on library defaults instead of setting them

**Where:** `main/I2SRecordingDevice.cpp:98-156`.

`assert(afe_config->aec_filter_length == 4)` etc. document expectations but do nothing
in release builds (`NDEBUG` compiles them out) and turn an esp-sr upgrade that changes a
default into either a debug-only abort or a silent behavior change. Assign the values you
require explicitly; delete the asserts.

## L6 — ~90 ms of fixed latency from the full DMA silence preload

**Where:** `main/I2SPlaybackDevice.cpp:127-140`.

The preload loop fills all DMA descriptors (defaults: 6 × 240 frames ≈ 90 ms) with
zeros on every start. That silence plays before the first real sample every time and
inflates the AEC reference anchor accordingly (correctly accounted for, but it's dead
air). Preloading 2 descriptors is enough to prevent an immediate underrun; alternatively
shrink the DMA ring (`dma_desc_num`/`dma_frame_num` in the channel config) — 3 × 320 ≈
60 ms total ring is plenty when the writer feeds 20 ms chunks. Combined with a smaller
`audio_buffer_ms` (defensible once H1/H2 are fixed — e.g. 100 ms), mouth-to-ear latency
drops from ~330 ms to ~180 ms.

## L7 — Float→int16 conversion truncates instead of rounding

**Where:** `main/AutoVolume.cpp:118`.

`(int16_t)(wet * INT16_MAX)` truncates toward zero — a tiny crossover nonlinearity around
silence. Use `lrintf()` (and note `INT16_MIN` has 1 LSB more range than `-INT16_MAX`;
scaling by `INT16_MAX` is the safe convention, so only the rounding needs changing).
Same nit applies to the divide-by-`INT16_MAX` normalization being asymmetric — harmless.

## L8 — README pin table is stale

**Where:** `README.md` vs `main/board_config.h`.

The README lists PB=33, mic on 35/36/37, speaker on 13/14/15; `board_config.h` (the
authority, per hardware version) says PB=13, mic on 14/15/16, speaker on 1/2/3. Point the
README at `board_config.h` rather than duplicating the numbers.

---

## O — Observations, no change required

- **O1 — AEC tuning headroom.** `aec_filter_length` is left at 4 (a commented-out `= 8`
  suggests you experimented). If you ever hear residual echo in a reverberant room, 8 is
  the first knob to try — longer filter models a longer echo tail at more CPU cost. The
  fixed L3 dump feature is the right way to evaluate it: record the stereo (mic +
  reference) AFE input and listen to/inspect alignment.
- **O2 — MSB slot format on the speaker.** TX uses left-justified framing
  (`I2S_STD_MSB_SLOT_DEFAULT_CONFIG`) rather than Philips. MAX98357-class amps accept
  both, and it evidently works; just be aware if the amp is ever swapped, Philips
  (`I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG`) is the more universal default.
- **O3 — Mic read config is correct for the INMP441.** 32-bit slot, left slot only,
  no bit shift + `<< 1` in software compensates the Philips 1-bit delay properly; sign
  is preserved. The comment block explains it well.
- **O4 — PSRAM tradeoff is reasonable.** Instructions/rodata and (best-effort) WiFi/lwIP
  buffers in PSRAM keep internal RAM free for audio buffers, which are correctly pinned
  `MALLOC_CAP_INTERNAL`. PSRAM cache-miss jitter is real but second-order compared with
  H1–H5; not worth changing unless glitches persist after those fixes.
- **O5 — Threading/locking checked out.** `RingBuffer` (overwrite-oldest semantics),
  the mixer, and the reference-feed handoff are consistently guarded by the right
  mutexes; the single-producer/single-consumer assumptions hold. `Mutex` wraps a FreeRTOS
  mutex (priority inheritance included). No data races found.
- **O6 — WiFi TX power clamp on v1** (11 dBm) reduces link margin → more retries →
  more jitter. If hiccups are noticeably worse on v1 hardware than v2, that's a likely
  contributor and worth confirming with the O7 counters.
- **O7 — You currently can't see any of this happening.** Nothing counts lost packets,
  stale drops, mixer overflows/underruns, or send failures. Before and after applying
  fixes, numbers beat ears — see recommendation R0 in the README.
