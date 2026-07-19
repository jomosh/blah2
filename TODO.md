
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

#### Improvement 4 — CFAR: Doppler window missing on ambiguity map ✅
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
- **Status**: Implemented. Pre-computed Hann window stored in `Ambiguity::dopplerWindow` (filled in constructor, zero-division guard for `nDopplerBins==1`). Applied element-wise in the Doppler loop before `fftw_execute(fftDoppler)`. All uses of `M_PI` removed: both the Hann window formula and the `dopplerPhase` step computation now use `std::numbers::pi` (C++20); `<math.h>` replaced with `<numbers>`. New Catch2 tests in `test/unit/process/ambiguity/TestAmbiguity.cpp`: sidelobe suppression ≥15 dB verified, single-bin passthrough verified.

#### Improvement 5 — CFAR: minDelay/minDoppler not excluded from training cells ✅
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
- **Status**: Implemented. Excluded cells contribute `0.0` to the prefix-sum array in `CfarDetector1D::process()`, preventing clutter in the exclusion zone from inflating CFAR thresholds for neighbouring bins. Leading-window bounds (`leadingStart`, `leadingEnd`) are clamped to `firstValidIdx` so excluded cells are excluded from both the energy sum and the training-cell count `nLeading`. `firstValidIdx` is computed once per `process()` call (above the Doppler loop) since `x->delay[]` is constant across Doppler rows. New Catch2 tests in `test/unit/process/detection/TestDetectionPhaseA.cpp`: excluded-zone clutter does not raise threshold for valid targets, no detection produced inside exclusion zone.

#### Improvement 6 — Tracker: greedy nearest-neighbour association ✅
- **File**: `src/process/tracker/Tracker.cpp` (~line 75–96), `Tracker.h` (`@todo` lines 7–9)
- **Root cause**: Track-to-detection association uses a first-match-wins nearest-neighbour
  search.  In dense-target scenarios two tracks competing for the same detection cause one to
  be spuriously updated and the other to coast.  No global cost matrix is maintained.
- **Fix**: Replace greedy scan with a simple Hungarian (Jonker-Volgenant) assignment.  The
  cost metric is Euclidean distance in normalised delay-Doppler space.  Libraries:
  `lap` (header-only C++11) or `dlib::hungarian`.  Only the association step changes; the
  gate, M-of-N logic, and state machine remain the same.
- **Effort**: Medium — ~50 lines replacing the inner association loop.
- **Status**: Implemented. Self-contained O(n³) Kuhn-Munkres (Hungarian) algorithm added as a static function in the anonymous namespace of `Tracker.cpp` (`#include <algorithm>` added for `std::max`). `update()` refactored into five phases: predict, build cost matrix, assign, apply associations, remove inactive (backwards loop for index stability). New Catch2 tests in `test/unit/process/tracker/TestTracker.cpp`: two-track globally-optimal assignment verified with per-track Doppler assertions that distinguish Hungarian from greedy (a greedy algorithm would swap the Doppler assignments and fail the checks); single-track nearest-detection selection verified. All 6 tracker test cases pass.

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

#### Improvement 11 — Track-Before-Detect (TBD): multi-frame energy integration for sub-noise-floor targets 🟡
- **File**: `src/process/tracker/Tracker.cpp/.h`, `src/process/detection/CfarDetector1D.cpp/.h`,
  `src/data/Detection.h`, `src/blah2.cpp`, `config/config.yml`
- **Root cause**: CFAR thresholds are set by PFA (default `1e-5`) to keep false-alarm rates
  low.  This inherently rejects targets whose ambiguity-map power is below the local noise
  estimate × threshold scale factor.  Real targets follow physically-consistent trajectories
  (smooth range/Doppler evolution constrained by kinematics); noise peaks are spatially and
  temporally random.  The tracker already maintains kinematic state but only operates on
  CFAR-detected peaks — weak targets that never cross the CFAR threshold are invisible to it.
