
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
- [ ] A feature to turn off the UI (when blah2 nodes are purely being used as feeders for 3lips)

## Installation Architecture

- [ ] Refactor code so multiple blah2 nodes can run on the same host (currently there's a conflict between other shared docker images)