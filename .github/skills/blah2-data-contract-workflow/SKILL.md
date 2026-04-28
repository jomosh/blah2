---
name: blah2-data-contract-workflow
description: 'Change blah2 JSON outputs, TCP frames, Node stash behavior, or frontend consumers. Use for Detection, Track, Map payloads, server.js routes, stash modules, controller links, or Plotly pages.'
argument-hint: 'Describe the payload or route change, the producer and consumer layers involved, and the compatibility requirements.'
---

# blah2 Data Contract Workflow

## When To Use
- Changing JSON emitted by C++ data classes.
- Modifying TCP transport, `api/server.js`, stash modules, or frontend consumers.
- Adding a new route or display that depends on existing transport patterns.

## Procedure
1. Trace the contract end to end: C++ serialization or socket sender, `api/server.js`, the relevant `api/stash/*.js` module, and each frontend page that consumes the payload.
2. Preserve newline-delimited TCP framing and the existing route families `/api`, `/stash`, and `/capture` unless the task explicitly changes them.
3. Keep `server.js` config-driven and stash modules timestamp-gated before they fetch heavier payloads.
4. Update `html/js/common.js` only if routing behavior must change, and preserve `api_base`, `api_port`, localhost, and IPv6 handling.
5. Land cross-layer changes together. Do not leave the producer, bridge, and consumer out of sync.
6. Validate both payload shape and polling behavior with the narrowest check you can run.

## Repo Anchors
- `src/data/Detection.cpp`
- `api/server.js`
- `api/stash/detection.js`
- `html/js/common.js`
- `html/js/plot_detection.js`

## Done Checklist
- JSON keys, timestamps, and units are consistent across layers.
- Routes and query parameters remain backward compatible unless intentionally changed.
- The UI still resolves the correct API base in local and hosted scenarios.