- **Approach**: Introduce a "candidate" track class that operates at a lower effective
  threshold and uses multi-frame non-coherent energy integration along hypothesised
  trajectories to confirm tracks.
  1. Add a `detection.pfaCandidate` config key (e.g., `1e-2`) to run a second CFAR pass at
     a higher PFA, producing candidate detections below the primary threshold.
  2. Add a `Detection::isCandidate` flag so the tracker can distinguish primary from
     candidate detections.
  3. Introduce a new track state `CANDIDATE` (alongside existing `TENTATIVE`, `ACTIVE`,
     `COAST`, `INACTIVE`).  Candidate tracks are initiated from candidate detections and
     accumulate raw ambiguity-map power along their kinematic trajectory across consecutive
     CPIs.
  4. The Kalman filter predicts where the candidate should appear in the next CPI; the
     raw map value at the predicted bin is read and accumulated into an energy buffer.
     No Hungarian association is needed for candidates — the kinematic prediction alone
     provides the bin index (one candidate → one predicted cell).
  5. A candidate is promoted to `TENTATIVE` → `ACTIVE` when its accumulated energy crosses
     a configurable `energyThreshold`.  This exploits the fact that real targets produce
     consistent energy at the same range-Doppler cell across frames, while false alarms
     are randomly distributed.
  6. M-of-N logic already exists for tentative→active confirmation; it is reused with
     separate `mCandidate`/`nCandidate` parameters for the candidate→tentative transition.
  7. When `process.tracker.tbd.enable: false`, the pipeline is unchanged (backward compat).
- **Constraint — raw map access**: The tracker currently receives `Detection` objects from
  CFAR, not raw map data.  Candidate energy integration requires the tracker to read the
  raw range-Doppler map at predicted bin positions.  Two options:
  - (a) Pass the raw `Map` pointer to the tracker's `update()` method (simpler, couples
    tracker to Map).
  - (b) Have the detection stage tag each candidate detection with its raw map power at
    emission time (cleaner, keeps tracker agnostic to Map).
  Option (b) is preferred as it preserves separation of concerns.
- **Performance**: Candidates add O(N_candidates) per CPI, where N_candidates is bounded by
  the higher-PFA CFAR output.  Typical N_candidates is 2–10× primary detections.  The main
  cost is the Kalman predict step + energy accumulation for each candidate track object.
  A `maxCandidates` config cap guards against explosion.
- **Config**:
  ```yaml
  process:
    detection:
      pfa: 1e-5            # primary CFAR PFA (unchanged)
      pfaCandidate: 0      # secondary PFA for TBD candidates; 0 = disabled
    tracker:
      enable: true
      tbd:
        enable: false       # off by default
        mCandidate: 5       # M-of-N for candidate → tentative confirmation
        nCandidate: 8
        energyThreshold: 0.0  # if >0, accumulated raw-map energy threshold for promotion
        maxCandidates: 50     # cap to prevent explosion under heavy false alarms
  ```
- **Effort**: Medium-High (~150–200 lines). New candidate-track state machine, raw-map
  energy accumulation, config wiring, Catch2 tests with synthetic weak-target trajectories.
- **Status**: Not implemented.

#### Improvement 12 — Two-pass CFAR: primary + secondary threshold with tracker persistence confirmation 🟡
- **File**: `src/process/detection/CfarDetector1D.cpp/.h`, `src/data/Detection.h`,
  `src/process/tracker/Tracker.cpp/.h`, `src/blah2.cpp`, `config/config.yml`
- **Root cause**: A single CFAR pass at low PFA rejects targets that are consistently present
  but just below the threshold.  A second pass at a higher PFA produces many false alarms,
  but real targets appear repeatedly at the same range-Doppler cell across CPIs while
  false alarms are randomly distributed.  The tracker's existing M-of-N logic can exploit
  this temporal persistence to separate real weak targets from noise.
