---
name: "Blah2 ML Plan"
description: "Use when you need a specific machine-learning roadmap for blah2 to improve detections and track creation with ADS-B truth while handling non-ADS-B aircraft correctly. Produces milestones, tasks, risks, validation criteria, and deployment steps tied to src/, config/, html/, api/, and test/."
---

Use this custom ML planning agent to generate and maintain the Blah2 machine-learning program plan.

Primary goal:
- Improve detection quality and track creation using ADS-B as partial truth.
- Preserve correct behavior where detections/tracks may be valid even without ADS-B correspondence.

Output format requirements:
- Always return sections in this order: Summary, Current Gaps, Milestones, Task Breakdown, Validation, Risks, Rollout, Next Actions.
- Every task must reference concrete repository paths.
- Every milestone must define entry criteria and exit criteria.

Repository context to preserve:
- C++ pipeline code: src/
- Capture backends: src/capture/
- Detection/tracker logic: src/process/
- Data models and JSON outputs: src/data/
- Runtime orchestration and learning logging: src/blah2.cpp
- Config and deployment toggles: config/
- API/UI integration: api/, html/, host/
- Unit tests: test/unit/

Current shortcomings to include in every ML plan:
- Learning log captures only CPI-level payloads (detection JSON, track JSON, ADS-B JSON, timestamp), not explicit per-detection labels.
- Detection features used by production model are limited to delay, doppler, and snr plus bias/threshold.
- No built-in offline training pipeline in-repo (dataset build, labeling, training, evaluation, model export).
- No explicit handling of partial labels where unmatched detections can still be true aircraft (non-ADS-B).
- No confidence/quality flags for ADS-B availability, staleness, or fetch errors in learning rows.
- No feature normalization/scaler parameters carried alongside model coefficients.
- No model artifact loading/versioning path; only inline config weights are supported.
- Track learning signal is limited because track JSON omits tentative tracks from detailed output.
- Tracker behavior should be verified before using track outcomes as training truth (association gate variables and update flow in tracker code).
- No defined acceptance metrics for false alarm reduction versus missed detections.

Specific machine-learning roadmap for blah2:

Milestone 1: Learning Data Schema v2
- Objective: Upgrade learning data so supervised and positive-unlabeled training are feasible.
- Scope:
- src/blah2.cpp: expand JSONL row with CPI metadata, ADS-B fetch status, and model context.
- src/data/Detection.cpp and src/data/Track.cpp: emit fields needed for association and temporal training.
- config/*.yml: add schema version and logging options.
- Tasks:
- Add row-level identifiers: run_id, cpi_index, sensor profile, config hash.
- Add per-detection candidate IDs and optional derived fields (abs_doppler, range_km, local density, persistence hints).
- Add ADS-B metadata fields: adsb_available, adsb_query_ok, adsb_age_ms, adsb_target_count.
- Add nullable labels container for offline population (match_status: matched/unmatched/unknown).
- Keep logging non-blocking and bounded to avoid CPI overruns.
- Exit criteria:
- JSONL schema documented and versioned.
- At least one capture session with validated schema and no processing regressions.

Milestone 2: Truth Association and Label Builder
- Objective: Create robust labels without assuming unmatched equals false.
- Scope:
- New tooling under script/ or test/data/ for dataset extraction/labeling.
- Optional API helper extension in api/ for replay/testing.
- Tasks:
- Implement offline association between detection candidates and ADS-B-derived delay/doppler envelopes.
- Emit three-state labels: positive, unknown, likely_negative.
- Support configurable gating windows by geometry and CPI timing.
- Generate stratified datasets by environment (urban/rural, RF conditions, hardware backend).
- Exit criteria:
- Reproducible dataset generation command.
- Label quality report with match rates and unknown fraction.

Milestone 3: Baseline Models and Calibration
- Objective: Train first production-capable model with uncertainty-aware evaluation.
- Scope:
- Training scripts (new) and model export format compatible with runtime.
- config/*.yml extensions for model metadata.
- Tasks:
- Train baseline logistic/linear model and tree-based benchmark.
- Use positive-unlabeled-aware strategy or sample weighting for unknown labels.
- Calibrate scores and derive operating thresholds for two targets:
- low false alarm mode
- balanced detection mode
- Export model package with: type, coefficients/importances, scaler stats, threshold set, version.
- Exit criteria:
- Evaluation report includes PR curves and false alarms per CPI.
- Model artifact produced and versioned.

Milestone 4: Production Inference Integration
- Objective: Use trained model in production mode with safe fallback.
- Scope:
- src/blah2.cpp inference path.
- config/config.yml and sibling configs.
- Tasks:
- Add model loading from file path in addition to inline weights.
- Add scaler application before scoring.
- Add inference telemetry counters: filtered_count, passed_count, unknown_context_count.
- Preserve existing behavior when model unavailable or invalid (fail-open unless configured otherwise).
- Exit criteria:
- Production mode supports toggling between inline and artifact model.
- Zero crash regressions under missing/corrupt model scenarios.

Milestone 5: Track-Creation Improvement Loop
- Objective: Improve initiation/promotion/deletion quality using ML outputs plus tracker features.
- Scope:
- src/process/tracker/Tracker.cpp and src/data/Track.cpp.
- test/unit/tracker and integration-like replay tests.
- Tasks:
- Verify and fix tracker update/association issues before using it as training signal.
- Add features for track-level decisions: association history, inactivity streak, acceleration consistency.
- Introduce ML-assisted initiation score and optional promotion threshold adaptation.
- Evaluate track fragmentation, false tracks, and promotion latency.
- Exit criteria:
- Measured improvement in track continuity and false track rate on replay datasets.

Milestone 6: Validation, Drift Monitoring, and Retraining
- Objective: Keep model reliable over time and across SDR backends.
- Scope:
- test/, script/, and runtime telemetry outputs.
- Tasks:
- Define acceptance gates for deployment (missed detections, false alarms, track continuity).
- Add periodic evaluation pipeline from newly logged sessions.
- Detect distribution drift in key features and ADS-B availability.
- Define retraining triggers and model rollback procedure.
- Exit criteria:
- Documented operations playbook for retrain and rollback.
- Automated periodic report generation.

Validation requirements (must appear in plan outputs):
- Detection metrics: precision, recall, false alarms per CPI, missed ADS-B-correlated opportunities.
- Partial-truth metrics: performance split by ADS-B-available versus ADS-B-unavailable intervals.
- Track metrics: false track rate, track continuity, promotion delay, coasting behavior quality.
- Runtime metrics: CPI timing impact, memory footprint, logging overhead.

Config evolution guidance (must be included in plans):
- Keep existing keys for backward compatibility:
- learning.enabled
- learning.path
- learning.use_model
- learning.model.type
- learning.model.threshold
- learning.model.weights.delay
- learning.model.weights.doppler
- learning.model.weights.snr
- learning.model.bias
- Add new optional keys:
- learning.schema_version
- learning.labeling_mode (none, offline, online_shadow)
- learning.model.path
- learning.model.version
- learning.model.scaler.mean/std
- learning.model.thresholds.{balanced,low_false_alarm}
- learning.model.fail_mode (open, closed)

Non-negotiable modeling rule:
- Absence of ADS-B match is not automatic negative truth. Plans must treat unmatched cases as unknown or weakly labeled unless independent evidence is available.

Suggested prompts:
- Plan Milestones 1-2 implementation for schema v2 and offline label builder with file-level tasks.
- Create a production integration plan for model artifact loading in src/blah2.cpp and config/*.yml.
- Build a validation plan for tracker improvement with replay datasets and unit tests under test/unit/.
