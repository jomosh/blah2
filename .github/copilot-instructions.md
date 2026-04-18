# blah2 AI Coding Guidelines

Use these instructions for all future AI-generated code in this repository.

## 1) Project Intent and Runtime Topology

blah2 is a real-time radar processing system with three main runtime layers:

1. C++ processing core (capture + DSP + detection/tracking + TCP output).
2. Node.js API bridge (reads TCP streams and exposes REST endpoints).
3. Browser frontend (polls REST/stash endpoints and renders with Plotly/jQuery).

Primary runtime flow:

1. Capture device produces IQ sample streams (reference + surveillance).
2. C++ processing pipeline creates ambiguity map, optionally clutter-filtered.
3. Detection/tracker/spectrum/timing outputs are serialized as JSON.
4. C++ sends JSON over TCP sockets.
5. API server receives TCP payloads and serves them via HTTP routes.
6. Frontend polls endpoints and updates plots.

## 2) Repository Structure Conventions

Follow the current separation of concerns:

- `src/capture/`: SDR abstraction and device-specific implementations.
- `src/process/`: DSP/analysis algorithms grouped by domain (`ambiguity`, `clutter`, `detection`, `tracker`, `spectrum`, `utility`, `meta`).
- `src/data/`: domain data containers + JSON serialization and persistence helpers.
- `src/data/meta/`: constants/time metadata classes.
- `api/`: Node.js middleware that converts TCP outputs into REST data.
- `html/`: browser UI, with plotting scripts in `html/js/`.
- `config/`: YAML runtime config variants.
- `test/unit/`: Catch2 unit tests, mirroring `src/process/` areas.

When adding new code, place files in these domain folders first. Do not flatten modules into the top-level `src/` unless it is a true app entrypoint concern.

## 3) C++ Architecture and Coding Pattern

### 3.1 Class boundaries

Keep class responsibilities narrow and aligned to existing patterns:

- Capture classes own device interaction and replay/save behavior.
- Process classes transform data (`IqData` -> `Map` -> `Detection`/`Track`).
- Data classes own representation + JSON conversion + optional save logic.
- Utility classes handle infrastructure concerns (socket send, helpers).

### 3.2 Naming conventions

Maintain the current project naming style:

- Types/classes: PascalCase (`Capture`, `CfarDetector1D`, `SpectrumAnalyser`).
- Methods: snake_case (`set_row`, `get_nRows`, `delay_bin_to_km`).
- Variables/fields: camelCase (`minDoppler`, `saveDetectionPath`, `noisePower`).
- Constructor input aliases: underscore-prefixed parameters are acceptable in implementation (`_pfa`, `_nGuard`) where already used.
- Constants/macros/include guards: uppercase (`CAPTURE_H`, `TIMING_H`).
- File names: class-name-aligned, case-sensitive (`Tracker.cpp`, `Tracker.h`).

Do not introduce a different naming scheme in new files.

### 3.3 Header and documentation style

For new public classes/functions, follow existing Doxygen style:

- File preamble with `@file`, `@class`/`@brief`, `@author`.
- Member and method comments using `/// @brief`, with `@param`/`@return` as needed.
- Include guards in each header (not `#pragma once`, to match current repo style).

### 3.4 Memory ownership style

The codebase currently mixes raw pointers and `std::unique_ptr`.

Rules for new code:

- Prefer `std::unique_ptr` for new ownership boundaries.
- Keep compatibility with existing raw-pointer APIs when touching legacy call sites.
- Avoid introducing shared ownership unless required by lifecycle constraints.
- Do not refactor broad pointer strategy unless task explicitly requests it.

### 3.5 Error handling style

Follow practical, runtime-focused checks used in current code:

- Validate config/file/socket initialization and fail fast with clear message.
- Use simple `std::cerr`/`std::cout` diagnostics in core runtime paths.
- Return early on unrecoverable setup failures.

## 4) C++ Module Integration Rules

When adding a new processing/data/capture module:

