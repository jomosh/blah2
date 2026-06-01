# blah2 — DeepSeek Project Context

This file provides project context for DeepSeek-powered code assistance. It consolidates
knowledge from `.github/copilot-instructions.md`, `.github/instructions/`, `.github/agents/`,
and `.github/skills/` into a single reference.

---

## Project Model

blah2 is a **real-time radar pipeline** with three runtime layers:

1. **C++ processor** (`src/`) — Capture, DSP (ambiguity, clutter cancellation, CFAR detection,
   tracking), JSON serialization, TCP output.
2. **Node.js API bridge** (`api/`) — Newline-framed TCP intake, REST endpoints, stash aggregation.
3. **Browser UI** (`html/`) — jQuery/Plotly polling displays and controller pages.

**Standard data flow:**
```
Capture -> IqData queue -> ambiguity/clutter/detection/tracker -> JSON over TCP -> API/stash -> Plotly pages
```

## Repository Layout

| Path | Purpose |
|---|---|
| `src/capture/` | SDR device integrations, replay, IQ save/load |
| `src/process/` | DSP and analysis modules (ambiguity, clutter, detection, tracker, spectrum, utility) |
| `src/data/` | Runtime data containers and JSON serialization (IqData, Map, Detection, Track, Timing) |
| `api/` | Express server (`server.js`), stash pollers (`stash/detection.js`, `stash/iqdata.js`, etc.) |
| `html/` | Frontend pages and JS (plain JS/jQuery/Plotly, `common.js` helpers, `lib/` vendor assets) |
| `config/` | YAML runtime configs (device-specific, pipeline parameters) |
| `test/` | Catch2 unit tests (mirror `src/` layout), comparison sweeps |
| `doc/` | Deployment examples, Doxygen HTML output |
| `docker/` | Additional Dockerfiles (kraken, UHD) |
| `host/` | Reverse-proxy nginx config and host deployment files |

## Stack & Constraints

- **C++20 + CMake** with `-Wall -Werror`, strict presets (`dev-release`, `test-all-unix-release`).
- **Node.js/Express** for the API bridge — **no** framework rewrites, persistent databases, or
  extra middleware layers.
- **Plain JS/jQuery/Plotly** frontend — **no** SPA frameworks, build systems, or component libraries.
- **Docker Compose** deployment with host networking, optional web service blocks, per-node configs.
- **YAML** under `config/` is the runtime source of truth.

## Cross-Cutting Guardrails

1. **Smallest compatible change.** Do not rename public routes, config keys, file formats, or major
   class names unless the task explicitly requires it.
2. **Preserve stack choices.** C++20, Node.js/Express, plain JS, Docker Compose. No new frameworks
   or heavy dependencies without explicit request.
3. **Performance-sensitive paths.** Capture callbacks, DSP loops, serialization, sockets, stash
   polling, and frontend refresh loops are hot paths. Avoid avoidable copies, blocking work, and
   verbose logging.
4. **Config-driven behavior.** YAML is the runtime source of truth. If behavior changes, update
   config examples and every consuming layer.
5. **End-to-end JSON/REST tracing.** Changes must be traced across C++, `api/server.js`, stash
   modules, and frontend consumers in the same change.

---

## Per-Layer Development Guidance

### C++ Runtime (`src/`, `CMakeLists.txt`)

- Put new modules in the nearest domain folder (`src/capture/`, `src/process/<domain>/`,
  `src/data/`, `src/process/utility/`). Keep `src/blah2.cpp` for orchestration/config wiring only.
- **Naming:** PascalCase classes and file names, snake_case methods, camelCase fields, uppercase
  include guards. Doxygen `@file`, `@class`, `@brief`, `@param`/`@return` on public headers.
- **Ownership:** Prefer `std::unique_ptr` for new boundaries. Keep compatibility with legacy
  raw-pointer APIs when touching older code.
- **JSON serialization:** Use `rapidjson::Document` + allocator-backed arrays + function-local
  `thread_local static rapidjson::StringBuffer`. Never store `StringBuffer` as a value member
  in copyable data classes.
- **Validation:** Validate YAML parameters at the owning boundary before threads start or heavy
  runtime objects are constructed.
- **CMake:** Update `CMakeLists.txt` when adding new source or test files. Run the narrowest
  relevant build and test.

### Node.js API Bridge (`api/`)

- `api/server.js` reads a YAML config path from argv. Keep host/port config-driven.
- **Route families:** `/api/*`, `/stash/*`, `/capture`. Do not rename or restructure them unless
  the task explicitly changes the contract.
- **TCP framing:** Input from C++ uses newline-delimited framing. Respect frame boundaries and
  pending-buffer limits.
- **Stash polling:** Stash modules poll `/api/timestamp` first, only fetch heavier payload when
  timestamp changes. Current cadence: 100 ms.
