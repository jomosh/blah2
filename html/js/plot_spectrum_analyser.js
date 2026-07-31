// Spectrum Analyser — dual-channel spectrum visualizations for antenna alignment
// Consumes /stash/iqdata with spectrumSurv, iqRef, iqSurv fields

var timestamp = -1;
var urlTimestamp = build_api_url('/api/timestamp');
var urlStash = build_api_url('/stash/iqdata');

var peakholdRef = null;
var peakholdSurv = null;
var peakholdFreq = null;

var MAX_WATERFALL_ROWS = 60;
var waterfallRefZ = [];
var waterfallSurvZ = [];
var waterfallFreq = [];
var waterfallTimestamps = [];
var waterfallInitialized = false;

// Power trend buffers
var MAX_TREND_POINTS = 120;
var trendTime = [];
var trendRefPwr = [];
var trendSurvPwr = [];
var trendIsolation = [];
var trendCorrelation = [];

// Isolation histogram bins
var HIST_BINS = 50;
var histValues = [];

var plotConfig = { responsive: true, displayModeBar: false };
var darkLayout = {
  plot_bgcolor: 'rgba(0,0,0,0)',
  paper_bgcolor: 'rgba(0,0,0,0)',
  font: { color: '#aaa', size: 10 },
  margin: { l: 42, r: 12, b: 30, t: 6 }
};

// =============================================
// 1. Spectrum Overlay Plot
// =============================================
function initSpectrumOverlay() {
  var traceRef = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref', line: { color: '#4fc3f7', width: 1.2 } };
  var traceSurv = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv', line: { color: '#ff8a65', width: 1.2 } };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Frequency (kHz)' }, color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'dB' }, color: '#aaa', gridcolor: '#333' },
    showlegend: true,
    legend: { x: 1, y: 1, font: { color: '#aaa', size: 9 }, bgcolor: 'rgba(0,0,0,0.4)' }
  });
  Plotly.newPlot('plot-spectrum-overlay', [traceRef, traceSurv], layout, plotConfig);
}

// =============================================
// 2. Peak-Hold Plot
// =============================================
function initPeakhold() {
  var traceRef = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref Peak', line: { color: '#4fc3f7', width: 1.2 } };
  var traceSurv = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv Peak', line: { color: '#ff8a65', width: 1.2 } };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Freq (kHz)' }, color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'dB' }, color: '#aaa', gridcolor: '#333' },
    showlegend: true,
    legend: { x: 1, y: 1, font: { color: '#aaa', size: 9 }, bgcolor: 'rgba(0,0,0,0.4)' }
  });
  Plotly.newPlot('plot-peakhold', [traceRef, traceSurv], layout, plotConfig);
  $('#btn-reset-peakhold').on('click', function() {
    peakholdRef = null; peakholdSurv = null; peakholdFreq = null;
    Plotly.deleteTraces('plot-peakhold', [0, 1]);
    Plotly.addTraces('plot-peakhold', [
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref Peak', line: { color: '#4fc3f7', width: 1.2 } },
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv Peak', line: { color: '#ff8a65', width: 1.2 } }
    ]);
  });
}

// =============================================
// 3. Differential Spectrum
// =============================================
function initDiffSpectrum() {
  var trace = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref−Surv', line: { color: '#a5d6a7', width: 1.2 }, fill: 'tozeroy', fillcolor: 'rgba(165,214,167,0.15)' };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Frequency (kHz)' }, color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'Isolation (dB)' }, color: '#aaa', gridcolor: '#333' },
    showlegend: false
  });
  Plotly.newPlot('plot-diff-spectrum', [trace], layout, plotConfig);
}

