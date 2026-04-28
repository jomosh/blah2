---
description: "Use when adding tests, changing validation workflows, or touching build configuration in blah2. Covers Catch2 structure, comparison sweeps, and preset-based validation."
applyTo:
  - "test/**/*.cpp"
  - "test/**/*.md"
  - "CMakeLists.txt"
  - "CMakePresets.json"
---
# Testing And Validation Guidance

- Unit tests mirror the `src/` layout and are built as explicit Catch2 executables in `CMakeLists.txt`.
- Use descriptive `TEST_CASE` names, deterministic inputs, and `CHECK_THAT(... WithinAbs(...))` for floating-point comparisons.
- Comparison workflows such as `test/comparison/TestDetectionSweep.cpp` are for tuning and replay analysis. Keep unit tests responsible for pass or fail correctness checks.
- If a change touches config, serialization, or cross-layer behavior, validate the narrowest relevant unit test and, when appropriate, the replay workflow documented in `test/README.md`.
- Keep new targets wired into CMake presets and runnable through CTest or a focused binary invocation.
- If a fixture is optional or missing from the repo, fail clearly or skip explicitly instead of relying on hidden local files.