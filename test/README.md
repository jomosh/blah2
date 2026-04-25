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

4. Replay that captured file in the detector sweep comparison.

Do not change `capture.replay.state` to `true` for this step. That setting is used by the main `blah2` runtime when you want the full application to replay data instead of reading live hardware. `testDetectionSweep` only needs the replay file path, either from `--replay-file` or from `capture.replay.file` in the config.

```
sudo docker exec -it blah2 /blah2/bin/test/comparison/testDetectionSweep \
	--config /blah2/config/config.yml \
	--replay-file /blah2/save/20260425-153000.rspduo.iq \
	--pfa 1e-5,1e-4,1e-3 \
	--min-doppler 5,10,15 \
	--min-delay 0,5,10 \
	--cfar-modes CAGO
```

If you already have a saved IQ file, you can run the same comparison command directly against that file.

- *TODO:* Run all test cases.

```
sudo docker exec -it blah2 /blah2/bin/test/runall.sh
sudo docker exec -it blah2 /blah2/bin/test/unit/runall.sh
sudo docker exec -it blah2 /blah2/bin/test/functional/runall.sh
sudo docker exec -it blah2 /blah2/bin/test/comparison/runall.sh
```