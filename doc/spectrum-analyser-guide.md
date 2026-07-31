# Spectrum Analyser — User Guide

The **Spectrum Analyser** page (Controller → Display → Spectrum Analyser) provides a
comprehensive set of real-time visualisations to help you align the reference and
surveillance antennas for optimal passive radar performance.

Access it at `/display/spectrum-analyser` from your blah2 web UI.

---

## What You're Looking At

The page shows the **frequency-domain view** of both the reference and surveillance
channels simultaneously. This is the same IQ data that drives the delay-Doppler
processing pipeline, so what you see here directly reflects detection quality.

**Key principle:** The reference antenna must capture the strongest possible
direct-path signal from the illuminator (e.g., FM radio tower). The surveillance
antenna must capture target echoes while seeing as little of the direct-path
signal as possible — this is called **isolation**.

---

## Panel-by-Panel Reference

### 1. Gauges (top row)

| Gauge | Unit | Good | Marginal | Poor |
|-------|------|------|----------|------|
| **Ref Power** | dB (relative) | As high as possible | — | — |
| **Surv Power** | dB (relative) | As low as possible | — | — |
| **Isolation** | dB (Ref − Surv) | **>30 dB** (green) | 20–30 dB (yellow) | <20 dB (red) |
| **Correlation** | Pearson's r (−1 to +1) | **<0.4** (green) | 0.4–0.7 (yellow) | >0.7 (red) |

**How to use them:**
- **Isolation** is your primary alignment metric. Adjust the surveillance antenna
  direction/position until this number is as high as possible.
- **Correlation** measures how much the surveillance spectrum *shapes* like the
  reference. When they're independent (good isolation), correlation is near 0.
  High correlation means reference signal is bleeding into surveillance.
- These values are computed from the full frequency spectrum each CPI (~every 750 ms
  by default), so they update in real time as you physically move antennas.

---

### 2. Spectrum Overlay

Two overlaid line traces showing amplitude (dB) vs frequency (kHz) for the
most recent CPI.

| Colour | Channel |
|--------|---------|
| **Blue** | Reference |
| **Orange** | Surveillance |

**What to look for:**
- The **reference trace** should show a strong signal across your band of
  interest. For FM passive radar, you should see distinct carrier peaks.
- The **surveillance trace** should be significantly lower than the reference
  across all frequencies. If you see the surveillance trace following the
  same shape as reference (just attenuated), you have leakage.
- A flat, featureless surveillance trace that stays well below reference
  is the goal.

**Good alignment:**
```
Reference:  ▁▁▁█▁▁▁▁██▁▁▁█▁▁▁▁  (clear peaks, ~40-50 dB range)
Surv:       ▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁▁  (flat noise floor, ~20-30 dB below ref)
```

**Bad alignment (leakage):**
```
Reference:  ▁▁▁█▁▁▁▁██▁▁▁█▁▁▁▁
Surv:       ▁▁▁▄▁▁▁▁▄▄▁▁▁▄▁▁▁▁  (same shape, just attenuated — reference bleed)
```

---

### 3. Peak Hold

Accumulates the maximum value seen at each frequency bin since the last reset.
A **Reset** button clears the accumulators.

**How to use it:**
1. Press **Reset** to clear previous peaks.
2. Slowly sweep the reference antenna direction across the expected illuminator
   azimuth.
3. Watch the blue trace build up — the strongest peak indicates the best
   reference antenna heading.
4. Lock the reference antenna at that heading.
5. Press **Reset** again, then sweep the surveillance antenna.
6. The orange trace should stay as low as possible while the reference trace
   climbs.

This is the most practical tool for **initial antenna pointing** — you can
see the effect of every physical adjustment without looking away from the
screen.

---

### 4. Differential Spectrum (Ref − Surv)

A green area plot showing the isolation value at **each individual frequency
bin**.

**How to interpret:**
- A **flat, high trace** across the band = uniform isolation at all frequencies.
- **Dips at specific frequencies** = those frequencies are leaking from reference
  into surveillance. This can happen due to:
  - Multipath reflections at specific wavelengths
  - Antenna pattern nulls at certain frequencies
  - Standing waves in the RF cabling
- A **negative value** at any bin means surveillance is actually *louder* than
  reference at that frequency — a strong indicator of a configuration or
  cabling problem.

This is the most diagnostic single view for identifying **frequency-specific
leakage** that might degrade detection at particular bistatic ranges or Dopplers.

---

### 5. Power Trend

A rolling time-series plot (last ~2 minutes) showing:

| Trace | Colour | Y-axis | Meaning |
|-------|--------|--------|---------|
| Ref Power | Blue | Left (Power dB) | Total reference band power over time |
| Surv Power | Orange | Left (Power dB) | Total surveillance band power over time |
| Isolation | Green | Right (Isolation dB) | Ref−Surv over time |

**How to use it:**
- Make a physical antenna adjustment, then watch the trend for 5–10 seconds.
- **Isolation going up** → you moved in the right direction.
- **Isolation going down** → move back.
- **Ref power dropping** → you may have moved the reference antenna off the
  illuminator.

Press **Reset Trend** to clear the history and start fresh (useful after a
major antenna repositioning).

---

### 6. Isolation Histogram

A histogram of every per-frequency-bin isolation value (Ref−Surv) accumulated
over time.

**How to interpret the shape:**
- **Tall, narrow peak shifted well to the right (>30 dB)** — excellent. Most
  frequency bins have good isolation, and isolation is consistent across the band.
- **Wide, spread-out distribution** — isolation varies significantly by frequency.
  Check the Differential Spectrum to identify which frequencies are weak.
