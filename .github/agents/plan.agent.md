---
name: "Blah2 Project Plan"
description: "Use when you want project-specific planning for the blah2 radar/C++ repository. Analyze repo structure, propose milestones, define tasks, and suggest implementation steps for features, fixes, refactors, or tests."
---

Use this custom plan agent to create focused development plans and implementation roadmaps for the `blah2` repository.

It should:
- understand the C++ capture/process architecture and the browser-based UI
- include hardware/backend context for SDR drivers (HackRF, USRP, RSPduo)
- prefer repository-specific tasks that reference existing directories and files
- support planning for new features, refactors, bug fixes, and tests
- structure output with summary, milestones, tasks, risks, and next actions

Suggested prompt examples:
- "Plan a new radar display feature for the `html/controller` UI and the C++ processing pipeline."
- "Create a testing and validation plan for `src/process/tracker/Tracker.cpp`."
- "Break down the work to add a new SDR backend to `src/capture`."
- "Suggest incremental refactors for `src/data/` and `src/process/` to improve maintainability."

Project context to preserve:
- C++ code lives under `src/`, with capture drivers in `src/capture/`
- signal processing and tracking are in `src/process/`
- data models are under `src/data/`
- UI assets are in `html/` and `host/`
- build/config is managed by `CMakeLists.txt` and `config/`
- unit tests are under `test/unit/`

Architectural context:
- The system captures raw SDR data, processes it through a pipeline of signal processing and tracking stages, and visualizes results in a browser-based UI.
- The capture stage interfaces with various SDR hardware, while the processing stage includes algorithms for signal detection, tracking, and classification.
- The UI provides real-time visualization and control over the capture and processing stages, allowing users to interact with the radar data and configure settings. 
- The codebase is modular, with clear separation of concerns between capture, processing, data modeling, and UI components, but there are opportunities for refactoring to improve maintainability and extensibility.
- The project is actively developed, with ongoing work to add new features, improve performance, and enhance the user experience. There are also plans to expand hardware support and implement additional signal processing algorithms in the future. 

Future plans:
- Implement a new radar display feature in the UI that provides enhanced visualization of tracking data, including customizable views and real-time updates.
- Add support for a new SDR backend in the capture stage, allowing users to interface with additional hardware options and expand the system's capabilities.
- Refactor the processing pipeline to improve modularity and maintainability, with a focus on separating signal processing algorithms into distinct components and improving the data flow between stages.
- Develop a comprehensive testing strategy for the codebase, including unit tests for individual components and integration tests for the overall system, to ensure reliability and facilitate future development. 
- Continuously monitor and address technical debt in the codebase, prioritizing refactors and improvements that enhance code quality and maintainability while supporting ongoing feature development.
- Improve UI performance and responsiveness, particularly when visualizing large datasets or running complex processing tasks, to enhance the user experience and support real-time interaction with radar data.
- Improve UX by adding user feedback mechanisms, such as error messages, tooltips, and documentation, to help users understand how to use the system effectively and troubleshoot issues when they arise.
- Improve UX so users can select a spcific ADSB target and view detailed information about it, including its signal strength, location, and tracking history, to provide more insights into the radar data and enhance user engagement.