// Alignment page — antenna alignment visualizations
// Consumes /stash/iqdata with per-CPI spectrumSurv, iqRef, iqSurv fields

var timestamp = -1;
var urlTimestamp = build_api_url('/api/timestamp');
var urlStash = build_api_url('/stash/iqdata');

// --- Peak-hold accumulators (client-side) ---
var peakholdRef = null;
var peakholdSurv = null;
var peakholdFreq = null;

// --- Waterfall buffers ---
var MAX_WATERFALL_ROWS = 60;
var waterfallRefZ = [];
var waterfallSurvZ = [];
var waterfallFreq = [];
var waterfallTimestamps = [];
var waterfallInitialized = false;

// --- Common Plotly config ---
var plotConfig = {
  responsive: true,
  displayModeBar: false
};

var darkLayout = {
  plot_bgcolor: 'rgba(0,0,0,0)',
  paper_bgcolor: 'rgba(0,0,0,0)',
  font: { color: '#aaa', size: 11 },
  margin: { l: 48, r: 16, b: 36, t: 8 }
};

// =============================================
// 1. Spectrum Overlay Plot
// =============================================
function initSpectrumOverlay() {
  var traceRef = {
    x: [],
    y: [],
    type: 'scatter',
    mode: 'lines',
    name: 'Reference',
    line: { color: '#4fc3f7', width: 1.5 }
  };
  var traceSurv = {
    x: [],
    y: [],
    type: 'scatter',
    mode: 'lines',
    name: 'Surveillance',
    line: { color: '#ff8a65', width: 1.5 }
  };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Frequency (kHz)', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'Amplitude (dB)', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333' },
    showlegend: true,
    legend: { x: 1, y: 1, font: { color: '#aaa', size: 10 }, bgcolor: 'rgba(0,0,0,0.4)' }
  });
  Plotly.newPlot('plot-spectrum-overlay', [traceRef, traceSurv], layout, plotConfig);
}

// =============================================
// 2. Peak-Hold Plot
// =============================================
function initPeakhold() {
  var traceRef = {
    x: [],
    y: [],
    type: 'scatter',
    mode: 'lines',
    name: 'Ref Peak',
    line: { color: '#4fc3f7', width: 1.5 }
  };
  var traceSurv = {
    x: [],
    y: [],
    type: 'scatter',
    mode: 'lines',
    name: 'Surv Peak',
    line: { color: '#ff8a65', width: 1.5 }
  };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Frequency (kHz)', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'Amplitude (dB)', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333' },
    showlegend: true,
    legend: { x: 1, y: 1, font: { color: '#aaa', size: 10 }, bgcolor: 'rgba(0,0,0,0.4)' }
  });
  Plotly.newPlot('plot-peakhold', [traceRef, traceSurv], layout, plotConfig);

  $('#btn-reset-peakhold').on('click', function() {
    peakholdRef = null;
    peakholdSurv = null;
    peakholdFreq = null;
    Plotly.deleteTraces('plot-peakhold', [0, 1]);
    Plotly.addTraces('plot-peakhold', [
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref Peak', line: { color: '#4fc3f7', width: 1.5 } },
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv Peak', line: { color: '#ff8a65', width: 1.5 } }
    ]);
  });
}

// =============================================
// 3. Constellation (I/Q scatter) Plots
// =============================================
function initConstellation() {
  // Ref constellation
  var traceRef = {
    x: [],
    y: [],
    type: 'scattergl',
    mode: 'markers',
    name: 'Ref',
    marker: { color: '#4fc3f7', size: 3, opacity: 0.6 }
  };
  var layoutRef = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'I', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333', zerolinecolor: '#555' },
    yaxis: { title: { text: 'Q', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333', zerolinecolor: '#555', scaleanchor: 'x', scaleratio: 1 },
    showlegend: false
  });
  Plotly.newPlot('plot-const-ref', [traceRef], layoutRef, plotConfig);

  // Surv constellation
  var traceSurv = {
    x: [],
    y: [],
    type: 'scattergl',
    mode: 'markers',
    name: 'Surv',
    marker: { color: '#ff8a65', size: 3, opacity: 0.6 }
  };
  var layoutSurv = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'I', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333', zerolinecolor: '#555' },
    yaxis: { title: { text: 'Q', font: { size: 11 } }, ticks: '', color: '#aaa', gridcolor: '#333', zerolinecolor: '#555', scaleanchor: 'x', scaleratio: 1 },
    showlegend: false
  });
  Plotly.newPlot('plot-const-surv', [traceSurv], layoutSurv, plotConfig);
}