- **Approach**:
  1. Add `detection.pfaSecondary` config key (e.g., `1e-3` or `1e-2`).  When >0, a second
     CFAR pass is run using the higher PFA.
  2. The second pass reuses the same prefix-sum precomputation as the primary pass; only the
     threshold-comparison loop is repeated with a different scale factor.  Cost is negligible
     (<1% CPI increase).
  3. Detections from the second pass are tagged with `isSecondary: true` in the `Detection`
     struct.  Primary detections (lower PFA) are treated as now — eligible for immediate
     track association.
  4. Secondary detections enter the tracker with a `CANDIDATE` flag (see Proposal 11 for
     the candidate state machine).  The tracker accepts them only if they persist across
     `mCandidate` of `nCandidate` CPIs.  Candidates that do not persist are dropped.
  5. Unlike full TBD (Proposal 11), this approach does *not* accumulate raw map energy —
     it relies purely on detection persistence.  It is simpler to implement and can be a
     stepping stone to full TBD.
  6. When `pfaSecondary: 0`, the second pass is disabled (backward compat).
- **Performance**: The second CFAR thresholding loop is O(nDelayBins × nDopplerBins) with
  a simple scalar comparison per bin — negligible.  Tracker overhead depends on the number
  of secondary detections; a configurable `maxSecondaryDetections` cap is recommended.
- **Config**:
  ```yaml
  process:
    detection:
      pfa: 1e-5
      pfaSecondary: 0       # higher PFA for secondary pass; 0 = disabled
      maxSecondaryDetections: 100  # cap secondary detections per CPI
  ```
- **Relationship to Proposal 11**: This is a subset of TBD — persistence-only confirmation
  without energy integration.  It is the recommended first step; if detection yield is still
  insufficient, full TBD with energy accumulation can be added on top.
- **Effort**: Low-Medium (~60–80 lines). Second threshold scale factor, second detection
  collection loop, `Detection::isSecondary` flag, tracker M-of-N reuse for candidates,
  config wiring, Catch2 tests.
- **Status**: Not implemented.

#### Improvement 13 — Non-coherent multi-frame map accumulation (persistence map) before CFAR 🟡
- **File**: New `src/process/detection/MapAccumulator.cpp/.h` (or extend
  `src/process/ambiguity/Ambiguity.cpp`), `src/blah2.cpp`, `config/config.yml`
- **Root cause**: After coherent integration (CAF), a weak target may sit at 3–8 dB SNR in
  the range-Doppler map — too low for a CFAR PFA of 1e-5 without excessive false alarms.
  Non-coherently averaging the *magnitude* of consecutive maps over K frames reduces the
  noise variance by √K (gain = 5·log₁₀(K) dB), pulling a persistent weak target above
  the effective noise floor while suppressing random noise peaks.
- **Approach**:
  1. Add a new processing stage `MapAccumulator` (or extend `Ambiguity`) that sits between
     the ambiguity map output and the CFAR detector input.
  2. Maintain a circular buffer of the K most recent *magnitude* (not complex) range-Doppler
     maps.  On each new map, compute the element-wise mean or median of the buffer and
     output the averaged map to CFAR.
  3. **EMA mode** (recommended): Use an exponential moving average with configurable α:
     `mapAvg[i] = α × mapNew[i] + (1−α) × mapAvg[i]`.  This gives continuous noise
     reduction without a hard K-frame lag and can be tuned with a single parameter.
     α = 0.1 means the effective window is ~1/α = 10 frames (~5 dB noise reduction).
  4. **Window mode**: Simple sliding-window mean over K frames.  Produces a sharper
     transition but introduces K-frame latency before weak targets become visible.
  5. **Median mode**: Element-wise median over K frames.  More robust to impulsive
     interference but higher compute cost (requires sorting per cell).
  6. Key trade-off: non-coherent averaging smears fast-moving targets because their
     range-Doppler cell changes between frames.  For targets moving ≤ a few m/s, the
     smearing over K=10–30 frames is acceptable.  For fast targets, use a smaller K or
     consider Doppler-compensated accumulation (Proposal 11 TBD handles this better).
  7. The existing `Ambiguity::compute_()` already performs a limited form of averaging via
     `mapWindow` and `nAverage`, but that averages the *complex* accumulator before
     magnitude extraction — coherent averaging that loses energy when phase drifts between
     CPIs.  Non-coherent averaging of the magnitude map captures energy that coherent
     averaging discards.
