var nRows = -1;
var timingKeys = [];
var timingPoller = null;
var resizeBinding = null;
var exportBinding = null;
var themeBinding = null;
var livePanel = create_live_panel(document.querySelector('[data-live-panel]'));

var urlTiming = build_api_url('/stash/timing');

var layout = {
  autosize: true,
  uirevision: 'timing-history',
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
    ticks: '',
    ticksuffix: ' '
  },
  legend: {
    orientation: 'h',
    bgcolor: 'rgba(8, 18, 22, 0.55)',
    bordercolor: 'rgba(206, 152, 97, 0.25)',
    borderwidth: 1,
    font: {
      color: '#ebe6dc'
    }
  }
};
var config = {
  responsive: true,
  displayModeBar: false,
  scrollZoom: true
};

apply_plot_theme(layout);

function convert_timing_timestamps(values) {
  if (!Array.isArray(values)) {
    return [];
  }

  return values.map(function (value) {
    return new Date(value);
  });
}

function get_timing_keys(payload) {
  return Object.keys(payload).filter(function (key) {
    return key !== 'timestamp' && key !== 'uptime_s' && key !== 'uptime_days';
  });
}

function format_uptime(payload) {
  if (Number.isFinite(payload.uptime_days)) {
    return payload.uptime_days.toFixed(1) + ' d';
  }

  if (Number.isFinite(payload.uptime_s)) {
    if (payload.uptime_s < 3600) {
      return Math.round(payload.uptime_s / 60) + ' m';
    }
    return (payload.uptime_s / 3600).toFixed(1) + ' h';
  }

  return '-';
}

function render_timing(payload) {
  var timestamps = convert_timing_timestamps(payload.timestamp);
  var nextKeys = get_timing_keys(payload);

  if (payload.nRows !== nRows || nextKeys.join('|') !== timingKeys.join('|')) {
    nRows = payload.nRows;
    timingKeys = nextKeys;

    var traces = timingKeys.map(function (key) {
      return {
        x: timestamps,
        y: payload[key],
        mode: 'lines+markers',
        type: 'scatter',
        name: key,
        line: {
          width: 3
        },
        marker: {
          size: 7
        }
      };
    });

    Plotly.react('data', traces, layout, config);
  }
  else {
    var xValues = [];
    var yValues = [];
    timingKeys.forEach(function (key) {
      xValues.push(timestamps);
      yValues.push(payload[key]);
    });

    Plotly.update('data', {
      x: xValues,
      y: yValues
    });
  }

  if (livePanel) {
    livePanel.setMetrics({
      series: timingKeys.length,
      samples: timestamps.length,
      uptime: format_uptime(payload)
    });
  }
}

Plotly.newPlot('data', [], layout, config);
resizeBinding = bind_plot_resize('data');
exportBinding = bind_plot_exports({
  plotId: 'data',
  baseFilename: 'blah2-pipeline-timing'
});
themeBinding = bind_plot_theme('data', layout);

timingPoller = create_timestamp_poller({
  livePanel: livePanel,
  visibleMinDelayMs: 450,
  visibleMaxDelayMs: 2800,
  idleStepMs: 280,
  onRefresh: function () {
    return request_json(urlTiming).then(render_timing);
  }
});
timingPoller.start();

window.addEventListener('beforeunload', function () {
  if (timingPoller) {
    timingPoller.stop();
  }
  if (resizeBinding) {
    resizeBinding.disconnect();
  }
  if (themeBinding) {
    themeBinding.disconnect();
  }
});