// =============================================
// 4. Waterfall Heatmaps
// =============================================
function initWaterfalls() {
  var dummyZ = [[0]];
  var traceRef = {
    z: dummyZ,
    type: 'heatmap',
    colorscale: 'Jet',
    showscale: false
  };
  var layoutRef = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Freq (kHz)', font: { size: 11 } }, ticks: '', color: '#aaa' },
    yaxis: { title: { text: 'Time', font: { size: 11 } }, ticks: '', color: '#aaa' }
  });
  Plotly.newPlot('plot-waterfall-ref', [traceRef], layoutRef, plotConfig);

  var traceSurv = {
    z: dummyZ,
    type: 'heatmap',
    colorscale: 'Jet',
    showscale: false
  };
  var layoutSurv = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Freq (kHz)', font: { size: 11 } }, ticks: '', color: '#aaa' },
    yaxis: { title: { text: 'Time', font: { size: 11 } }, ticks: '', color: '#aaa' }
  });
  Plotly.newPlot('plot-waterfall-surv', [traceSurv], layoutSurv, plotConfig);
}

// =============================================
// Helpers
// =============================================
function computeBandPower(spectrumDb) {
  if (!spectrumDb || spectrumDb.length === 0) return null;
  // Convert dB back to linear, average, then back to dB
  var sumLin = 0;
  var validCount = 0;
  for (var i = 0; i < spectrumDb.length; i++) {
    var v = spectrumDb[i];
    if (isFinite(v)) {
      sumLin += Math.pow(10, v / 10);
      validCount++;
    }
  }
  if (validCount === 0) return null;
  var avgLin = sumLin / validCount;
  if (avgLin <= 0) return null;
  return 10 * Math.log10(avgLin);
}

function updateGauges(refSpectrum, survSpectrum) {
  var refPwr = computeBandPower(refSpectrum);
  var survPwr = computeBandPower(survSpectrum);

  if (refPwr !== null) {
    $('#gauge-ref').text(refPwr.toFixed(1) + ' dB');
  } else {
    $('#gauge-ref').text('-- dB');
  }

  if (survPwr !== null) {
    $('#gauge-surv').text(survPwr.toFixed(1) + ' dB');
  } else {
    $('#gauge-surv').text('-- dB');
  }

  if (refPwr !== null && survPwr !== null) {
    var isolation = refPwr - survPwr;
    $('#gauge-isolation').text(isolation.toFixed(1) + ' dB');
    var box = $('#gauge-isolation-box');
    box.removeClass('warn bad');
    if (isolation < 20) {
      box.addClass('bad');
    } else if (isolation < 30) {
      box.addClass('warn');
    }
  } else {
    $('#gauge-isolation').text('-- dB');
  }
}

function updatePeakhold(freq, refSpectrum, survSpectrum) {
  if (!freq || freq.length === 0) return;
  // Initialize peak-hold on first call
  if (peakholdRef === null) {
    peakholdRef = refSpectrum.slice();
    peakholdSurv = survSpectrum.slice();
    peakholdFreq = freq.slice();
  } else {
    // Only update if frequency axes match
    if (freq.length === peakholdFreq.length) {
      for (var i = 0; i < freq.length; i++) {
        if (refSpectrum[i] > peakholdRef[i]) peakholdRef[i] = refSpectrum[i];
        if (survSpectrum[i] > peakholdSurv[i]) peakholdSurv[i] = survSpectrum[i];
      }
    } else {
      // Frequency axis changed (e.g. config changed), reset
      peakholdRef = refSpectrum.slice();
      peakholdSurv = survSpectrum.slice();
      peakholdFreq = freq.slice();
    }
  }
  Plotly.restyle('plot-peakhold', {
    x: [peakholdFreq, peakholdFreq],
    y: [peakholdRef, peakholdSurv]
  });
}

