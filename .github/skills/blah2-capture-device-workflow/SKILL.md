---
name: blah2-capture-device-workflow
description: 'Add or modify SDR capture devices, replay or save logic, or hardware-specific config in blah2. Use for HackRF, USRP, RspDuo, KrakenSDR, Source, or Capture changes.'
argument-hint: 'Describe the device or capture-path change, the config knobs involved, and how you plan to validate it.'
---

# blah2 Capture Device Workflow

## When To Use
- Adding a new SDR or changing an existing device integration.
- Modifying replay, save, or canonical two-channel IQ handling.
- Touching `Capture`, `Source`, or device-specific validation logic.

## Procedure
1. Start with `src/capture/Source.h`, `src/capture/Capture.cpp`, and the closest device implementation under `src/capture/<device>/`.
2. Keep device-specific code in its own folder and preserve the current config-driven factory pattern.
3. Validate hardware-specific settings early. Reject unsupported gain, serial, or rate combinations before capture starts.
4. Preserve the canonical two-channel IQ save or replay format and the thread-safe `IqData` handoff semantics.
5. Keep callbacks allocation-aware and logging-light. Avoid adding work that can stall device reads.
6. If you add runtime knobs or a new device type, update `config/*.yml`, `CMakeLists.txt`, and the relevant deployment docs.
7. Validate with the narrowest unit or runtime check you can run, and note any hardware prerequisites explicitly.

## Repo Anchors
- `src/capture/Source.h`
- `src/capture/hackrf/HackRf.cpp`
- `src/capture/rspduo/README.md`
- `config/`
- `doc/example-deployments/HackRF-RPI5/README.md`

## Done Checklist
- Device settings are still config-driven.
- Replay and save behavior still matches the canonical file format.
- Validation covers both startup failures and the steady-state capture path.