- **Performance**: O(nRangeBin × nDopBin) per CPI for the averaging step.  With typical
  600×200 = 120k cells, this is ~0.5 MFLOP — negligible (<0.1% CPI increase).
  Memory: K × 120k × sizeof(float) = ~9.6 MB for K=20, well within budget.
- **Config**:
  ```yaml
  process:
    mapAccumulator:
      enable: false
      mode: "ema"           # "ema" | "window" | "median"
      emaAlpha: 0.1         # α for EMA mode (0 < α ≤ 1; smaller = more averaging)
      windowSize: 20        # K for "window" or "median" modes
  ```
- **Relationship to Proposal 11 (TBD)**: Multi-frame accumulation is a pre-detection
  technique (operates on the map before CFAR); TBD is a post-detection technique (operates
  on candidate tracks after CFAR).  They are complementary: accumulation can pull targets
  above the CFAR threshold so they appear as primaries; TBD catches the remaining targets
  still below even the accumulated noise floor.
- **Effort**: Low-Medium (~50–80 lines new code, config wiring, Catch2 test with synthetic
  weak-target injection at known range-Doppler cell).
- **Status**: Not implemented.

## Signal-Specific Waveform Reconstruction for PBR

### Concept & Motivation

Passive bistatic radar (PBR) works by correlating a reference channel (direct-path
illuminator signal) against a surveillance channel (target echoes).  When the
illuminator signal is *below the thermal noise floor* — as is the case with GNSS
(approximately −25 to −30 dB SNR in a 2 MHz bandwidth), Inmarsat, and distant DVB-T/DAB
transmitters — the noisy reference raises the ambiguity-map noise floor and degrades
or prevents detection.

**This is *not* full demodulation.**  We do not need to decode payload data
(navigation messages, MPEG transport streams, audio).  What we need is **waveform
reconstruction**: regenerating the clean transmitted waveform so it can serve as a
high-SNR reference for the existing cross-ambiguity processing chain.

The reconstructed waveform is produced by a **background thread** (same pattern as the
existing ADS-B ingestion thread in `blah2.cpp`) and consumed inside the CPI loop via a
thread-safe buffer.  The Wiener-Hopf → ambiguity → CFAR → centroid → tracker pipeline
is **unchanged**.

### Processing Gain Budget

blah2's default config (0.75 s CPI, 2 MHz bandwidth) provides ~61.8 dB of
time-bandwidth integration gain from the ambiguity processing alone.  Adding
waveform-specific processing gain from the reconstruction stage (e.g. ~30 dB code
correlation gain for GPS C/A) pushes the total processing gain to ~90+ dB, pulling
signals from ~30 dB *below* the noise floor to ~60 dB *above* it in the final ambiguity
map.  The limiting factor becomes the **surveillance-channel echo SNR** (fixed by the
bistatic radar equation), not the reference quality.

### Reference vs. Surveillance — Where Reconstruction Helps