- **Responses:** Simple JSON or text with `no-cache` and CORS headers already set.
- **Payload changes:** Update `server.js`, the relevant stash module, and every affected frontend
  consumer in the same change.

### Frontend Visualization (`html/`, `host/html/`)

- **Plain HTML + jQuery/Plotly.** No SPA framework or build system.
- **API URLs:** Build through `html/js/common.js` helpers to keep localhost, reverse-proxy, IPv6,
  `api_port`, and `api_base` behavior intact.
- **Polling:** Displays poll every ~100 ms. Prefer incremental Plotly updates. Only rebuild when
  trace/layout shape truly changes.
- **Compatibility:** Keep pages working with both default local deployment and hosted/reverse-proxied
  deployments.
- **Style:** Small scripts, direct endpoint calls, minimal state.

### Testing & Validation (`test/`, `CMakeLists.txt`)

- **Unit tests** mirror the `src/` layout. Built as explicit Catch2 executables in `CMakeLists.txt`.
- **Naming:** Descriptive `TEST_CASE` names, deterministic inputs, `CHECK_THAT(... WithinAbs(...))`
  for floating-point comparisons.
- **Comparison/workflow:** `test/comparison/TestDetectionSweep.cpp` for tuning and replay analysis.
  Unit tests are for pass/fail correctness.
- **Scope:** If a change touches config, serialization, or cross-layer behavior, validate the
  narrowest relevant unit test and, when appropriate, the replay workflow in `test/README.md`.
- **CMake:** Keep new targets wired into CMake presets and runnable through CTest or focused binary
  invocation.

### Build Commands

```bash
cmake --preset dev-release
cmake --build --preset dev-release
ctest --preset test-all-unix-release
```

---

## Key Agent-Derived Checklists

### Code Review Gate

When reviewing a PR or changeset, verify:

- [ ] C++ capture, DSP, serialization, and threading correctness.
- [ ] Node.js TCP framing, stash freshness, and route stability.
- [ ] Frontend polling, Plotly updates, hosted/local API targeting.
- [ ] Docker, config, multi-node ports, hardware-runtime assumptions.
- [ ] No renames of public routes, config keys, or file formats unless intentional.
- [ ] JSON/API contract breaks are traced end-to-end across all layers.
- [ ] Test coverage matches change scope.

### Detection/Tracker Tuning Gate

When changing detection or tracking:

- [ ] Baseline established (current thresholds, post-processing mode).
- [ ] Metrics defined (hit rate, match percentage, false-positive rate, missed truth points,
      latency cost).
- [ ] Validation run with narrowest relevant Catch2 target.
- [ ] For detector/tracker tuning: use `test/comparison/TestDetectionSweep.cpp` with `.iq` + optional
      `.adsb` sidecar files.
- [ ] Results evaluated as: `HitRate`, `MatchPts`, `FalsePts`, `MissedPts`, `MatchPctPt`,
      `FPRatePt`, runtime cost.
- [ ] No regression in existing tests.
- [ ] Missing fixtures or hardware needs documented explicitly.

### Performance Budget

When diagnosing performance:

- [ ] Baseline established for the affected layer (latency, throughput).
- [ ] Per-layer budget or target defined.
- [ ] Smallest measurement that can confirm/falsify the hypothesis identified.
- [ ] Quick wins separated from structural changes.
- [ ] No correctness or data-contract stability trade-off without explicit approval.
- [ ] Rollback options documented.

### Hardware Integration Gate

When adding or changing SDR device support:

- [ ] Device-specific code in its own folder under `src/capture/<device>/`.
- [ ] Config-driven factory pattern preserved.
- [ ] Hardware-specific settings validated early (unsupported gain, serial, or rate combinations
      rejected before capture starts).
- [ ] Two-channel IQ save/replay format preserved.
- [ ] Thread-safe `IqData` handoff semantics preserved.
- [ ] Callbacks allocation-aware and logging-light.
- [ ] `config/*.yml`, `CMakeLists.txt`, and deployment docs updated.
- [ ] Narrowest unit or runtime check validated; hardware prerequisites noted.

### Multi-Node Deployment Gate

When changing deployment:

- [ ] Ports are unique per node.
- [ ] Host networking, optional web service blocks, explicit per-node config files, and isolated
      save paths preserved.
- [ ] Per-node port offset pattern followed.
- [ ] API targeting compatible with `api_port` and `api_base` query parameters.
- [ ] Device exclusivity, `/dev/usb` access, host resource conflicts considered.
- [ ] UI-on and headless paths both understandable.
- [ ] Operator-facing docs match compose and config behavior.

### Data Contract Change Gate

When changing JSON payloads, TCP frames, API routes, or stash/frontend consumers:

