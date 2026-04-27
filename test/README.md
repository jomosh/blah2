# blah2 Test

A set of tests are provided for development/debugging.

## Framework

The test framework is [catch2](https://github.com/catchorg/Catch2).

## Types

The test files are split across directories defined by the type of test.

- **Unit tests** will test the class in isolation. The directory structure mirrors *src*.
- **Functional tests** will test that expected outputs are achieved from defined inputs. An example would be checking the program turns a specific IQ data set to a specific delay-Doppler map. This test category will rely on golden data.
- **Comparison tests** will compare different methods of performing the same task. An example would be comparing 2 methods of clutter filtering. Metrics to be compared may include time and performance. Note there is no specific pass/fail criteria for comparison tests - this is purely for information. A comparison test will pass if executed successfully. Any comparison testing on input parameters for a single class will be handled in the unit test.

## Usage

All tests are compiled when building, however tests be run manually.

- Run a single unit test for "TestClass".

```
sudo docker exec -it blah2 /blah2/bin/test/unit/testClass
```

- Run a single functional test for "TestFunctional".

```
sudo docker exec -it blah2 /blah2/bin/test/functional/testFunctional
```

- Run a single comparison test for "TestComparison".

```
sudo docker exec -it blah2 /blah2/bin/test/comparison/testComparison
```

- Capture a Blah2 IQ file during a normal live run, then replay it in the detector sweep comparison.

1. Configure blah2 for a normal live run, not replay mode, and point saves at a writable directory.

```yml
capture:
  replay:
    state: false

truth:
  adsb:
    enabled: true

save:
  path: "/blah2/save/"
```

2. Start `blah2` and the API server normally with that config.

3. Capture the scene you want while the live system is running. Start recording, wait until enough data has been collected, then stop recording.

Use either the IQ Capture button on one of the three detection map pages, or the API toggle endpoint.

```
curl http://127.0.0.1:3000/capture/toggle
curl http://127.0.0.1:3000/capture/toggle
```

blah2 writes a canonical two-channel interleaved IQ file under `save.path` with a timestamped name such as `/blah2/save/20260425-153000.rspduo.iq` or `/blah2/save/20260425-153000.hackrf.iq`.

If `truth.adsb.enabled` is true, blah2 writes a matching ADS-B sidecar for each IQ capture file, for example `/blah2/save/20260425-153000.rspduo.adsb` or `/blah2/save/20260425-153000.hackrf.adsb`, but only while the Start IQ Capture toggle is active. The sidecar stores the explicit capture start time once plus the CPI timestamped ADS-B delay-Doppler targets seen during that capture window.

4. Replay that captured file in the detector sweep comparison.

Do not change `capture.replay.state` to `true` for this step. That setting is used by the main `blah2` runtime when you want the full application to replay data instead of reading live hardware. `testDetectionSweep` only needs the replay file path, either from `--replay-file` or from `capture.replay.file` in the config.

If you want to score replay detections against ADS-B truth, pass the saved `.adsb` sidecar as well. `testDetectionSweep` prefers the explicit capture start time stored inside new sidecars, and only falls back to `--capture-start-ms` or IQ file-name inference for older sidecars.

```
sudo docker exec -it blah2 /blah2/bin/test/comparison/testDetectionSweep \
	--config /blah2/config/config.yml \
	--replay-file /blah2/save/20260425-153000.rspduo.iq \
	--adsb-file /blah2/save/20260425-153000.rspduo.adsb \
	--pfa 1e-5,1e-4,1e-3 \
	--min-doppler 5,10,15 \
	--min-delay 0,5,10 \
	--cfar-modes CAGO \
	--post-process-modes local-peak,cluster-centroid \
	--adsb-delay-window-km 1.0 \
	--adsb-doppler-window-hz 10
```

Use `--post-process-modes` when you want one run to compare the legacy local-peak suppression against the newer cluster-based weighted centroid stage on the same replay data.

With `--adsb-file`, the sweep summary adds truth-aware columns for matched Blah2 detection points, false-positive detection points, missed ADS-B truth points, and the corresponding per-detection-point rates for each sweep case.

### Interpreting the sweep summary

When `--adsb-file` is supplied, `testDetectionSweep` scores each Blah2 detection point independently against at most one ADS-B truth point from the nearest snapshot within `--adsb-max-age-ms`. This score is per detection point, not per CPI and not per aircraft track.

- `Rank`: Sort order of the sweep cases. With ADS-B truth, cases are ranked by higher `MatchPctPt`, then lower `FPRatePt`, then higher `MatchPts`. Without ADS-B truth, ranking falls back to `HitRate`, `MeanPeakSnr`, and `Mean/CPI`.
- `Mode`: CFAR mode used for that sweep case.
- `PostProc`: Detection post-process mode used for that sweep case. `local-peak` is the legacy nearby-peak suppression stage. `cluster-centroid` groups nearby detections and emits one weighted centroid per cluster.
- `Pfa`: CFAR probability of false alarm used for that sweep case. The table prints 3 decimal places, so values such as `1e-5` and `1e-4` will appear as `0.000`.
- `MinDoppHz`: Minimum Doppler threshold in Hz.
- `MinDelay`: Minimum delay-bin threshold applied by the detector.
- `HitRate`: Fraction of analysed CPIs that produced at least one detection. This is not truth accuracy.
- `TotalDetect`: Total number of detection points produced across all analysed CPIs.
- `Mean/CPI`: Average number of detection points per analysed CPI.
- `MeanPeakSnr`: Average peak SNR among CPIs that produced at least one detection.
- `TruthCPI`: Number of analysed CPIs that had a nearby ADS-B snapshot within the configured max-age window. A single ADS-B snapshot can be reused by more than one CPI if it is the nearest valid snapshot.
- `MatchPts`: Number of Blah2 detection points that matched an ADS-B truth point inside the configured delay and Doppler windows.
- `FalsePts`: Number of Blah2 detection points that did not match any ADS-B truth point.
- `MissedPts`: Number of ADS-B truth points that were not matched by any Blah2 detection point.
- `MatchPctPt`: `MatchPts / (MatchPts + FalsePts)`. This is effectively point-level precision.
- `FPRatePt`: `FalsePts / (MatchPts + FalsePts)`, which is equal to `1 - MatchPctPt`.

`testDetectionSweep` does not currently print recall directly. If needed, estimate truth-side recall for a sweep case as `MatchPts / (MatchPts + MissedPts)`.

In practice, a high `HitRate` with a very low `MatchPctPt` means the detector is firing often, but most of those detections do not align with ADS-B truth under the current match windows.

If you already have a saved IQ file, you can run the same comparison command directly against that file.

- *TODO:* Run all test cases.

```
sudo docker exec -it blah2 /blah2/bin/test/runall.sh
sudo docker exec -it blah2 /blah2/bin/test/unit/runall.sh
sudo docker exec -it blah2 /blah2/bin/test/functional/runall.sh
sudo docker exec -it blah2 /blah2/bin/test/comparison/runall.sh
```