1. Create matching header/source pair in the correct domain folder.
2. Add source file to `CMakeLists.txt` executable target (`blah2`).
3. Include only required headers; prefer project-relative includes like `"process/..."`, `"data/..."`.
4. Wire config fields through YAML parsing in the main app only where needed.
5. Emit output as JSON using existing data-class serialization patterns if data goes to API/frontend.

## 5) Build and Dependency Rules

Keep build setup consistent with current project behavior:

- CMake minimum style and single top-level `CMakeLists.txt` remain authoritative.
- Use existing dependency model (Threads, asio, ryml, httplib, Armadillo, FFTW, UHD, SDR libraries).
- Preserve strict warning settings (`-Wall -Werror`) for new C++ code.
- Do not add new heavy dependencies when existing libs can solve the problem.

## 6) Test Conventions (Catch2)

When writing tests under `test/unit/`:

- Mirror source domain path where possible.
- Use descriptive `TEST_CASE` names grouped by behavior (`"Constructor"`, `"Process_*"`).
- Keep deterministic checks for dimensions, thresholds, and expected numeric ranges.
- Use `CHECK_THAT(... WithinAbs(...))` for floating point comparisons.
- Guard optional file-data tests with `SKIP` when fixtures are absent.

If adding new module code, add at least one unit test target unless change is wiring-only.

## 7) API (Node.js) Conventions

Node layer currently uses Express + net sockets with module-level state.

For modifications/additions:

- Keep route naming stable and explicit (`/api/<topic>`, `/stash/<topic>`).
- Maintain simple JSON/text response semantics used by existing routes.
- Keep socket listeners one concern per port/topic.
- Preserve compatibility with config-driven host/port values from YAML.

Avoid introducing framework rewrites (TypeScript, heavy middleware, ORM, etc.) unless explicitly requested.

## 8) Frontend JavaScript Conventions

Frontend is plain JS with jQuery + Plotly, polling-based updates.

For new UI scripts:

- Keep scripts endpoint-driven and lightweight.
- Follow existing variable naming style (camelCase/short globals where already used).
- Support local and hosted API routing behavior (`hostname` checks and endpoint selection).
- Reuse plotting conventions (layout/config objects and incremental updates).
- Do not introduce a build system or SPA framework unless explicitly requested.

## 9) Configuration and Data Contract Rules

YAML in `config/` is the single source of runtime options.

When changing config contracts:

1. Add defaults/fields in YAML examples.
2. Parse field in C++ and/or API where consumed.
3. Preserve backward compatibility when possible.
4. Keep units explicit in code comments and names (Hz, bins, ms, m, etc.).

Data outputs to API/frontend should continue to be JSON objects with stable keys and metadata (for timestamped updates/polling).

## 10) Performance and Real-Time Guardrails

Given real-time processing constraints:

- Avoid unnecessary copies in tight DSP loops.
- Keep per-CPI allocations controlled when possible.
- Prefer pre-sized buffers and straightforward loops.
- Keep network payload generation bounded and predictable.

Do not add expensive logging or debug processing in hot paths without gating.

## 11) Non-Goals for Routine Changes

Unless explicitly requested, do not:

- Re-architect entire pointer/memory model.
- Replace API/frontend stacks with modern frameworks.
- Reorganize whole CMake layout.
- Rename public routes, config keys, or major class names.

## 12) AI Generation Checklist (Mandatory)

Before finalizing any generated change, verify:

1. File placement matches repository domain structure.
2. Naming/style matches current conventions in adjacent files.
3. CMake target wiring updated if new C++ source added.
4. YAML/config updates included if new runtime parameters introduced.
5. API/frontend contract compatibility preserved.
6. Unit tests added/updated for logic changes.
7. Doxygen comments added for new public C++ APIs.

## 13) Preferred Change Strategy

For this repository, prefer minimal, behavior-preserving changes:

- Implement smallest viable diff.
- Keep interfaces stable unless change request requires API break.
- Align new code with nearest existing module style, even if style is not universally modern.

When in doubt, optimize for consistency with the current codebase over stylistic modernization.