- [ ] Contract traced end to end: C++ serialization → socket sender → `api/server.js` → stash module → frontend consumer.
- [ ] Newline-delimited TCP framing preserved.
- [ ] Route families preserved (`/api`, `/stash`, `/capture`) unless explicitly changed.
- [ ] Stash modules remain timestamp-gated before fetching heavy payloads.
- [ ] `html/js/common.js` updated only if routing behaviour must change; `api_base`, `api_port`, localhost, and IPv6 handling preserved.
- [ ] Producer, bridge, and consumer changes landed together (no layering drift).
- [ ] Both payload shape and polling behaviour validated with the narrowest available check.

---

## Task Backlog (TODO.md)

For the current task backlog and planned work, read `TODO.md` at the project root. It contains:

- **Capture & DSR** — SDR device support, replay modes, sample alignment.
- **DSP & Processing** — Known bugs and improvements (Wiener-Hopf, Ambiguity, CFAR, Tracker, SpectrumAnalyser) with implementation status, file references, and effort estimates.
- **Data & Serialization** — JSON schema stability, persistence, metadata.
- **API Layer** — REST endpoints, error handling, hot-reload, TCP optimization.
- **Frontend UI** — Plotly visualizations, refresh optimization, controls.
- **Testing** — Catch2 coverage, integration tests, fixtures.
- **Configuration** — YAML schema docs, validation, env var overrides.
- **Performance & Real-Time** — Hot path profiling, memory allocation, network payload benchmarking.
- **3lips Localisation Readiness** — Headless mode, multi-node deployment, acceptance gates.

**Usage tip:** Mention "read TODO.md" in your prompt when asking DeepSeek to plan future work, so it loads the current backlog into context.

---

## Detection Sweep (testDetectionSweep)

For detector/tracker tuning and replay validation, use `test/comparison/TestDetectionSweep.cpp`.

### Capturing Replay Data

1. Configure for live run (not replay), with saves writable and `truth.adsb.enabled: true`:
   ```yaml
   capture:
     replay:
       state: false
   truth:
     adsb:
       enabled: true
   save:
     path: "/blah2/save/"
   ```
2. Start `blah2` and the API server normally.
3. Start/stop recording via the IQ Capture button on a detection map page, or:
   ```
   curl http://127.0.0.1:3000/capture/toggle
   ```
4. blah2 writes a canonical two-channel interleaved IQ file (`*.rspduo.iq` or `*.hackrf.iq`).
   If `truth.adsb.enabled`, an ADS-B sidecar (`*.adsb`) is also written.

### Running a Sweep