| Scenario | Reference SNR (typical) | Impact of Reconstruction |
|---|---|---|
| DVB-T (near tower, <30 km) | +40 to +60 dB | Minor (cleaner ambiguity baseline, ~1–3 dB clutter improvement).  Primarily mitigates SFN multipath artefacts. |
| DVB-T (distant, >80 km) | 0 to +10 dB | Significant.  Reference noise floor drops, enabling detection that a direct-path reference would miss. |
| GNSS (any range) | −25 to −30 dB | **Critical.**  Without reconstruction the reference noise dominates and detections are impossible. |
| Inmarsat (L-band) | −10 to +10 dB | Significant.  Enables reliable reference at modest dish sizes. |
| FM Radio | +50 to +70 dB | Negligible.  Direct reference is already nearly perfect. |

### Architecture

```
Reference IQ ──► Waveform Recon (background thread) ──► cleanRef buffer ──►
                                                                           │
Surveillance IQ ──────────────────────────────────────────► WienerHopf ──► Ambiguity ──► ...
```

A new abstract base class `WaveformReconstructor` in `src/process/waveform/` defines
the interface.  Concrete implementations live in domain subdirectories
(`src/process/waveform/dvbt/`, `src/process/waveform/inmarsat/`, etc.).  Reconstruction
runs in a detached `std::thread` at configurable cadence and writes to a
`thread-safe` ring buffer that the main CPI loop picks up.

New YAML section:
```yaml
process:
  waveform:
    enable: false
    type: "dvbt"   # dvbt | dab | inmarsat | gnss-gps-l1
    dvbt:
      bandwidth: 8e6       # 8 MHz (6/7/8 supported)
      mode: "8k"           # 2k | 4k | 8k
      constellation: "qam64"
      codeRate: "2/3"      # auto-detect or hard-set
    dab:
      mode: 1              # transmission mode I/II/III/IV
    inmarsat:
      satellite: "4f3"     # satellite identifier for frequency lookup
      channel: "psmc"      # PSMCh/PSMCi type
      baudRate: 600
      frequency: 1546050000
    gnss:
      system: "gps-l1"     # gps-l1 | galileo-e1 | glonass-l1
      maxSatellites: 12
```

### Phased Implementation Plan

#### Phase 1 — DVB-T & DAB (Proof of Concept) 🟢

*Priority:* Highest.  DVB-T/DAB provide strong terrestrial reference signals in most
deployment scenarios (tens of kW ERP), the OFDM structure is simple to synchronise to,
and constellation demodulation/remodulation (QPSK/16-QAM/64-QAM) is well-documented.
SFN multipath mitigation via clean reconstruction is a tangible benefit even when the
reference is already strong.

- **DVB-T waveform chain:** Timing synchronisation (Schmidl-Cox or CP correlation) →
  fractional/coarse frequency offset correction → FFT per OFDM symbol → channel
  estimation from scattered/continual pilots → equalisation → constellation demap →
  remodulate clean symbols → IFFT → output as IQ stream.
- **DAB waveform chain:** Null symbol detection → PRS-based fine sync → OFDM
  demodulation → DQPSK demap → frequency deinterleaver → time deinterleaver →
  remodulate clean → IFFT.
- **New files:**
  - `src/process/waveform/WaveformReconstructor.h` — abstract interface
  - `src/process/waveform/WaveformReconstructor.cpp`
  - `src/process/waveform/dvbt/DvbtReconstructor.h/.cpp`
  - `src/process/waveform/dab/DabReconstructor.h/.cpp`
- **Config:** `process.waveform.enable`, `process.waveform.type`, `*.dvbt.*`, `*.dab.*`
- **Compute budget:** ~15–20% of one core for DVB-T 8K mode (FFT every ~1 ms +
  pilot processing + remod).  Easily fits alongside the CPI loop.
- **Effort:** Medium (150–250 lines new C++, new config keys, background thread
  wiring, Catch2 tests).

#### Phase 2 — Inmarsat (GEO Satellite) 🟡

*Priority:* Medium.  Inmarsat satellites are geostationary, eliminating Doppler
complexity.  Their L-band downlinks (1.5 GHz band) can be received with modest RHCP
patch antennas or small dishes, giving a stable, predictable reference.  Inmarsat uses
narrowband channels (600–1200 baud BPSK/QPSK), so the reconstruction compute cost is
negligible.

