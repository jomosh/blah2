
# blah2 TODO

## Capture & DSR

- [X] Expand SDR device support (UHD integration, additional hardware)
- [X] Add replay/file-based capture modes
- [ ] Add 2x RTL-SDR sample-time alignment for shared-clock setups without shared trigger (use a noise source or equivalent startup calibration to establish a common trigger after device start)
- [ ] Optimize IQ sample streaming performance

## DSP & Processing

- [ ] Enhance clutter filtering algorithms
- [ ] Improve detection/tracking robustness
- [ ] Optimize ambiguity map computation
- [X] Add spectrum analysis features

## Data & Serialization

- [ ] Review JSON schema stability for API contract
- [X] Add data persistence/logging features
- [ ] Improve metadata handling and timestamps

## API Layer (Node.js)

- [X] Expand REST endpoint coverage
- [X] Implement error handling/logging
- [ ] Add configuration hot-reload support
- [ ] Performance optimize TCP socket handling

## Frontend UI

- [ ] Enhance Plotly visualizations
- [ ] Add real-time plot refresh optimization
- [ ] Implement user controls for filtering/scaling
- [ ] Improve responsive layout

## Testing

- [ ] Expand Catch2 unit test coverage
- [ ] Add integration tests for end-to-end flows
- [ ] Improve test fixtures and determinism

## Configuration

- [ ] Document YAML schema and defaults
- [ ] Add configuration validation
- [ ] Support environment variable overrides

## Documentation

- [ ] Add architecture documentation
- [ ] Create API endpoint reference
- [X] Document deployment steps

## Performance & Real-Time

- [ ] Profile hot paths in DSP pipeline
- [ ] Optimize memory allocations in tight loops
- [ ] Benchmark network payload generation
- [X] A feature to turn off the UI when running blah2 nodes for 3lips ingestion

## Installation Architecture

- [X] Refactor code so multiple blah2 nodes can run on the same host (currently there's a conflict between other shared docker images)

## 3lips Localisation Readiness (Q2-Q3 2026)

### Q2 Milestones

- [X] Add headless node mode profile (run processor + API without web UI)
- [X] Remove fixed Docker container names to allow multi-instance deployment
- [X] Parameterize API/web port bindings per node instance
- [X] Replace hardcoded localhost:3000 assumptions in stash modules
- [X] Add null-safe handling for `/capture` polling in capture thread

### Q3 Milestones

- [X] Validate 2x node deployment on one host with independent configs and ports
- [ ] Add startup preflight checks for occupied ports and SDR device lock conflicts
- [ ] Add observability endpoints for node health and data freshness
- [ ] Document multi-node deployment and 3lips integration patterns

### Acceptance Gates

- [ ] Gate A: Headless node mode runs 24h without UI containers and without API schema changes
- [ ] Gate B: Two nodes run concurrently for 8h on one host with zero port bind/connect conflicts
- [ ] Gate C: Single-node default workflow remains backward compatible