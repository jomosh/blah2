---
name: blah2-multi-node-deployment-workflow
description: 'Modify docker-compose, per-node configs, headless mode, ports, or deployment docs for blah2 single-node and multi-node operation.'
argument-hint: 'Describe the deployment change, node count, hardware allocation, and the compatibility or validation constraints.'
---

# blah2 Multi-Node Deployment Workflow

## When To Use
- Editing `docker-compose.yml`, config files, or deployment documentation.
- Adding headless nodes, port offsets, or host-specific deployment changes.
- Planning or validating multi-node behavior on one host.

## Procedure
1. Start from `docker-compose.yml` and `doc/example-deployments/multi-node/README.md`.
2. Preserve host networking, optional web service blocks, explicit per-node config files, and isolated save paths.
3. Follow the existing per-node port offset pattern and keep API targeting compatible with `api_port` and `api_base` query parameters.
4. Consider device exclusivity, `/dev/usb` access, and host resource conflicts before claiming a deployment will work.
5. If the UI is optional for the scenario, keep that path explicit rather than folding it into hidden defaults.
6. Validate API reachability, frontend targeting, and the absence of bind or routing conflicts.

## Repo Anchors
- `docker-compose.yml`
- `doc/example-deployments/multi-node/README.md`
- `host/README.md`
- `config/`

## Done Checklist
- Ports are unique per node.
- UI-on and headless paths are both still understandable.
- Operator-facing docs match the compose and config behavior.