// =============================================
// 4. Power Trend
// =============================================
function initPowerTrend() {
  var traceRef = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref Pwr', line: { color: '#4fc3f7', width: 1.2 }, yaxis: 'y' };
  var traceSurv = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv Pwr', line: { color: '#ff8a65', width: 1.2 }, yaxis: 'y' };
  var traceIso = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Isolation', line: { color: '#81c784', width: 1.5 }, yaxis: 'y2' };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Time' }, color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'Power (dB)' }, color: '#4fc3f7', gridcolor: '#333' },
    yaxis2: { title: { text: 'Isolation (dB)' }, color: '#81c784', overlaying: 'y', side: 'right', gridcolor: 'rgba(0,0,0,0)' },
    showlegend: true,
    legend: { x: 0, y: 1, font: { color: '#aaa', size: 8 }, bgcolor: 'rgba(0,0,0,0.4)' }
  });
  Plotly.newPlot('plot-power-trend', [traceRef, traceSurv, traceIso], layout, plotConfig);

  $('#btn-reset-trend').on('click', function() {
    trendTime = []; trendRefPwr = []; trendSurvPwr = []; trendIsolation = []; trendCorrelation = [];
    Plotly.deleteTraces('plot-power-trend', [0, 1, 2]);
    Plotly.addTraces('plot-power-trend', [
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref Pwr', line: { color: '#4fc3f7', width: 1.2 }, yaxis: 'y' },
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv Pwr', line: { color: '#ff8a65', width: 1.2 }, yaxis: 'y' },
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Isolation', line: { color: '#81c784', width: 1.5 }, yaxis: 'y2' }
    ]);
  });
}

// =============================================
// 5. Isolation Histogram
// =============================================
function initIsolationHist() {
  var trace = { x: [], type: 'histogram', marker: { color: '#81c784' }, nbinsx: HIST_BINS };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Isolation per bin (dB)' }, color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'Count' }, color: '#aaa', gridcolor: '#333' },
    bargap: 0.05,
    showlegend: false
  });
  Plotly.newPlot('plot-isolation-hist', [trace], layout, plotConfig);
}

// =============================================
// 6. IQ Scatter Plots
// =============================================
function initIQScatter() {
  var layoutConst = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'I' }, color: '#aaa', gridcolor: '#333', zerolinecolor: '#555' },
    yaxis: { title: { text: 'Q' }, color: '#aaa', gridcolor: '#333', zerolinecolor: '#555', scaleanchor: 'x', scaleratio: 1 },
    showlegend: false
  });
  Plotly.newPlot('plot-iq-scatter-ref', [{ x: [], y: [], type: 'scattergl', mode: 'markers', marker: { color: '#4fc3f7', size: 2, opacity: 0.5 } }], layoutConst, plotConfig);
  Plotly.newPlot('plot-iq-scatter-surv', [{ x: [], y: [], type: 'scattergl', mode: 'markers', marker: { color: '#ff8a65', size: 2, opacity: 0.5 } }], layoutConst, plotConfig);
}

// =============================================
// 7. Waterfall Heatmaps
// =============================================
function initWaterfalls() {
  var dummyZ = [[0]];
  var layoutWf = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Freq (kHz)' }, color: '#aaa' },
    yaxis: { title: { text: 'Time' }, color: '#aaa' }
  });
  Plotly.newPlot('plot-waterfall-ref', [{ z: dummyZ, type: 'heatmap', colorscale: 'Jet', showscale: false }], layoutWf, plotConfig);
  Plotly.newPlot('plot-waterfall-surv', [{ z: dummyZ, type: 'heatmap', colorscale: 'Jet', showscale: false }], layoutWf, plotConfig);
}

// =============================================
// Helpers
// =============================================
function computeBandPower(spectrumDb) {
  if (!spectrumDb || spectrumDb.length === 0) return null;
  var sumLin = 0, validCount = 0;
  for (var i = 0; i < spectrumDb.length; i++) {
    var v = spectrumDb[i];
    if (isFinite(v)) { sumLin += Math.pow(10, v / 10); validCount++; }
  }
  if (validCount === 0) return null;
  var avgLin = sumLin / validCount;
  return avgLin > 0 ? 10 * Math.log10(avgLin) : null;
}

function pearsonCorrelation(x, y) {
  if (!x || !y || x.length !== y.length || x.length < 2) return null;
  var sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
  var n = 0;
  for (var i = 0; i < x.length; i++) {
    if (!isFinite(x[i]) || !isFinite(y[i])) continue;
    sumX += x[i]; sumY += y[i]; sumXY += x[i] * y[i]; sumX2 += x[i] * x[i]; sumY2 += y[i] * y[i];
    n++;
  }
  if (n < 2) return null;
  var num = n * sumXY - sumX * sumY;
  var den = Math.sqrt((n * sumX2 - sumX * sumX) * (n * sumY2 - sumY * sumY));
  return den === 0 ? 0 : num / den;
}