- **Waveform chain:** Carrier frequency acquisition (FFT-based or PLL) → symbol timing
  recovery (Gardner or early-late) → frame sync on known Unique Word patterns →
  remodulate clean symbols with correct timing → output as IQ stream.
- **Frequency plan:** Inmarsat-4/5/6 satellites provide global L-band coverage.
  Relevant channels: PSMCh (600 bps), PSMCi (1200 bps), navigation channels.  Multiple
  satellites visible simultaneously for multistatic geometry.
- **New files:** `src/process/waveform/inmarsat/InmarsatReconstructor.h/.cpp`
- **Config:** `process.waveform.inmarsat.*`
- **Compute budget:** <1% of one core (narrowband, low symbol rate).
- **Effort:** Medium (100–150 lines new C++, config extension, tests).

#### Phase 3 — GNSS (GPS L1 C/A, Galileo E1) 🟡

*Priority:* Medium-high once DVB-T/Inmarsat are validated.  GNSS enables true
multistatic PBR with 8–12 simultaneous transmitters (one per visible satellite),
providing RCS diversity and improved track continuity.  However, tracking 12+
satellites simultaneously + correlating each as a separate ambiguity-map reference adds
compute cost.

- **Waveform chain (per satellite):** Code-phase acquisition (parallel code-phase
  search via FFT correlation) → carrier tracking (Costas PLL) → code tracking (DLL) →
  regenerate PRN spreading code chips → output as IQ stream.  One channel per
  satellite; all channels summed for a single composite reference, or kept separate
  for per-satellite ambiguity maps.
- **Processing gain:** GPS L1 C/A: code despreading ~30 dB + CPI integration ~62 dB =
  ~92 dB total.  Galileo E1 BOC(1,1) offers similar performance.
- **New files:** `src/process/waveform/gnss/GnssReconstructor.h/.cpp`
- **Config:** `process.waveform.gnss.*`
- **Compute budget:** 12-channel GPS L1: ~5–10% of one core.  If running per-satellite
  ambiguity maps (12× the maps), compute becomes significant; consider a shared
  composite map or limiting to M strongest satellites.
- **Effort:** High (200–400 lines new C++, careful PRN code phase/pseudorange
  alignment, per-satellite pipeline design).

#### Phase 4 — DVB-S2 (GEO Broadcast) 🔴

*Priority:* Low.  Requires a steerable dish with LNB, APSK carrier recovery, and FEC
framing knowledge.  Defer until Phases 1–3 prove the reconstruction architecture.
- **Effort:** High (unknown, significant DVB-S2 framing complexity).

### Performance Budget for the CPI Loop

The reconstruction runs **outside** the CPI hot path (background thread).  The only CPI
cost is a mutex-guarded copy of the latest clean reference buffer into the main thread,
which is O(1) with a ring buffer and negligible latency (<10 μs).

### Config-Driven Behaviour

- `process.waveform.enable` — master on/off (default `false` for backward compat)
- `process.waveform.type` — selects the reconstructor factory
- Sub-keys per type (see YAML sketch above)
- When `enable: false`, the existing capture IQ path is used unchanged

### Testing Strategy

- **Unit tests:** Inject a synthetic DVB-T OFDM frame (known pilots + random symbols)
  into `DvbtReconstructor`, assert remodulated output matches clean symbols within
  acceptable EVM (<1% for high SNR input).  Repeat for DAB, Inmarsat, GNSS.
- **Integration test:** Record a live DVB-T capture as `.iq` replay file, configure
  blah2 to use the reconstructor, run through the full pipeline, compare detection
  metrics (hit rate, false-positive rate) against direct-reference baseline.
- **Performance test:** Assert reconstruction thread CPU utilisation stays within a
  configurable budget (e.g. <20% of one core) on representative hardware.

