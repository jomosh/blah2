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
- Base configs:
  - `config/config.yml` (RspDuo/default)
  - `config/config-usrp.yml`
  - `config/config-hackrf.yml`
  - `config/config-kraken.yml`

## Setup

1. Create one config file per node from the template:

```bash
cp config/config.yml config/config-node1.yml
cp config/config.yml config/config-node2.yml
```

2. Update node override files under `compose/nodes/` so each node points to the intended config file, save path, and UI host port.

- Set a unique `blah2_web.ports` mapping in each node override when UI is enabled (examples: node1 `49152:80`, node2 `49153:80`).

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
  -f compose/nodes/node1.yml up -d --build

docker compose -p blah2-node2 \
  -f docker-compose.multi-node.yml \
  -f compose/nodes/node2.yml up -d --build
```

5. Start any node with UI for debugging:

```bash
docker compose -p blah2-node1 \
  -f docker-compose.multi-node.yml \
  -f compose/nodes/node1.yml --profile ui up -d --build
```

6. Scale to additional nodes by copying a node override and updating values:

- a unique compose project name (`-p blah2-nodeX`)
- a unique config file in `command` fields
- unique ports in that config file
- a unique save path mount
- a unique `blah2_web.ports` mapping (only if UI is enabled)

## Notes

- Frontend pages support API targeting via query parameters:
  - `?api_port=<port>` (example: `?api_port=3100`)
  - `?api_base=//<host>:<port>` (example: `?api_base=//localhost:3100`)
- If neither parameter is provided, frontend defaults to `:3000` on localhost and same-origin host elsewhere.
- Device-level sharing constraints still apply; use separate SDR devices unless replay mode is used.

## Validation checklist

- Every node API reachable on configured port.
- No TCP bind failures in logs.
- 3lips can ingest all active node feeds concurrently.
