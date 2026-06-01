
# blah2 TODO

## Capture & DSR

- [X] Expand SDR device support (UHD integration, additional hardware)
- [X] Add replay/file-based capture modes
- [ ] Add 2x RTL-SDR sample-time alignment for shared-clock setups without shared trigger (use a noise source or equivalent startup calibration to establish a common trigger after device start)
- [ ] Optimize IQ sample streaming performance

## DSP & Processing

- [ ] Enhance clutter filtering algorithms
- [ ] Improve detection/tracking robustness
- [ ] Optimize ambiguity map computation
- [X] Add spectrum analysis features

### DSP Pipeline — Known Issues (code-analysis pass, 2026-05-31)

The items below came from a full static analysis of `src/process/` and the test suite.
Each entry has enough context to work in a fresh checkout without re-reading the sources.

#### Bug 1 — Wiener-Hopf: Cholesky failure drops entire CPI silently 🔴
- **File**: `src/process/clutter/WienerHopf.cpp` (~line 116), `WienerHopf.h` (`@todo` line 7)
- **Root cause**: `arma::chol(A, A)` decomposes the autocorrelation Toeplitz matrix `A` of size
  `nBins × nBins` (where `nBins = delayMax - delayMin`, ~410 for the default config).
  When the input signal is weak or the reference power drops (e.g. transmitter off-air), `A`
  becomes ill-conditioned and Cholesky fails. The current fallback is `return false`, which
  causes `blah2.cpp` to skip clutter filtering for that entire CPI and pass the raw,
  clutter-dominated IQ through to the ambiguity map and CFAR.  The result is hundreds of
  spurious detections per CPI and a potential cascade of tentative-track explosions in the
  tracker (see Bug 8 below).
- **Fix**: Add diagonal loading (Tikhonov regularisation) immediately before the `arma::chol`
  call.  The current implementation uses
  `ε = |a[0]| * diagonalLoadScale / nBins`, where `a[0]` is the zero-lag autocorrelation
  and `diagonalLoadScale` is configurable from YAML (`process.clutter.diagonalLoadScale`).
  This keeps the matrix positive-definite while avoiding a full-matrix norm in the CPI loop.
  Example:
  ```cpp
  const double eps = std::abs(a[0]) * diagonalLoadScale / nBins;
  A.diag() += std::complex<double>(eps, 0.0);
  // then proceed with arma::chol(A, A) as before
  ```
  **Note on `nBins`**: fixed to `delayMax - delayMin + 1` (matching header documentation and
  `Ambiguity::nDelayBins` for the same range).  The constructor now also guards
  `delayMax < delayMin` with a clamp-and-log before computing `nBins`, preventing uint32_t
  underflow and the associated allocation blowup. Constructor now also clamps
  `nBins` to `nSamples` when `delayMax - delayMin + 1 > nSamples`, preventing
  out-of-bounds reads of `dataA`/`dataB` in `process()`.
- **Effort**: ~3 lines in `WienerHopf.cpp`.
- **Test**: Validate three classes of behavior in unit tests:
  1. Constructor/process safety guards (`delayMax < delayMin`, `delayMin == delayMax`,
     `delay window > CPI length`).
  2. Near-zero reference failure path is graceful (`process()` returns `false`, no crash).
  3. Quantitative clutter suppression and target retention (SIR improves materially).
- **Status**: Implemented and expanded.
  Additional updates completed in this context:
  - `process.clutter.diagonalLoadScale` added to YAML and wired through `blah2.cpp` into
    `WienerHopf` (default `1e-6`, validated non-negative finite).
  - `nfilt` selection changed to nearest FFTW-fast size (small-prime factors) to avoid
    pathological FFT lengths.
  - Oversized delay-window guard added: `nBins` clamped to `nSamples`.
  - CPI regression improved from ~5400 ms to ~1650-1750 ms after performance fixes;
    still above pre-regression ~1500 ms baseline.
  Unit tests added in `test/unit/process/clutter/TestWienerHopf.cpp`:
  `WienerHopf_Constructor_InvertedRange_NoCrash` (REQUIRE_NOTHROW on delayMax<delayMin),
  `WienerHopf_InvertedRange_ProcessReturnsTrue` (1-bin filter returns true on sinusoidal input),
  `WienerHopf_EqualRange_OneBin_ProcessReturnsTrue` (delayMin==delayMax, 1 tap),
  `WienerHopf_CustomDiagonalLoadScale_ProcessReturnsTrue`,
  `WienerHopf_DelayWindowLargerThanCpi_ClampedProcessReturnsTrue`,
  `WienerHopf_ZeroReference_ReturnsFalseNoCrash` (zero reference → Cholesky fails gracefully),
  `WienerHopf_ClutterSuppressionAndTargetRetention` (quantitative DSP regression guard).
  Target `testWienerHopf` wired into `CMakeLists.txt` and `add_test`.

