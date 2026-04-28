---
description: "Use when editing blah2 C++ runtime files, capture sources, DSP/process modules, data classes, or CMake wiring. Covers naming, ownership, serialization, and real-time constraints."
applyTo:
  - "src/**/*.h"
  - "src/**/*.cpp"
  - "CMakeLists.txt"
  - "CMakePresets.json"
---
# C++ Runtime Guidance

- Put new runtime code in `src/capture/`, `src/process/<domain>/`, `src/data/`, or `src/process/utility/`. Avoid growing `src/blah2.cpp` with new module logic.
- Match existing naming and file layout: PascalCase classes and file names, snake_case methods, camelCase fields, and uppercase include guards.
- Public headers should use the existing Doxygen style with `@file`, `@class`, `@brief`, and `@param` or `@return` where needed.
- Validate YAML-driven parameters at the owning boundary before threads start or heavy runtime objects are constructed.
- Prefer `std::unique_ptr` for new ownership boundaries, but keep compatibility with legacy raw-pointer call sites when changing older code.
- Do not add non-copyable RapidJSON members to copyable data classes. Use a function-local `thread_local static rapidjson::StringBuffer` inside serialization methods.
- Capture callbacks, `IqData` handoff, ambiguity/detection loops, and socket send paths are performance-sensitive. Keep allocations and logging out of the hot path.
- When adding a new module, update `CMakeLists.txt` and add the narrowest mirrored Catch2 test unless the change is wiring-only.