function updateWaterfall(currentFreq, refSpectrum, survSpectrum) {
  if (!currentFreq || currentFreq.length === 0) return;
  if (!refSpectrum || !survSpectrum) return;

  // On first call or freq change, reset waterfalls
  if (!waterfallInitialized ||
      waterfallFreq.length === 0 ||
      waterfallFreq.length !== currentFreq.length) {
    waterfallFreq = currentFreq.slice();
    waterfallRefZ = [];
    waterfallSurvZ = [];
    waterfallTimestamps = [];
    waterfallInitialized = true;
  }

  waterfallRefZ.push(refSpectrum.slice());
  waterfallSurvZ.push(survSpectrum.slice());
  waterfallTimestamps.push(new Date());

  // Trim to max rows
  while (waterfallRefZ.length > MAX_WATERFALL_ROWS) {
    waterfallRefZ.shift();
    waterfallSurvZ.shift();
    waterfallTimestamps.shift();
  }

  var timeLabels = [];
  for (var i = 0; i < waterfallTimestamps.length; i++) {
    var t = waterfallTimestamps[i];
    timeLabels.push(t.getHours().toString().padStart(2, '0') + ':' +
                    t.getMinutes().toString().padStart(2, '0') + ':' +
                    t.getSeconds().toString().padStart(2, '0') + '.' +
                    Math.floor(t.getMilliseconds() / 100).toString());
  }

  var layoutUpdate = {
    'xaxis.range': [waterfallFreq[0], waterfallFreq[waterfallFreq.length - 1]]
  };

  Plotly.update('plot-waterfall-ref', {
    z: [waterfallRefZ],
    x: [waterfallFreq],
    y: [timeLabels]
  }, layoutUpdate);

  Plotly.update('plot-waterfall-surv', {
    z: [waterfallSurvZ],
    x: [waterfallFreq],
    y: [timeLabels]
  }, Object.assign({}, layoutUpdate));
}

function updateConstellation(iqRef, iqSurv) {
  if (!iqRef || iqRef.length < 2) return;
  if (!iqSurv || iqSurv.length < 2) return;

  // iqRef / iqSurv are flat arrays: [I0, Q0, I1, Q1, ...]
  var xRef = [], yRef = [];
  for (var i = 0; i < iqRef.length; i += 2) {
    xRef.push(iqRef[i]);
    yRef.push(iqRef[i + 1]);
  }

  var xSurv = [], ySurv = [];
  for (var i = 0; i < iqSurv.length; i += 2) {
    xSurv.push(iqSurv[i]);
    ySurv.push(iqSurv[i + 1]);
  }

  Plotly.restyle('plot-const-ref', { x: [xRef], y: [yRef] });
  Plotly.restyle('plot-const-surv', { x: [xSurv], y: [ySurv] });
}

// =============================================
// Main poll loop
// =============================================
function initAllPlots() {
  initSpectrumOverlay();
  initPeakhold();
  initConstellation();
  initWaterfalls();
}

$(document).ready(function() {
  initAllPlots();

  window.setInterval(function() {
    $.get(urlTimestamp)
      .done(function(ts) {
        if (timestamp !== ts) {
          timestamp = ts;
          $.getJSON(urlStash)
            .done(function(data) {
              // Get the latest spectrum row (last in the buffered arrays)
              var refSpectrum = null;
              var survSpectrum = null;
              var freqBins = null;

              // The stash buffers spectrum and spectrumSurv as arrays of arrays (history)
              // The most recent CPI is the last element
              if (Array.isArray(data.spectrum) && data.spectrum.length > 0) {
                var lastIdx = data.spectrum.length - 1;
                refSpectrum = data.spectrum[lastIdx];
              }
              if (Array.isArray(data.spectrumSurv) && data.spectrumSurv.length > 0) {
                var lastIdx = data.spectrumSurv.length - 1;
                survSpectrum = data.spectrumSurv[lastIdx];
              }
              if (Array.isArray(data.frequency) && data.frequency.length > 0) {
                var lastIdx = data.frequency.length - 1;
                freqBins = data.frequency[lastIdx];
              }

              // Update spectrum overlay (most recent per-CPI values)
              if (refSpectrum && survSpectrum && freqBins) {
                Plotly.restyle('plot-spectrum-overlay', {
                  x: [freqBins, freqBins],
                  y: [refSpectrum, survSpectrum]
                });

                // Update gauges
                updateGauges(refSpectrum, survSpectrum);

                // Update peak-hold
                updatePeakhold(freqBins, refSpectrum, survSpectrum);

                // Update waterfalls
                updateWaterfall(freqBins, refSpectrum, survSpectrum);
              }

              // Update constellation (per-CPI IQ samples)
              if (data.iqRef && data.iqSurv) {
                updateConstellation(data.iqRef, data.iqSurv);
              }
            });
        }
      });
  }, 100);
});