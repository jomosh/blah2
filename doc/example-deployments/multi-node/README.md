# Multi-node blah2 deployment (same host)

The primary deployment path is through a single-file workflow using `docker-compose.yml`.

## Goals

1. Run 1+ independent blah2 nodes on one host.
2. Keep UI optional so nodes can run headless.
3. Preserve API payload compatibility for 3lips.

## Files

- Main compose file: `docker-compose.yml`
- Base configs:
  - `config/config.yml` (RspDuo/default)
  - `config/config-usrp.yml`
  - `config/config-hackrf.yml`
  - `config/config-kraken.yml`

## Setup

1. Start node1 with a single command:

```bash
sudo docker compose up -d --build
```

2. UI on/off toggle is controlled directly in `docker-compose.yml`:

- UI ON: keep the `blah2_web_node1` service block.
- UI OFF (headless): comment out the `blah2_web_node1` service block.

3. Enable an additional node from the commented template inside `docker-compose.yml`:

- Uncomment `blah2_node2` and `blah2_api_node2`.
- Optional UI: uncomment `blah2_web_node2`.
- Create node2 config from a template:

```bash
cp config/config.yml config/config-node2.yml
```

4. Edit each enabled node config:

- Ensure every node has a unique `network.ports` block.
- Recommended offset pattern per node index `n` (starting at 0):
  - API: `3000 + 100n`
  - map/detection/track/adsb: `3001-3004 + 100n`
  - timestamp/timing/iqdata/config: `4000-4003 + 100n`

5. Recreate the stack after enabling node2 or changing configs:

```bash
sudo docker compose up -d --build --force-recreate
```

6. Scale to additional nodes by copying the commented service template blocks and updating values:

- unique service names and container names
- a unique config file in `command` fields
- unique ports in that config file
- a unique save path mount
- a unique UI host-port mapping (only if UI is enabled)

## Notes

- Frontend pages support API targeting via query parameters:
  - `?api_port=<port>` (example: `?api_port=3100`)
  - `?api_base=//<host>:<port>` (example: `?api_base=//localhost:3100`)
- Controller behavior follows the same API targeting rules:
  - API/Stash links on `/controller` are rewritten using the active `api_port`/`api_base` values.
  - IQ Capture buttons on the detection map pages call the API using the same resolved base URL.
- Example node2 UI URLs:
  - `http://localhost:49153/?api_port=3100`
  - `http://localhost:49153/controller?api_port=3100`
- If neither parameter is provided, frontend defaults to `:3000` on localhost and same-origin host elsewhere.
- Device-level sharing constraints still apply; use separate SDR devices unless replay mode is used.

## Validation checklist

- Every node API reachable on configured port.
- No TCP bind failures in logs.
- 3lips can ingest all active node feeds concurrently.
