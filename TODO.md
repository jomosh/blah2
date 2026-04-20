
# blah2 TODO

## Capture & DSR

- [ ] Expand SDR device support (UHD integration, additional hardware)
- [ ] Add replay/file-based capture modes
- [ ] Optimize IQ sample streaming performance

## DSP & Processing

- [ ] Enhance clutter filtering algorithms
- [ ] Improve detection/tracking robustness
- [ ] Optimize ambiguity map computation
- [ ] Add spectrum analysis features

## Data & Serialization

- [ ] Review JSON schema stability for API contract
- [ ] Add data persistence/logging features
- [ ] Improve metadata handling and timestamps

## API Layer (Node.js)

- [ ] Expand REST endpoint coverage
- [ ] Implement error handling/logging
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
- [ ] Document deployment steps

## Performance & Real-Time

- [ ] Profile hot paths in DSP pipeline
- [ ] Optimize memory allocations in tight loops
- [ ] Benchmark network payload generation
- [ ] A feature to turn off the UI when running blah2 nodes for 3lips ingestion

## Installation Architecture

- [ ] Refactor code so multiple blah2 nodes can run on the same host (currently there's a conflict between other shared docker images)

## 3lips Localisation Readiness (Q2-Q3 2026)

### Q2 Milestones

- [ ] Add headless node mode profile (run processor + API without web UI)
- [ ] Remove fixed Docker container names to allow multi-instance deployment
- [ ] Parameterize API/web port bindings per node instance
- [ ] Replace hardcoded localhost:3000 assumptions in stash modules
- [ ] Add null-safe handling for `/capture` polling in capture thread

### Q3 Milestones

- [ ] Validate 2x node deployment on one host with independent configs and ports
- [ ] Add startup preflight checks for occupied ports and SDR device lock conflicts
- [ ] Add observability endpoints for node health and data freshness
- [ ] Document multi-node deployment and 3lips integration patterns

### Acceptance Gates

- [ ] Gate A: Headless node mode runs 24h without UI containers and without API schema changes
- [ ] Gate B: Two nodes run concurrently for 8h on one host with zero port bind/connect conflicts
- [ ] Gate C: Single-node default workflow remains backward compatible