- **Bimodal (two peaks)** — there may be two distinct types of signal behaviour
  in your band (e.g., one part of the band sees the illuminator well, another
  part doesn't).
- **Peak near or left of 0 dB** — serious leakage; surveillance is picking up
  almost as much as reference.

The histogram complements the Differential Spectrum: the spectrum shows *where*
in frequency the problem is; the histogram shows *how much* of your band is
affected.

---

### 7. IQ Scatter (Ref & Surv)

Two scatter plots showing raw decimated I/Q samples (in-phase vs quadrature)
for each channel — updated per CPI.

**What to look for:**
- **Reference IQ Scatter** — typically a roughly circular cloud spread out
  from the origin. The larger the spread, the stronger the signal. For FM
  radio illuminators, this is an analog constant-envelope signal, so you'll
  see a donut/ring shape at strong signal levels.
- **Surveillance IQ Scatter** — should be a **tight, compact cloud** near the
  origin. Significantly smaller spread than the reference.
- **If the surveillance cloud looks similar in size to the reference cloud** —
  strong indication of reference signal bleed-through. The surveillance antenna
  is seeing the direct-path signal nearly as well as the reference antenna.

> **Note:** These are raw, un-demodulated IQ samples — not symbol constellations.
> You won't see QPSK/16QAM symbol clusters because passive radar illuminators
> (FM, DAB, DVB-T) use analog or spread-spectrum modulations.

---

### 8. Waterfalls (Ref & Surv)

Side-by-side heatmaps showing spectrum amplitude (colour = dB) over time
(Y-axis = time, X-axis = frequency). Approximately 60 seconds of history
(~60 CPI rows).

**How to use them:**
- **Stable, consistent patterns** — your RF environment is steady. Good.
- **Horizontal stripes appearing/disappearing** — intermittent interference
  or a transmitter turning on/off.
- **Vertical features that shift in frequency** — a moving transmitter or
  drifting oscillator.
- **The surveillance waterfall should be uniformly darker (lower amplitude)
  than the reference waterfall.** If the surveillance waterfall shows the same
  bright patterns as the reference, you have poor isolation.

The waterfalls are best for diagnosing **intermittent problems** — interference
that comes and goes, or environmental changes over minutes.

---

## Step-by-Step Alignment Procedure

### Initial setup

1. Point the **reference antenna** roughly toward the known illuminator
   (e.g., FM broadcast tower).
2. Point the **surveillance antenna** roughly toward the area you want to
   monitor for targets.
3. Open the Spectrum Analyser page.

### Tune the reference antenna

4. Press **Reset** on the Peak Hold panel.
5. Slowly sweep the reference antenna horizontally across the expected
   illuminator bearing.
6. Watch the **Peak Hold** blue (Ref) trace. Stop at the angle that produces
   the highest peaks.
7. Check the **Ref Power** gauge — it should be as high as possible.
8. Also check the **Spectrum Overlay** — you should see clear signal peaks
   above the noise floor.

### Tune the surveillance antenna

9. Press **Reset** on the Peak Hold panel again.
10. Slowly sweep the surveillance antenna horizontally.
11. Watch the **Isolation** gauge and **Power Trend** — find the angle that
    maximises isolation.
12. Watch the **Differential Spectrum** — aim for the flattest, highest trace
    across the band.
13. The **Correlation** gauge should fall toward 0 as isolation improves.
14. Check the **Surveillance IQ Scatter** — it should be a tight cluster,
    much smaller than the reference scatter.

### Verify

15. Check the **Isolation Histogram** — it should show a narrow peak shifted
    well to the right (>25–30 dB).
16. Check the **Waterfalls** — the surveillance waterfall should be uniformly
    darker than the reference.
17. Walk away and come back in 5 minutes — look at the **Power Trend** to
    confirm isolation is stable over time.

### On-going monitoring

18. Periodically check the **Correlation** gauge. If it drifts up over hours/days,
    environmental changes (wind moving antennas, vegetation growth, new
    structures) may be affecting alignment.
19. If detections degrade, return to this page first before changing any
    processing parameters (CFAR thresholds, clutter filter range, etc.).

---

## Troubleshooting Common Problems

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Isolation < 10 dB | Surveillance antenna seeing direct-path signal | Re-point surveillance antenna away from illuminator; add RF shielding/absorber between antennas |
| Correlation > 0.8 | Strong reference bleed into surveillance | Same as above; check cabling for cross-talk |
| Ref Power very low | Reference antenna not pointed at illuminator, or wrong frequency configured | Check `capture.fc` in config; verify illuminator is active at that frequency |
| Differential Spectrum has deep dips at specific frequencies | Frequency-selective multipath or antenna resonance | Try small antenna position changes (wavelength-scale); check cable quality |
| Surveillance waterfall shows same patterns as reference | Poor isolation across the band | Re-aim surveillance antenna; increase physical separation between antennas |
| IQ Scatter clouds are same size | Reference signal dominating both channels | Check SDR channel mapping in config; verify reference antenna is on correct SDR input |
| All gauges show "--" | Pipeline not running or no data arriving | Check that the blah2 processor is running; verify `/api/timestamp` returns changing values |

---

## Technical Notes

- All values are in **relative dB** (uncalibrated) — the absolute numbers
  depend on SDR gain settings, antenna gain, and cable losses. Only
  *differences* (isolation) are meaningful across setups.
- Spectrum resolution depends on `process.data.cpi` and `capture.fs` in
  your config file. A typical configuration produces ~1000–2000 frequency bins.
- The IQ Scatter plots show decimated samples (~2000 points per CPI) for
  performance. They represent the raw complex sample distribution, not
  demodulated symbols.
- Data refreshes at approximately 100 ms intervals from the stash layer,
  but the underlying CPI rate (typically 0.75–1.5 seconds) determines how
  often the spectrum actually changes.