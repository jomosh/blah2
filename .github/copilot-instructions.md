# blah2 Copilot Guidance

Use these instructions for work in this repository. Detailed language and workflow rules live in `.github/instructions/` and `.github/skills/`.

## Project Model

blah2 is a real-time radar pipeline with three runtime layers:

1. C++ processor in `src/` for capture, DSP, detection/tracking, serialization, and TCP output.
2. Node.js API bridge in `api/` for newline-framed TCP intake, REST endpoints, and stash aggregation.
3. Browser UI in `html/` for jQuery/Plotly polling displays and controller pages.

The standard data flow is `Capture -> IqData queue -> ambiguity/clutter/detection/tracker -> JSON over TCP -> API/stash -> Plotly pages`.

## Repository Layout

- `src/capture/`: device integrations, replay, and IQ save behavior.
- `src/process/`: DSP and analysis modules grouped by domain.
- `src/data/`: runtime data containers and JSON serialization.
- `api/`: Express server plus stash pollers.
- `html/`: plain JS/HTML frontend.
- `config/`: YAML runtime configs.
- `test/`: Catch2 unit tests and comparison workflows.

Put new code in the existing domain folders. Keep `src/blah2.cpp` for application wiring and config-driven orchestration, not for new standalone modules.

## Cross-Cutting Guardrails

- Prefer the smallest compatible change. Do not rename public routes, config keys, file formats, or major class names unless the task explicitly requires it.
- Preserve the current stack choices: C++20 + CMake, Node.js/Express, plain JS/jQuery/Plotly, Docker Compose. Do not introduce frameworks or heavy dependencies without an explicit request.
- Treat capture, DSP, serialization, sockets, stash polling, and frontend refresh loops as performance-sensitive. Avoid avoidable copies, blocking work, and verbose logging in those paths.
- YAML under `config/` is the runtime source of truth. If behavior changes, update the config examples and every consuming layer that depends on the change.
- JSON and REST changes must be traced end-to-end across C++, `api/server.js`, stash modules, and frontend consumers.

## Build And Validation

- C++ builds are strict: `CMakeLists.txt` enables `-Wall -Werror`, and `CMakePresets.json` requires C++20.
- Prefer presets over ad-hoc commands: `cmake --preset dev-release`, `cmake --build --preset dev-release`, and `ctest --preset test-all-unix-release`.
- When behavior changes, add or update the narrowest relevant Catch2 or comparison test and keep test paths mirrored to the source tree.

## Repo-Specific Footguns

- Do not store `rapidjson::StringBuffer` as a value member in copyable data classes. Reuse a function-local `thread_local static rapidjson::StringBuffer` like the existing serialization code.
- Prefer `std::unique_ptr` for new ownership boundaries, but keep compatibility with legacy raw-pointer APIs when touching older runtime code.
- Keep multi-node and deployment changes compatible with `docker-compose.yml`, host networking, optional UI service blocks, and the existing per-node port-offset pattern.
