var nRows = -1;
var spectrumPoller = null;
var resizeBinding = null;
var exportBinding = null;
var themeBinding = null;
var livePanel = create_live_panel(document.querySelector('[data-live-panel]'));

var urlMap = build_api_url('/stash/iqdata');

var layout = {
  autosize: true,
  uirevision: 'spectrum-history',
  margin: {
    l: 60,
    r: 30,
    b: 60,
    t: 20,
    pad: 0
  },
  font: {
    family: 'Trebuchet MS, Gill Sans, sans-serif',
    color: '#ebe6dc'
  },
  hoverlabel: {
    namelength: 0,
    bgcolor: 'rgba(8, 18, 22, 0.96)',
    bordercolor: '#ce9861',
    font: {
      family: 'Trebuchet MS, Gill Sans, sans-serif'
    }
  },
  plot_bgcolor: 'rgba(0,0,0,0)',
  paper_bgcolor: 'rgba(0,0,0,0)',
  annotations: [],
  xaxis: {
    title: {
      text: 'Frequency (MHz)',
      font: {
        size: 22
      }
    },
    ticks: '',
    side: 'bottom',
    showgrid: true,
    gridcolor: 'rgba(206, 152, 97, 0.12)',
    linecolor: 'rgba(235, 230, 220, 0.28)'
  },
  yaxis: {
    title: {
      text: 'Timestamp',
      font: {
        size: 22
      }
    },
    ticks: '',
    ticksuffix: ' ',
    showgrid: true,
    gridcolor: 'rgba(206, 152, 97, 0.12)',
    linecolor: 'rgba(235, 230, 220, 0.28)'
  }
};
var config = {
  responsive: true,
  displayModeBar: false
};

apply_plot_theme(layout);

function convert_spectrum_timestamps(values) {
  if (!Array.isArray(values)) {
    return [];
  }

  return values.map(function (value) {
    return new Date(value);
  });
}

function get_spectrum_frequency_axis(payload, binCount) {
  var frequencyHistory = Array.isArray(payload.frequency) ? payload.frequency : [];
  var firstRow = Array.isArray(frequencyHistory[0]) ? frequencyHistory[0] : [];
  if (!firstRow.length || (binCount > 0 && firstRow.length !== binCount)) {
    return [];
  }

  return firstRow.map(function (value) {
    return Number.isFinite(value) ? value / 1000 : value;
  });
}

function render_spectrum(payload) {
  var timestamps = convert_spectrum_timestamps(payload.timestamp);
  var spectrumValues = Array.isArray(payload.spectrum) ? payload.spectrum : [];
  var binCount = spectrumValues.length > 0 && Array.isArray(spectrumValues[0]) ? spectrumValues[0].length : 0;
  var frequencyAxis = get_spectrum_frequency_axis(payload, binCount);
  var trace = {
    y: timestamps,
    z: spectrumValues,
    colorscale: 'Jet',
    type: 'heatmap'
  };

  if (frequencyAxis.length) {
    trace.x = frequencyAxis;
  }

  if (timestamps.length !== nRows) {
    nRows = timestamps.length;
    Plotly.react('data', [trace], layout, config);
  }
  else {
    var traceUpdate = {
      y: [trace.y],
      z: [trace.z]
    };
    if (frequencyAxis.length) {
      traceUpdate.x = [frequencyAxis];
    }
    Plotly.update('data', traceUpdate);
  }

  if (livePanel) {
    livePanel.setMetrics({
      rows: timestamps.length,
      bins: binCount
    });
  }
}

Plotly.newPlot('data', [{
  y: [],
  z: [[]],
  colorscale: 'Jet',
  type: 'heatmap'
}], layout, config);
resizeBinding = bind_plot_resize('data');
exportBinding = bind_plot_exports({
  plotId: 'data',
  baseFilename: 'blah2-spectrum-reference'
});
themeBinding = bind_plot_theme('data', layout);

spectrumPoller = create_timestamp_poller({
  livePanel: livePanel,
  visibleMinDelayMs: 450,
  visibleMaxDelayMs: 2800,
  idleStepMs: 280,
  onRefresh: function () {
    return request_json(urlMap).then(render_spectrum);
  }
});
spectrumPoller.start();

window.addEventListener('beforeunload', function () {
  if (spectrumPoller) {
    spectrumPoller.stop();
  }
  if (resizeBinding) {
    resizeBinding.disconnect();
  }
  if (themeBinding) {
    themeBinding.disconnect();
  }
});