function updateGauges(refSpectrum, survSpectrum) {
  var refPwr = computeBandPower(refSpectrum);
  var survPwr = computeBandPower(survSpectrum);
  var corr = pearsonCorrelation(refSpectrum, survSpectrum);

  $('#gauge-ref').text(refPwr !== null ? refPwr.toFixed(1) + ' dB' : '-- dB');
  $('#gauge-surv').text(survPwr !== null ? survPwr.toFixed(1) + ' dB' : '-- dB');

  if (refPwr !== null && survPwr !== null) {
    var isolation = refPwr - survPwr;
    $('#gauge-isolation').text(isolation.toFixed(1) + ' dB');
    var boxIso = $('#gauge-isolation-box');
    boxIso.removeClass('warn bad');
    if (isolation < 20) boxIso.addClass('bad');
    else if (isolation < 30) boxIso.addClass('warn');
  } else {
    $('#gauge-isolation').text('-- dB');
  }

  if (corr !== null) {
    $('#gauge-corr').text(corr.toFixed(3));
    var boxCorr = $('#gauge-corr-box');
    boxCorr.removeClass('warn bad');
    if (corr > 0.7) boxCorr.addClass('bad');
    else if (corr > 0.4) boxCorr.addClass('warn');
  } else {
    $('#gauge-corr').text('--');
  }

  return { refPwr: refPwr, survPwr: survPwr, isolation: (refPwr !== null && survPwr !== null) ? refPwr - survPwr : null, correlation: corr };
}

function updatePeakhold(freq, ref, surv) {
  if (!freq || freq.length === 0) return;
  if (peakholdRef === null) {
    peakholdRef = ref.slice(); peakholdSurv = surv.slice(); peakholdFreq = freq.slice();
  } else if (freq.length === peakholdFreq.length) {
    for (var i = 0; i < freq.length; i++) {
      if (ref[i] > peakholdRef[i]) peakholdRef[i] = ref[i];
      if (surv[i] > peakholdSurv[i]) peakholdSurv[i] = surv[i];
    }
  } else {
    peakholdRef = ref.slice(); peakholdSurv = surv.slice(); peakholdFreq = freq.slice();
  }
  Plotly.restyle('plot-peakhold', { x: [peakholdFreq, peakholdFreq], y: [peakholdRef, peakholdSurv] });
}

function updateDiffSpectrum(freq, ref, surv) {
  if (!freq || freq.length === 0 || !ref || !surv) return;
  var diff = [];
  for (var i = 0; i < ref.length; i++) {
    diff.push(ref[i] - surv[i]);
  }
  Plotly.restyle('plot-diff-spectrum', { x: [freq], y: [diff] });
}

function updatePowerTrend(refPwr, survPwr, isolation, correlation) {
  if (refPwr === null) return;
  trendTime.push(new Date());
  trendRefPwr.push(refPwr);
  trendSurvPwr.push(survPwr);
  trendIsolation.push(isolation);
  trendCorrelation.push(correlation);
  while (trendTime.length > MAX_TREND_POINTS) { trendTime.shift(); trendRefPwr.shift(); trendSurvPwr.shift(); trendIsolation.shift(); trendCorrelation.shift(); }

  var labels = [];
  for (var i = 0; i < trendTime.length; i++) {
    labels.push(trendTime[i].getHours().toString().padStart(2, '0') + ':' + trendTime[i].getMinutes().toString().padStart(2, '0') + ':' + trendTime[i].getSeconds().toString().padStart(2, '0'));
  }
  Plotly.restyle('plot-power-trend', {
    x: [labels, labels, labels],
    y: [trendRefPwr, trendSurvPwr, trendIsolation]
  });
}

function updateIsolationHist(freq, ref, surv) {
  if (!freq || freq.length === 0 || !ref || !surv) return;
  for (var i = 0; i < ref.length; i++) {
    histValues.push(ref[i] - surv[i]);
  }
  while (histValues.length > 50000) { histValues.splice(0, histValues.length - 40000); }
  if (histValues.length > 0) {
    Plotly.restyle('plot-isolation-hist', { x: [histValues] });
  }
}