```bash
docker exec -it blah2 /blah2/bin/test/comparison/testDetectionSweep \
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

### Sweep Column Meanings (with ADS-B truth)

| Column | Meaning |
|---|---|
| `Rank` | Sort order: higher `MatchPctPt`, then lower `FPRatePt`, then higher `MatchPts` |
| `Mode` | CFAR mode (e.g. CAGO) |
| `PostProc` | Post-process mode (`local-peak` or `cluster-centroid`) |
| `Pfa` | CFAR Pfa (printed to 3 dp; `1e-5` appears as `0.000`) |
| `MinDoppHz` | Min Doppler threshold (Hz) |
| `MinDelay` | Min delay-bin threshold |
| `HitRate` | Fraction of analysed CPIs with ≥1 detection |
| `TotalDetect` | Total detection points across all CPIs |
| `Mean/CPI` | Mean detections per CPI |
| `MeanPeakSnr` | Mean peak SNR among CPIs with ≥1 detection |
| `TruthCPI` | CPIs with a nearby ADS-B snapshot |
| `MatchPts` | Detections matched to ADS-B truth within delay/Doppler windows |
| `FalsePts` | Detections not matched to any ADS-B truth |
| `MissedPts` | ADS-B truth points not matched by any detection |
| `MatchPctPt` | `MatchPts / (MatchPts + FalsePts)` — point-level precision |
| `FPRatePt` | `FalsePts / (MatchPts + FalsePts)` = `1 - MatchPctPt` |

Without ADS-B truth, ranking falls back to `HitRate`, `MeanPeakSnr`, then `Mean/CPI`.

---

## Repo-Specific Footguns

1. **`rapidjson::StringBuffer` in copyable classes.** Never store as a value member. Use
   `thread_local static` inside serialization methods (existing pattern).
2. **Wiener-Hopf Cholesky failure.** When reference signal is weak, Cholesky decomposition of
   the autocorrelation matrix can fail. Fix: diagonal loading (configurable via
   `process.clutter.diagonalLoadScale` in YAML).
3. **Ambiguity map delay bin offset.** The expression `- 1 + 1` in `Ambiguity.cpp` is a no-op.
   If adjusting delay indexing, verify with the deterministic delay-pin test
   (`Process_DelayBinPin` in `TestAmbiguity.cpp`).
4. **SpectrumAnalyser center frequency.** Was hardcoded to 204.64 MHz. Now takes `fc` constructor
   parameter. Ensure call-sites pass the config value.
5. **CFAR exclusion zone training cells.** Cells inside `minDelay`/`minDoppler` zones now
   contribute 0.0 to prefix-sum, preventing threshold inflation near exclusion boundaries.
6. **Tracker association.** Uses Hungarian algorithm (not greedy nearest-neighbour). Cost matrix
   is Euclidean distance in normalised delay-Doppler space.
7. **Hardcoded literals.** Always check for hardcoded frequencies, ports, or paths. These should
   be config-driven via YAML.
8. **Docker container names.** No fixed container names to allow multi-instance deployment.
9. **Stash API base.** Uses `common.js` helpers for API targeting. Changes must preserve
   `api_base`, `api_port`, localhost, and IPv6 handling.

---

## Known Issues (from TODO.md)

### Critical Bugs (Implemented Fixes)

- **Wiener-Hopf Cholesky failure** → Diagonal loading via YAML key `process.clutter.diagonalLoadScale`
  (default `1e-6`). Also guards: `delayMax < delayMin` clamped, `nBins > nSamples` clamped.
- **Ambiguity delay offset** → No-op `- 1 + 1` cleaned up. Deterministic delay-pin test covers
  D ∈ {-5,-3,-1,0,1,3,5,10} for both `roundHamming` settings.
- **SpectrumAnalyser hardcoded fc** → `double fc` constructor parameter added. Unit tests verify
  center bin at fc across multiple frequencies.

### Improvements (Implemented)

- Doppler Hann window applied before FFT (sidelobe suppression ≥15 dB verified).
- CFAR exclusion zones excluded from training-cell energy sum (not just post-threshold deletion).
- Tracker uses Hungarian algorithm for globally-optimal track-to-detection association.

### Remaining Work

- Tracker α-β smoothing (improves position output quality).
- Tracker tentative-track cap (prevents explosion under heavy false alarms).
- WienerHopf pre-allocation audit (heap churn reduction).
- FFTW_MEASURE instead of FFTW_ESTIMATE (20-40% faster FFTs with one-time planning cost).
- Multi-node acceptance gates (24h headless run, 2-node concurrent run, backward compat).
- DSP pipeline performance regression (CPI ~1650-1750 ms vs ~1500 ms baseline).

---

## Config (YAML) Reference

Config lives under `config/`. Key structure:

```yaml
capture:
  # Device type, fc, fs, gain, serial, sample_count (nSamples)
  replay: true/false, file: path/to/iq
  save: true/false, path: /tmp/
process:
  ambiguity:
    delayMin: int, delayMax: int, doppler: int
  clutter:
    delayMin: int, delayMax: int, nfilt: int
    diagonalLoadScale: float (default 1e-6)
  detection:
    pfa: float, guard: int, reference: int
    minDelay: int, minDoppler: int
  tracker:
    tAcc: int, tFree: int, tInit: int, tDrop: int
    gate: float, snrMin: float
    accMin: float, accMax: float
  spectrum:
    bandwidth: float, logScale: bool, averaging: int
```

---

## Key Files Index

| File | What It Does |
|---|---|
| `src/blah2.cpp` | Main entry point: config parsing, pipeline wiring, capture/process/socket orchestration |
| `src/capture/Capture.cpp` | Capture thread: device read, IqData queue push |
| `src/process/ambiguity/Ambiguity.cpp` | Cross-ambiguity function (delay-Doppler map) |
| `src/process/clutter/WienerHopf.cpp` | Clutter cancellation via Wiener-Hopf + Cholesky |
| `src/process/detection/CfarDetector1D.cpp` | CFAR detection in 1D (prefix-sum based) |
| `src/process/detection/Centroid.cpp` | Detection centroiding and interpolation |
| `src/process/tracker/Tracker.cpp` | Multi-target tracker with Hungarian association |
| `src/data/Detection.cpp` | Detection JSON serialization |
| `src/data/IqData.cpp` | IQ data container with zero-copy semantics |
| `api/server.js` | Express API server: TCP intake, REST endpoints, stash aggregation |
| `api/stash/detection.js` | Detection stash poller |
| `html/js/common.js` | Frontend API URL helpers |
| `html/js/plot_detection.js` | Detection Plotly display |
| `config/config.yml` | Default runtime config |
| `docker-compose.yml` | Multi-service deployment |
| `test/unit/` | Catch2 unit tests (mirrors `src/` layout) |
| `test/comparison/TestDetectionSweep.cpp` | Detection tuning sweep with replay data |
| `test/README.md` | Replay and validation workflow docs |