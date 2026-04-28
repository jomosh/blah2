---
name: blah2-cpp-runtime-workflow
description: 'Implement or modify blah2 C++ runtime code. Use for capture, process, data, utility, config-driven pipeline, or CMake target changes in src/ and test/.'
argument-hint: 'Describe the runtime change, touched module, config fields, and expected validation.'
---

# blah2 C++ Runtime Workflow

## When To Use
- Adding or changing code in `src/capture/`, `src/process/`, `src/data/`, or `src/process/utility/`.
- Wiring new runtime behavior through `src/blah2.cpp`.
- Updating CMake targets or the matching Catch2 test surface.

## Procedure
1. Start from the owning abstraction. Use `src/blah2.cpp` for orchestration and config parsing, and keep module logic in the nearest domain folder.
2. Match the existing C++ style: PascalCase classes and files, snake_case methods, camelCase fields, uppercase include guards, and Doxygen in public headers.
3. Validate YAML-derived parameters at the boundary before threads start or heavy objects are constructed.
4. Prefer `std::unique_ptr` for new ownership boundaries, but keep compatibility with legacy raw-pointer interfaces when touching older code.
5. For JSON serialization, use `rapidjson::Document` plus allocator-backed arrays and a function-local `thread_local static rapidjson::StringBuffer`. Do not store `StringBuffer` as a value member.
6. Keep hot paths allocation-aware and logging-light, especially capture callbacks, `IqData` queue handoff, ambiguity or detection loops, and socket sends.
7. Update `CMakeLists.txt` when adding new source or test files, then run the narrowest relevant build and test.

## Repo Anchors
- `src/blah2.cpp`
- `src/process/`
- `src/data/Detection.cpp`
- `test/unit/`
- `CMakeLists.txt`

## Done Checklist
- File placement matches the domain structure.
- Config keys and JSON keys stay compatible unless the task explicitly changes them.
- Public C++ APIs are documented.
- The narrowest relevant Catch2 validation has been run.