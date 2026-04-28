---
description: "Use when editing blah2 API bridge files, TCP framing, stash modules, or REST endpoints in api/. Covers config-driven ports, newline frames, and stash freshness."
applyTo:
  - "api/**/*.js"
  - "api/package.json"
---
# API Bridge Guidance

- `api/server.js` reads a YAML config path from argv. Keep host and port behavior config-driven rather than hardcoding local defaults into new routes.
- Preserve the existing route families: `/api/*`, `/stash/*`, and `/capture`. Do not rename or restructure them unless the task explicitly changes the contract.
- TCP input from the C++ process is newline-delimited framing. Respect frame boundaries and pending-buffer limits when changing socket handling.
- Stash modules poll `/api/timestamp` first and only fetch the heavier payload when the timestamp changes. The current cadence is 100 ms; keep new work compatible with that model.
- Keep responses simple JSON or text with the same no-cache and CORS behavior already set in `server.js`.
- Avoid framework rewrites, persistent databases, or extra middleware layers unless explicitly requested.
- If a payload shape changes, update `server.js`, the relevant stash module, and every affected frontend consumer in the same change.