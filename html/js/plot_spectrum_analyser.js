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

var plotConfig = { responsive: true, displayModeBar: false };
var darkLayout = {
  plot_bgcolor: 'rgba(0,0,0,0)',
  paper_bgcolor: 'rgba(0,0,0,0)',
  font: { color: '#aaa', size: 11 },
  margin: { l: 48, r: 16, b: 36, t: 8 }
};

// 1. Spectrum Overlay Plot
function initSpectrumOverlay() {
  var traceRef = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Reference', line: { color: '#4fc3f7', width: 1.5 } };
  var traceSurv = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surveillance', line: { color: '#ff8a65', width: 1.5 } };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Frequency (kHz)', font: { size: 11 } }, color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'Amplitude (dB)', font: { size: 11 } }, color: '#aaa', gridcolor: '#333' },
    showlegend: true,
    legend: { x: 1, y: 1, font: { color: '#aaa', size: 10 }, bgcolor: 'rgba(0,0,0,0.4)' }
  });
  Plotly.newPlot('plot-spectrum-overlay', [traceRef, traceSurv], layout, plotConfig);
}

// 2. Peak-Hold Plot
function initPeakhold() {
  var traceRef = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref Peak', line: { color: '#4fc3f7', width: 1.5 } };
  var traceSurv = { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv Peak', line: { color: '#ff8a65', width: 1.5 } };
  var layout = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Frequency (kHz)', font: { size: 11 } }, color: '#aaa', gridcolor: '#333' },
    yaxis: { title: { text: 'Amplitude (dB)', font: { size: 11 } }, color: '#aaa', gridcolor: '#333' },
    showlegend: true,
    legend: { x: 1, y: 1, font: { color: '#aaa', size: 10 }, bgcolor: 'rgba(0,0,0,0.4)' }
  });
  Plotly.newPlot('plot-peakhold', [traceRef, traceSurv], layout, plotConfig);
  $('#btn-reset-peakhold').on('click', function() {
    peakholdRef = null; peakholdSurv = null; peakholdFreq = null;
    Plotly.deleteTraces('plot-peakhold', [0, 1]);
    Plotly.addTraces('plot-peakhold', [
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Ref Peak', line: { color: '#4fc3f7', width: 1.5 } },
      { x: [], y: [], type: 'scatter', mode: 'lines', name: 'Surv Peak', line: { color: '#ff8a65', width: 1.5 } }
    ]);
  });
}

// 3. Constellation Plots
function initConstellation() {
  var layoutConst = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'I', font: { size: 11 } }, color: '#aaa', gridcolor: '#333', zerolinecolor: '#555' },
    yaxis: { title: { text: 'Q', font: { size: 11 } }, color: '#aaa', gridcolor: '#333', zerolinecolor: '#555', scaleanchor: 'x', scaleratio: 1 },
    showlegend: false
  });
  Plotly.newPlot('plot-const-ref', [{ x: [], y: [], type: 'scattergl', mode: 'markers', marker: { color: '#4fc3f7', size: 3, opacity: 0.6 } }], layoutConst, plotConfig);
  Plotly.newPlot('plot-const-surv', [{ x: [], y: [], type: 'scattergl', mode: 'markers', marker: { color: '#ff8a65', size: 3, opacity: 0.6 } }], layoutConst, plotConfig);
}

// 4. Waterfall Heatmaps
function initWaterfalls() {
  var dummyZ = [[0]];
  var layoutWf = Object.assign({}, darkLayout, {
    xaxis: { title: { text: 'Freq (kHz)', font: { size: 11 } }, color: '#aaa' },
    yaxis: { title: { text: 'Time', font: { size: 11 } }, color: '#aaa' }
  });
  Plotly.newPlot('plot-waterfall-ref', [{ z: dummyZ, type: 'heatmap', colorscale: 'Jet', showscale: false }], layoutWf, plotConfig);
  Plotly.newPlot('plot-waterfall-surv', [{ z: dummyZ, type: 'heatmap', colorscale: 'Jet', showscale: false }], layoutWf, plotConfig);
}

// Helpers
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

function updateGauges(refSpectrum, survSpectrum) {
  var refPwr = computeBandPower(refSpectrum);
  var survPwr = computeBandPower(survSpectrum);
  $('#gauge-ref').text(refPwr !== null ? refPwr.toFixed(1) + ' dB' : '-- dB');
  $('#gauge-surv').text(survPwr !== null ? survPwr.toFixed(1) + ' dB' : '-- dB');
  if (refPwr !== null && survPwr !== null) {
    var isolation = refPwr - survPwr;
    $('#gauge-isolation').text(isolation.toFixed(1) + ' dB');
    var box = $('#gauge-isolation-box');
    box.removeClass('warn bad');
    if (isolation < 20) box.addClass('bad');
    else if (isolation < 30) box.addClass('warn');
  } else {
    $('#gauge-isolation').text('-- dB');
  }
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

function updateConstellation(iqRef, iqSurv) {
  if (!iqRef || iqRef.length < 2 || !iqSurv || iqSurv.length < 2) return;
  var xRef = [], yRef = [], xSurv = [], ySurv = [];
  for (var i = 0; i < iqRef.length; i += 2) { xRef.push(iqRef[i]); yRef.push(iqRef[i + 1]); }
  for (var i = 0; i < iqSurv.length; i += 2) { xSurv.push(iqSurv[i]); ySurv.push(iqSurv[i + 1]); }
  Plotly.restyle('plot-const-ref', { x: [xRef], y: [yRef] });
  Plotly.restyle('plot-const-surv', { x: [xSurv], y: [ySurv] });
}

// Main poll loop
$(document).ready(function() {
  initSpectrumOverlay();
  initPeakhold();
  initConstellation();
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
            updateGauges(refSpectrum, survSpectrum);
            updatePeakhold(freqBins, refSpectrum, survSpectrum);
            updateWaterfall(freqBins, refSpectrum, survSpectrum);
          }
          if (data.iqRef && data.iqSurv) updateConstellation(data.iqRef, data.iqSurv);
        });
      }
    });
  }, 100);
});