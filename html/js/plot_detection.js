var nRows = -1;
var detectionPoller = null;
var resizeBinding = null;
var exportBinding = null;
var themeBinding = null;
var livePanel = create_live_panel(document.querySelector('[data-live-panel]'));

var urlDetection = build_api_url('/stash/detection');

var layout = {
  autosize: true,
  uirevision: 'detection-history',
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
      text: xTitle,
      font: {
        size: 22
      }
    },
    showgrid: true,
    gridcolor: 'rgba(206, 152, 97, 0.12)',
    linecolor: 'rgba(235, 230, 220, 0.28)',
    zerolinecolor: 'rgba(235, 230, 220, 0.18)',
    ticks: '',
    side: 'bottom'
  },
  yaxis: {
    title: {
      text: yTitle,
      font: {
        size: 22
      }
    },
    showgrid: true,
    gridcolor: 'rgba(206, 152, 97, 0.12)',
    linecolor: 'rgba(235, 230, 220, 0.28)',
    zerolinecolor: 'rgba(235, 230, 220, 0.18)',
    ticks: '',
    ticksuffix: ' '
  }
};
var config = {
  responsive: true,
  displayModeBar: false,
  scrollZoom: true
};

apply_plot_theme(layout);

function convert_detection_axis(values) {
  if (!Array.isArray(values)) {
    return [];
  }

  if (xVariable !== 'timestamp') {
    return values.slice();
  }

  return values.map(function (value) {
    return new Date(value);
  });
}

function build_detection_trace(payload) {
  return {
    x: convert_detection_axis(payload[xVariable]),
    y: Array.isArray(payload[yVariable]) ? payload[yVariable].slice() : [],
    mode: 'markers',
    type: 'scatter',
    marker: {
      size: 11,
      color: 'rgba(240, 186, 127, 0.92)',
      line: {
        width: 1,
        color: 'rgba(3, 9, 12, 0.9)'
      }
    },
    hovertemplate: xTitle + ': %{x}<br>' + yTitle + ': %{y}<extra></extra>'
  };
}

function render_detection(payload) {
  var trace = build_detection_trace(payload);

  if (payload.nRows !== nRows) {
    nRows = payload.nRows;
    Plotly.react('data', [trace], layout, config);
  }
  else {
    Plotly.update('data', {
      x: [trace.x],
      y: [trace.y]
    });
  }

  if (livePanel) {
    livePanel.setMetrics({
      samples: trace.y.length,
      rows: payload.nRows || 0
    });
  }
}

Plotly.newPlot('data', [{
  x: [],
  y: [],
  mode: 'markers',
  type: 'scatter',
  marker: {
    size: 11,
    color: 'rgba(240, 186, 127, 0.92)'
  }
}], layout, config);
resizeBinding = bind_plot_resize('data');
exportBinding = bind_plot_exports({
  plotId: 'data',
  baseFilename: xVariable === 'timestamp' && yVariable === 'delay'
    ? 'blah2-detection-delay'
    : xVariable === 'timestamp' && yVariable === 'doppler'
      ? 'blah2-detection-doppler'
      : 'blah2-detection-delay-doppler'
});
themeBinding = bind_plot_theme('data', layout);

detectionPoller = create_timestamp_poller({
  livePanel: livePanel,
  visibleMinDelayMs: 425,
  visibleMaxDelayMs: 2600,
  idleStepMs: 260,
  onRefresh: function () {
    return request_json(urlDetection).then(render_detection);
  }
});
detectionPoller.start();

window.addEventListener('beforeunload', function () {
  if (detectionPoller) {
    detectionPoller.stop();
  }
  if (resizeBinding) {
    resizeBinding.disconnect();
  }
  if (themeBinding) {
    themeBinding.disconnect();
  }
});