#### Bug 2 — Ambiguity: map delay bins may be offset by 1 🔴
- **File**: `src/process/ambiguity/Ambiguity.cpp` (~line 157), `Ambiguity.h` (`@todo` line 7)
- **Root cause**: The correlation slice is written to the map with:
  ```cpp
  map->data[i][j] = dataCorr[nDelayBins + delayMin + j - 1 + 1];
  ```
  The `- 1 + 1` is a no-op (it was introduced piecemeal during a previous debug attempt and
  never resolved).  Static analysis of `dataCorr` shows the algebraic mapping
  `dataCorr[nDelayBins + lag]` ↔ delay `lag` is self-consistent, and the existing test
  `Process_PairedBufferSkewMigratesPeakAcrossDelayBins` expects peak at bin 0 for an aligned
  signal and bin ±1 for a 1-sample-skewed signal.  It is therefore **not clear** whether the
  offset is in the delay dimension or whether the TODO is stale.
- **Investigation needed**:
  1. Write a deterministic test: inject a surveillance signal that is a perfect copy of the
     reference delayed by exactly `D` samples (e.g. `D=5`); assert the ambiguity map peak is at
     `map->delay[j] == D`.  Run with multiple `D` values and both `roundHamming` settings.
  2. Cross-check the Doppler fftshift: the current formula
     `(j + int(nDopplerBins/2) + 1) % nDopplerBins` has been verified as intentional (it
     accounts for the asymmetric deque construction of `map->doppler`); do not remove the `+1`.
  3. Clean up the no-op `- 1 + 1` once the delay-pin test is green, to prevent future
     confusion.
- **Effort**: test writing ~20 lines; code cleanup 1 line.
- **Status**: No-op `- 1 + 1` cleaned up in `Ambiguity.cpp`. Deterministic delay-pin test
  `Process_DelayBinPin` added to `TestAmbiguity.cpp` covering D ∈ {-5,-3,-1,0,1,3,5,10}
  for both `roundHamming` settings. Source-buffer sizing in this test now uses
  `nSamples + max(abs(delayMin), abs(delayMax)) + 1` to remain safe if delay bounds are
  edited independently. Whether a residual offset exists is now gated on that test going green.

#### Bug 3 — SpectrumAnalyser: center frequency hardcoded to 204.64 MHz 🔴
- **File**: `src/process/spectrum/SpectrumAnalyser.cpp` line ~35, `SpectrumAnalyser.h`
- **Root cause**: The frequency-bin vector is computed as:
  ```cpp
  frequencyBins[i] = ((bin * bandwidth) + offset + 204640000) / 1000;
  ```
  The literal `204640000` Hz is the DAB multiplex the author's hardware listens to.  For any
  other deployment (HackRF at FM frequencies, USRP at L-band, etc.) the spectrum display will
  show wrong absolute frequencies.  The config `capture.fc` value is already available in
  `blah2.cpp` as `uint32_t fc` but is not forwarded to the constructor.
- **Fix**: Add a `double fc` parameter to `SpectrumAnalyser(uint32_t n, double bandwidth,
  double fc)` and replace the literal.  Update the single call-site in `blah2.cpp`:
  ```cpp
  SpectrumAnalyser *spectrumAnalyser = new SpectrumAnalyser(nSamples, spectrumBandwidth, fc);
  ```
- **Effort**: ~5 lines across `SpectrumAnalyser.h`, `.cpp`, and `blah2.cpp`.
- **Status**: Implemented. `fc` constructor parameter added; hardcoded literal removed;
  `blah2.cpp` call site updated. Unit tests added in
  `test/unit/process/spectrum/TestSpectrumAnalyser.cpp`:
  `SpectrumAnalyser_FrequencyBins_CenterBinAtFc` (GENERATE across 100/204.64/433.92 MHz),
  `SpectrumAnalyser_FrequencyBins_SpacingEqualsBandwidth` (uniform spacing check),
  `SpectrumAnalyser_FrequencyBins_AxisSymmetricAroundFc` (fftshift symmetry).
  `IqData::get_frequency()` const getter added to support the tests.
  Target `testSpectrumAnalyser` wired into `CMakeLists.txt` and `add_test`.

### Session Progress Update (2026-06-01)

