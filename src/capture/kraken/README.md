# Kraken / 2x RTL-SDR Guide for blah2

This guide describes how the `Kraken` capture path works in blah2 when it is used with two RTL-SDR receive channels.

In the current implementation, blah2 uses two live channels only:

- Channel 0 is treated as the reference channel.
- Channel 1 is treated as the surveillance channel.

The `Kraken` device type covers both KrakenSDR-style shared-clock hardware and a plain 2x RTL-SDR setup that has been wired to share a common clock.

## Hardware Expectations

For a 2x RTL-SDR setup, the two SDR devices are expected to share a common clock.

This is important because standard RTL-SDR devices do not provide a reliable hardware trigger that starts both receivers on the exact same sample. Even if both tuners are configured with the same center frequency and sample rate, the streams will usually begin with an unknown sample offset.

blah2 works around that limitation by aligning the streams from the received IQ itself. That means both the reference and surveillance channels must be able to observe the same correlated signal during alignment. In passive radar terms, the reference channel should see a strong direct-path transmitter signal, and the surveillance channel must also see enough of that same signal for cross-correlation to lock onto it.

If the two devices do not share a common clock, or if the surveillance channel cannot see enough of the same signal as the reference channel, startup alignment may fail, become unstable, or settle to the wrong offset.

## Software Expectations

blah2 will attempt to call `rtlsdr_set_dithering()` when librtlsdr exposes it. This is recommended for shared-clock RTL-SDR operation because disabling PLL dithering can improve phase coherence.

If your librtlsdr build does not expose `rtlsdr_set_dithering()`, blah2 will continue to run and will log a warning once. Alignment can still work, but startup alignment and drift rechecks may be less stable.

## How Startup IQ Alignment Works

Because normal RTL-SDR devices cannot be sample-triggered together, blah2 performs a signal-based startup alignment step before it releases IQ downstream.

The process is:

1. blah2 opens both RTL-SDR devices and starts asynchronous reads on both channels.
2. Each callback appends raw IQ into per-channel pending and history ring buffers.
3. If alignment is enabled, blah2 waits until both channels have enough recent history for a lag estimate.
4. The history requirement is `windowCount * alignmentWindowSamples` samples per channel.
5. `alignmentWindowSamples` is not a YAML field. It is derived automatically from `capture.fs` and clamped between 65536 and 262144 samples.
6. blah2 copies the most recent alignment windows from both channels.
7. For each window, blah2 estimates lag by FFT-based cross-correlation between reference and surveillance IQ.
8. If the lag estimates agree within `consensusToleranceSamples`, the median lag is accepted as the raw startup offset.
9. blah2 drops samples on the earlier channel so the two streams line up.
10. Once the correction has been applied, aligned IQ starts flowing into the rest of the pipeline.

If the windows do not agree closely enough, blah2 keeps collecting more IQ and tries again. This is why startup alignment depends on both channels seeing the same signal clearly enough for correlation.

## Drift Rechecks

After startup alignment, blah2 continues to monitor the raw lag between the two channels.

At every `recheckIntervalMinutes`, it repeats the same lag measurement on recent IQ history. If the measured lag differs from the correction already applied, blah2 schedules another sample drop on the earlier channel.

Important limits:

- This is a best-effort software correction, not a hardware resync.
- Corrections are implemented by dropping samples on the earlier channel.
- Failed rechecks are retried later, but they do not guarantee a stable long-term lock.

## Continuous Drift and Detection Risk

Even with a common clock and periodic realignment, there will still be continuous drift between the two receive paths.

That drift matters because passive radar detections depend on a stable relationship between the reference and surveillance signals. If that relationship moves, the ambiguity surface can smear, peaks can weaken or shift, and detections can appear, disappear, or move in ways that are hard to predict.

In practice:

- periodic realignment reduces the error, but does not eliminate it,
- drift still accumulates between rechecks,
- the effect on detections depends on transmitter strength, direct-path visibility, SNR, clock quality, USB behavior, and the quality of the shared-clock wiring.

This means the RTL-SDR alignment path should be treated as a workaround for non-triggerable hardware, not as a substitute for a truly synchronous multi-channel receiver.

## YAML Configuration

Configure the Kraken capture path under `capture.device` in the YAML file passed to `blah2 -c`.

Example:

```yaml
capture:
  fs: 2000000
  fc: 204640000
  device:
    type: "Kraken"
    gain: [15.0, 15.0]
    alignment:
      enabled: true
      windowCount: 3
      consensusToleranceSamples: 16
      recheckIntervalMinutes: 10
```

Relevant fields:

- `capture.device.type`: Must be `Kraken`.
- `capture.fs`: Sample rate in Hz. This also affects the automatically derived alignment window size, so it changes how much history blah2 needs before the first lag estimate.
- `capture.fc`: Center frequency in Hz. Both channels must be tuned to the same signal environment.
- `capture.device.gain`: Per-channel tuner gain in dB. blah2 snaps each requested value to the next supported RTL-SDR tuner step.
- `capture.device.alignment.enabled`: Enables startup sample-time alignment and periodic drift correction. If set to `false`, blah2 forwards the two channels without startup alignment or drift correction.
- `capture.device.alignment.windowCount`: Number of consecutive windows used for each lag measurement. Larger values demand more agreement and more startup history.
- `capture.device.alignment.consensusToleranceSamples`: Maximum allowed spread between the per-window lag estimates. Smaller values are stricter. Larger values accept noisier agreement.
- `capture.device.alignment.recheckIntervalMinutes`: Interval between drift rechecks after startup alignment succeeds.

Related notes:

- `capture.device.array` and `capture.device.boresight` are downstream geometry settings. They do not control the startup IQ alignment logic.
- `capture.replay` bypasses live RTL-SDR capture and uses a recorded two-channel IQ file instead.
- When Kraken capture is running, blah2 writes a local runtime metric file named `kraken-alignment-runtime.json` in `save.path`.

## Runtime Metric File

The runtime metric file is intended for local monitoring and troubleshooting. It is not part of the API contract.

The file includes:

- `alignmentReady`: Whether startup alignment has completed.
- `rawLagValid`: Whether a lag measurement has been established.
- `currentRawLagSamples`: Most recent measured raw lag.
- `appliedCorrectionSamples`: Net correction already applied.
- `driftSamples`: Difference between the latest raw lag and the startup lag baseline.

Positive lag values mean channel 1 is lagging channel 0.

## Troubleshooting

If startup alignment does not complete, check the following first:

- The two RTL-SDR devices really are sharing a common clock.
- Both channels are tuned to the same transmitter environment.
- The surveillance channel can see enough of the same signal as the reference channel for cross-correlation.
- Gain is not so low that the common signal disappears into noise.
- Gain is not so high that the shared signal is clipping badly.

Typical symptoms:

- Repeated startup messages saying the lag estimate is not stable usually mean the shared signal is too weak, too distorted, or not common enough across both channels.
- Large or frequent drift corrections usually indicate unstable timing, weak correlation, or poor shared-clock behavior.
- If you need to bypass this logic during testing, set `capture.device.alignment.enabled: false`.

## Practical Recommendation

For the best chance of usable detections with two RTL-SDR devices:

- share the clock between both dongles,
- make sure both channels can see the same direct-path signal during alignment,
- keep the cabling and RF setup stable,
- treat drift correction as a mitigation step rather than a guarantee.