function updateWaterfall(freq, ref, surv) {
  if (!freq || freq.length === 0 || !ref || !surv) return;
  if (!waterfallInitialized || waterfallFreq.length !== freq.length) {
    waterfallFreq = freq.slice(); waterfallRefZ = []; waterfallSurvZ = []; waterfallTimestamps = []; waterfallInitialized = true;
  }
  waterfallRefZ.push(ref.slice()); waterfallSurvZ.push(surv.slice()); waterfallTimestamps.push(new Date());
  while (waterfallRefZ.length > MAX_WATERFALL_ROWS) { waterfallRefZ.shift(); waterfallSurvZ.shift(); waterfallTimestamps.shift(); }
  var timeLabels = [];
  for (var i = 0; i < waterfallTimestamps.length; i++) {
    var t = waterfallTimestamps[i];
    timeLabels.push(t.getHours().toString().padStart(2, '0') + ':' + t.getMinutes().toString().padStart(2, '0') + ':' + t.getSeconds().toString().padStart(2, '0') + '.' + Math.floor(t.getMilliseconds() / 100));
  }
  var lUpdate = { 'xaxis.range': [waterfallFreq[0], waterfallFreq[waterfallFreq.length - 1]] };
  Plotly.update('plot-waterfall-ref', { z: [waterfallRefZ], x: [waterfallFreq], y: [timeLabels] }, lUpdate);
  Plotly.update('plot-waterfall-surv', { z: [waterfallSurvZ], x: [waterfallFreq], y: [timeLabels] }, lUpdate);
}

function updateIQScatter(iqRef, iqSurv) {
  if (!iqRef || !iqSurv || iqRef.length < 2 || iqSurv.length < 2) return;
  if (iqRef.length % 2 !== 0 || iqSurv.length % 2 !== 0) return;
  var xRef = [], yRef = [], xSurv = [], ySurv = [];
  for (var i = 0; i < iqRef.length; i += 2) { xRef.push(iqRef[i]); yRef.push(iqRef[i + 1]); }
  for (var i = 0; i < iqSurv.length; i += 2) { xSurv.push(iqSurv[i]); ySurv.push(iqSurv[i + 1]); }
  Plotly.restyle('plot-iq-scatter-ref', { x: [xRef], y: [yRef] });
  Plotly.restyle('plot-iq-scatter-surv', { x: [xSurv], y: [ySurv] });
}

// =============================================
// Main poll loop
// =============================================
$(document).ready(function() {
  initSpectrumOverlay();
  initPeakhold();
  initDiffSpectrum();
  initPowerTrend();
  initIsolationHist();
  initIQScatter();
  initWaterfalls();

  window.setInterval(function() {
    $.get(urlTimestamp).done(function(ts) {
      if (timestamp !== ts) {
        timestamp = ts;
        $.getJSON(urlStash).done(function(data) {
          var refSpectrum = null, survSpectrum = null, freqBins = null;
          if (Array.isArray(data.spectrum) && data.spectrum.length > 0) refSpectrum = data.spectrum[data.spectrum.length - 1];
          if (Array.isArray(data.spectrumSurv) && data.spectrumSurv.length > 0) survSpectrum = data.spectrumSurv[data.spectrumSurv.length - 1];
          if (Array.isArray(data.frequency) && data.frequency.length > 0) freqBins = data.frequency[data.frequency.length - 1];

          if (refSpectrum && survSpectrum && freqBins) {
            Plotly.restyle('plot-spectrum-overlay', { x: [freqBins, freqBins], y: [refSpectrum, survSpectrum] });

            var metrics = updateGauges(refSpectrum, survSpectrum);
            updatePeakhold(freqBins, refSpectrum, survSpectrum);
            updateDiffSpectrum(freqBins, refSpectrum, survSpectrum);
            updatePowerTrend(metrics.refPwr, metrics.survPwr, metrics.isolation, metrics.correlation);
            updateIsolationHist(freqBins, refSpectrum, survSpectrum);
            updateWaterfall(freqBins, refSpectrum, survSpectrum);
          }

          if (data.iqRef && data.iqSurv) updateIQScatter(data.iqRef, data.iqSurv);
        });
      }
    });
  }, 100);
});