- Completed: Bug 1, Bug 2, Bug 3 implementations and targeted unit coverage.
- Completed: Clutter performance triage and mitigation for WienerHopf FFT sizing regression.
- Completed: YAML-configurable diagonal loading with inline and README guidance.
- Completed: Safety guard for `nBins > nSamples` to prevent OOB reads.
- Pending validation on target machine:
  - Re-run full unit suite and replay workflow in Docker with current branch.
  - Confirm CPI gap closure from current ~1650-1750 ms toward ~1500 ms baseline.

#### Improvement 4 — CFAR: Doppler window missing on ambiguity map 🟠
- **File**: `src/process/ambiguity/Ambiguity.cpp` (Doppler FFT section), `Ambiguity.h`
- **Root cause**: No windowing function is applied to the `dataDoppler` vector before
  `fftw_execute(fftDoppler)`.  The implicit rectangular window produces Doppler sidelobes at
  −13 dB.  Strong zero-Doppler clutter residual bleeds 30+ dB into adjacent Doppler bins,
  raising CFAR thresholds and masking slow-moving targets.
- **Fix**: Apply a Hann (raised-cosine) window to `dataDoppler[j]` before executing
  `fftDoppler`.  A pre-computed window vector `dopplerWindow` (member of size `nDopplerBins`)
  can be filled in the constructor and multiplied element-wise in the Doppler loop.
  Hann gives −31.5 dB first sidelobe, Chebyshev/DPSS gives lower still.  The window must be
  applied before Doppler FFT only, not to the range-correlation axis (which uses `fftZi`).
- **Effort**: ~10 lines (constructor fill + element-wise multiply in `process()`).
- **Config impact**: None; the window type could be a future config option.
- **Status**: Not implemented.

#### Improvement 5 — CFAR: minDelay/minDoppler not excluded from training cells 🟠
- **File**: `src/process/detection/CfarDetector1D.cpp`, `CfarDetector1D.h` (`@todo` line 6)
- **Root cause**: The `minDelay` and `minDoppler` exclusion zones are applied
  *post-threshold* (detections inside them are deleted after detection).  Cells inside the
  exclusion zone still contribute to training windows for neighbouring cells.  This elevates
  CFAR thresholds in the border region just outside the exclusion zone, suppressing
  low-Doppler or short-range targets that are otherwise valid.
- **Fix**: Mask exclusion-zone cells as `0.0` power before building the prefix-sum array for
  each Doppler slice.  Alternatively, track an "excluded" flag per cell so training-cell
  counts adjust accordingly (avoid under-counting valid training cells near the boundary).
- **Effort**: Medium — needs careful prefix-sum adjustment.
- **Status**: Not implemented.

#### Improvement 6 — Tracker: greedy nearest-neighbour association 🟠
- **File**: `src/process/tracker/Tracker.cpp` (~line 75–96), `Tracker.h` (`@todo` lines 7–9)
- **Root cause**: Track-to-detection association uses a first-match-wins nearest-neighbour
  search.  In dense-target scenarios two tracks competing for the same detection cause one to
  be spuriously updated and the other to coast.  No global cost matrix is maintained.
- **Fix**: Replace greedy scan with a simple Hungarian (Jonker-Volgenant) assignment.  The
  cost metric is Euclidean distance in normalised delay-Doppler space.  Libraries:
  `lap` (header-only C++11) or `dlib::hungarian`.  Only the association step changes; the
  gate, M-of-N logic, and state machine remain the same.
- **Effort**: Medium — ~50 lines replacing the inner association loop.
- **Status**: Not implemented.

#### Improvement 7 — Tracker: no track smoothing (α-β filter) 🟡
- **File**: `src/process/tracker/Tracker.cpp/.h`, `src/data/Track.h` (`@todo` line 13)
- **Root cause**: Tracker stores the full association history per track but never uses it for
  smoothing.  The output position jumps between raw detection positions each CPI, producing
  noisy range/Doppler outputs and sub-optimal gate placement for the next CPI.
- **Fix**: Implement a two-state α-β smoother (no matrix required) on the delay and Doppler
  axes.  The smoothed state drives the kinematic prediction (`delayPredict`, `dopplerPredict`)
  and the reported track position.  α and β can be derived from the track manoeuvring
  parameter already in the config.
- **Effort**: Medium (~30 lines new logic, no API change).
- **Status**: Not implemented.

#### Improvement 8 — Tracker: tentative-track explosion under heavy false alarms 🟡
- **File**: `src/process/tracker/Tracker.cpp` (~line 102–117)
- **Root cause**: Every unassociated detection spawns `2*nAcc + 1` new tentative tracks
  (one per acceleration hypothesis).  With `maxAcc=10 Hz/s` and `cpi=0.75 s` that is 36 new
  tracks per false alarm per CPI.  When the clutter filter fails (see Bug 1) and CFAR produces
  hundreds of false alarms, the tentative-track count explodes.  O(nTracks × nDetections)
  association then consumes seconds of CPU per CPI.
