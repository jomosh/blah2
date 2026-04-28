---
name: blah2-replay-validation
description: 'Validate blah2 detector, tracker, serialization, or performance-sensitive changes using Catch2, replay IQ files, TestDetectionSweep, and ADS-B sidecars.'
argument-hint: 'Describe the change, the available tests or replay data, and the signal you need to confirm or reject.'
---

# blah2 Replay And Validation Workflow

## When To Use
- Any change in detection, tracking, ambiguity, clutter, serialization, or cross-layer behavior that needs evidence beyond a code read.
- Running targeted unit tests or comparison sweeps before approving a tuning change.

## Procedure
1. Pick the narrowest relevant Catch2 target first. Use the mirrored layout under `test/unit/`.
2. For detector or tracker tuning, use `test/comparison/TestDetectionSweep.cpp` and the workflow documented in `test/README.md`.
3. If replay data exists, include the `.iq` file and the optional `.adsb` sidecar so the sweep can score precision-like and false-positive metrics.
4. Read the summary in terms of `HitRate`, `MatchPts`, `FalsePts`, `MissedPts`, `MatchPctPt`, `FPRatePt`, and runtime cost.
5. Record missing fixtures, hardware needs, or environment prerequisites explicitly when you cannot run the intended validation.

## Repo Anchors
- `test/README.md`
- `test/comparison/TestDetectionSweep.cpp`
- `test/unit/`
- `config/config.yml`

## Done Checklist
- The chosen validation directly matches the change scope.
- Floating-point assertions use tolerance where needed.
- Any replay or ADS-B assumptions are documented in the result.