### References & Design Notes

- The Wiener-Hopf clutter filter benefits from a cleaner reference: the Toeplitz
  autocorrelation matrix is better-conditioned, improving cancellation depth (estimated
  3–5 dB improvement for DVB-T at medium range).
- The existing ambiguity processing chain (`Ambiguity.cpp`, `WienerHopf.cpp`,
  `CfarDetector1D.cpp`) is **not modified**.  The reconstructed reference is injected
  as a drop-in replacement for the raw reference-channel IQ data.
- The background-thread + thread-safe-buffer pattern follows the existing ADS-B
  ingestion implementation in `blah2.cpp` (lines 299–326).
- Waveform-specific FFTW plans (OFDM demodulation) can reuse the project's existing
  FFTW infrastructure and wisdom-file pattern.

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
- [ ] Add "Tuning for Weak Targets" section to user guide
  **Status:** Not Started (Easy — documentation only)
  Document how to maximise processing gain through config tuning for sub-noise-floor target
  detection.  The ambiguity function already implements a matched filter (optimal for known
  waveform in white Gaussian noise), and many integration-gain parameters are exposed in YAML
  but are not documented with their detection-sensitivity implications:
  - **`capture.fs` (sample rate / bandwidth):** wider bandwidth → more independent samples →
    higher time-bandwidth product → more integration gain.  However, wider bandwidth also
    increases noise power linearly (kTB term).  The net SNR improvement from increasing
    bandwidth alone is 0 dB (signal and noise scale equally), but the *post-CPI*
    signal-to-noise ratio improves as 10·log₁₀(N_independent_samples) because the CAF
    correlates across the whole band.  Users should match bandwidth to the illuminator's
    occupied bandwidth (e.g., ~8 MHz for DVB-T, ~1.5 MHz for DAB, ~200 kHz for FM).
  - **`process.data.cpi` (coherent processing interval):** longer CPI → more coherent
    integration → gain ∝ 10·log₁₀(CPI × fs).  Limited by target coherence time:
    CPI ≤ λ / (2·a_max), where λ is wavelength and a_max is max target acceleration.
    A target accelerating at 10 m/s² at 600 MHz (λ = 0.5 m) decorrelates after ~0.16 s.
    For typical aircraft (≤2 m/s²), a CPI of 0.75 s is reasonable.
  - **`process.ambiguity.dopplerMin`/`dopplerMax` (Doppler window):** wider Doppler window →
    more Doppler bins, each with fewer correlation samples (nCorr = nSamples / nDopplerBins).
    The CAF processing gain per bin is ∝ nCorr, so there is a direct trade-off between
    Doppler coverage and per-bin SNR.  Only open the Doppler window as wide as needed for
    expected target velocities.
  - **`process.ambiguity.delayMin`/`delayMax` (range window):** wider delay window → more
    range bins.  Does not directly affect per-bin SNR (fixed CPI), but the total number of
    range-Doppler cells increases, which affects CFAR threshold scaling.
  - **Bistatic radar equation:** the document should explain the full gain budget from antenna
    to CFAR output: transmit EIRP → free-space path loss (reference & surveillance paths) →
    target RCS → antenna gain → receiver noise figure → CPI integration gain → CFAR
    threshold.  This helps users understand which losses dominate and whether a weak target
    is recoverable or fundamentally below the thermal floor.
  - **`process.detection.pfa`:** reducing PFA (e.g., 1e-4 → 1e-3) lowers the CFAR threshold
    and captures weaker targets at the cost of more false alarms.  The tracker's M-of-N
    logic filters transient false alarms.  Document the PFA vs detection-probability trade-off
    for a given per-cell SNR (standard Marcum Q-function curves for CA-CFAR).
  - **Document location:** new file `doc/tuning-guide.md`, linked from `README.md` and the
    main `doc/` index.  Update `.github/deepseek.md` to reference it.

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