- **Fix**: Cap the total tentative-track count (e.g. `maxTentative=200`).  When the cap is
  reached, suppress new initiation until count drops.  Log a warning.  Also consider a
  Doppler-gated initiation zone: only initiate tracks for detections outside a configurable
  zero-Doppler exclusion band.
- **Effort**: Small (~5 lines + new config key).
- **Status**: Not implemented.

#### Improvement 9 — WienerHopf: matrix allocated per CPI (heap churn) 🟡
- **File**: `src/process/clutter/WienerHopf.cpp` constructor vs `process()`
- **Root cause**: `A`, `a`, `b`, `w` are declared as class members but in `process()` Armadillo
  may re-allocate internally when `arma::toeplitz(a)` creates a new matrix.  Confirm with
  Armadillo `set_size` pre-allocation in the constructor to guarantee in-place reuse.
- **Fix**: After construction, call `A.set_size(nBins, nBins)` and in `process()` use
  `arma::toeplitz(a, A)` (in-place form if available) or manually assign into `A` row by row.
- **Effort**: Small.
- **Status**: Not confirmed whether Armadillo reuses or reallocates; needs profiling.

#### Improvement 10 — FFTW: ESTIMATE mode instead of MEASURE 🟡
- **File**: All FFTW plan creation calls in `Ambiguity.cpp`, `WienerHopf.cpp`,
  `SpectrumAnalyser.cpp` — all use `FFTW_ESTIMATE`.
- **Root cause**: `FFTW_ESTIMATE` chooses a plausible plan without measuring.  On the actual
  deployment hardware `FFTW_MEASURE` would select an optimal plan (typically 20–40% faster
  FFTs) at the cost of a one-time ~1 s planning run at startup.
- **Fix**: Switch to `FFTW_MEASURE` and optionally serialise the plan to a wisdom file
  (`fftw_export_wisdom_to_filename`) so subsequent startups skip the planning step.
- **Effort**: Small (constructor change + wisdom file path in config).
- **Status**: Not implemented.

## Data & Serialization

- [ ] Review JSON schema stability for API contract
- [X] Add data persistence/logging features
- [ ] Improve metadata handling and timestamps

## API Layer (Node.js)

- [X] Expand REST endpoint coverage
- [X] Implement error handling/logging
- [ ] Add configuration hot-reload support
- [ ] Performance optimize TCP socket handling

## Frontend UI

- [ ] Enhance Plotly visualizations
- [ ] Add real-time plot refresh optimization
- [ ] Implement user controls for filtering/scaling
- [ ] Improve responsive layout

## Testing

- [ ] Expand Catch2 unit test coverage
- [ ] Add integration tests for end-to-end flows
- [ ] Improve test fixtures and determinism

## Configuration

- [ ] Document YAML schema and defaults
- [ ] Add configuration validation
- [ ] Support environment variable overrides

## Documentation

- [ ] Add architecture documentation
- [ ] Create API endpoint reference
- [X] Document deployment steps

## Performance & Real-Time

- [ ] Profile hot paths in DSP pipeline
- [ ] Optimize memory allocations in tight loops
- [ ] Benchmark network payload generation
- [X] A feature to turn off the UI when running blah2 nodes for 3lips ingestion

## Installation Architecture

- [X] Refactor code so multiple blah2 nodes can run on the same host (currently there's a conflict between other shared docker images)

## 3lips Localisation Readiness (Q2-Q3 2026)

### Q2 Milestones

- [X] Add headless node mode profile (run processor + API without web UI)
- [X] Remove fixed Docker container names to allow multi-instance deployment
- [X] Parameterize API/web port bindings per node instance
- [X] Replace hardcoded localhost:3000 assumptions in stash modules
- [X] Add null-safe handling for `/capture` polling in capture thread

### Q3 Milestones

- [X] Validate 2x node deployment on one host with independent configs and ports
- [ ] Add startup preflight checks for occupied ports and SDR device lock conflicts
- [ ] Add observability endpoints for node health and data freshness
- [ ] Document multi-node deployment and 3lips integration patterns

### Acceptance Gates

- [ ] Gate A: Headless node mode runs 24h without UI containers and without API schema changes
- [ ] Gate B: Two nodes run concurrently for 8h on one host with zero port bind/connect conflicts
- [ ] Gate C: Single-node default workflow remains backward compatible