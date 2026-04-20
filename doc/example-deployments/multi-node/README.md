# Multi-node blah2 deployment (same host)

This deployment pattern is for running multiple blah2 nodes that feed data into 3lips.

## Goals

1. Run 1+ independent blah2 nodes on one host.
2. Keep UI optional so nodes can run headless.
3. Preserve API payload compatibility for 3lips.

## Files

- Compose template: `docker-compose.multi-node.yml`
- Node overrides:
  - `compose/nodes/node1.yml`
  - `compose/nodes/node2.yml`
- Config templates:
  - `config/config-node-template-rspduo.yml`
  - `config/config-node-template-usrp.yml`
  - `config/config-node-template-hackrf.yml`
  - `config/config-node-template-kraken.yml`
  - `config/config-node-template.yml` (legacy/general template)

## Setup

1. Create one config file per node from the template:

```bash
cp config/config-node-template-rspduo.yml config/config-node1-rspduo.yml
cp config/config-node-template-usrp.yml config/config-node2-usrp.yml
```

2. Update node override files under `compose/nodes/` so each node points to the intended config file and save path.

3. Edit each node config:

- Ensure every node has a unique `network.ports` block.
- Recommended offset pattern per node index `n` (starting at 0):
  - API: `3000 + 100n`
  - map/detection/track/adsb: `3001-3004 + 100n`
  - timestamp/timing/iqdata/config: `4000-4003 + 100n`

4. Start each node in headless mode (no UI) as a separate compose project:

```bash
docker compose -p blah2-node1 \
  -f docker-compose.multi-node.yml \
  -f compose/nodes/node1.yml up -d

docker compose -p blah2-node2 \
  -f docker-compose.multi-node.yml \
  -f compose/nodes/node2.yml up -d
```

5. Start any node with UI for debugging:

```bash
docker compose -p blah2-node1 \
  -f docker-compose.multi-node.yml \
  -f compose/nodes/node1.yml --profile ui up -d
```

6. Scale to additional nodes by copying a node override and updating values:

- a unique compose project name (`-p blah2-nodeX`)
- a unique config file in `command` fields
- unique ports in that config file
- a unique save path mount
- optional unique UI port override (only if UI is enabled)

## Notes

- The current frontend scripts still assume API port 3000 in several places.
- For non-default API ports, update frontend/api stash modules to use config-driven API base.
- Device-level sharing constraints still apply; use separate SDR devices unless replay mode is used.

## Validation checklist

- Every node API reachable on configured port.
- No TCP bind failures in logs.
- 3lips can ingest all active node feeds concurrently.
