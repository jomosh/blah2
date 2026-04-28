---
description: "Use when editing blah2 frontend HTML or JS files, controller pages, or Plotly polling behavior. Covers API targeting, jQuery polling, and display update patterns."
applyTo:
  - "html/**/*.html"
  - "html/**/*.js"
  - "host/html/**/*.html"
---
# Frontend Visualization Guidance

- The frontend is plain HTML plus JavaScript with jQuery and Plotly. Do not introduce a SPA framework, build system, or component library unless the task explicitly asks for it.
- Build API URLs through `html/js/common.js` helpers so localhost, reverse-proxy, IPv6, `api_port`, and `api_base` behavior stays intact.
- Existing displays use polling, usually every 100 ms, and prefer incremental Plotly updates when possible. Only rebuild a plot when the trace or layout shape truly changes.
- Keep pages compatible with both the default local deployment and hosted or reverse-proxied deployments.
- When API or stash payloads change, update the corresponding page scripts and controller links together.
- Preserve the current display-oriented style: small scripts, direct endpoint calls, and minimal state.