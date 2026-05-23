var nRows = -1;
var mapPoller = null;
var resizeBinding = null;
var exportBinding = null;
var themeBinding = null;
var livePanel = create_live_panel(document.querySelector('[data-live-panel]'));

var urlDetection = build_api_url('/api/detection');
var urlAdsb = build_api_url('/api/adsb');
var urlConfig = build_api_url('/api/config');
var urlTracker = build_api_url('/api/tracker');
urlMap = build_api_url(urlMap);

var isTruth = false;
var detection = { delay: [], doppler: [] };
var adsbTargets = {};
var selectedAdsbTarget = 'all';
var track = {};
var isMaxholdMap = (typeof urlMap === 'string' && urlMap.indexOf('/stash/map') !== -1);
var maxholdTrackHistory = {};
var maxholdTrackTtlCpi = 20;
var maxholdFrame = 0;
var lastLockedAxisBounds = null;

var configReady = request_json(urlConfig)
  .then(function (data_config) {
    isTruth = Boolean(
      data_config &&
      data_config.truth &&
      data_config.truth.adsb &&
      data_config.truth.adsb.enabled === true
    );
  })
  .catch(function () {
    isTruth = false;
  });

var layout = {
  autosize: true,
  uirevision: 'map-surface',
  dragmode: 'pan',
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
      text: 'Bistatic Range (km)',
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
      text: 'Bistatic Doppler (Hz)',
      font: {
        size: 22
      }
    },
    ticks: '',
    ticksuffix: ' ',
    showgrid: true,
    gridcolor: 'rgba(206, 152, 97, 0.12)',
    linecolor: 'rgba(235, 230, 220, 0.28)'
  },
  showlegend: false
};
var config = {
  responsive: true,
  displayModeBar: false
};

apply_plot_theme(layout);

function get_placeholder_traces() {
  return [
    {
      z: [[0]],
      x: [0],
      y: [0],
      colorscale: 'Viridis',
      type: 'heatmap'
    },
    {
      x: [],
      y: [],
      text: [],
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 16,
        opacity: 0.82,
        color: 'rgba(255, 165, 0, 0.85)',
        line: {
          width: 1,
          color: 'rgba(3, 9, 12, 0.9)'
        }
      },
      hovertemplate: 'Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'Blah2 Target'
    },
    {
      x: [],
      y: [],
      text: [],
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 14,
        opacity: 0.68,
        color: 'rgba(211, 243, 106, 0.85)',
        line: {
          width: 1,
          color: 'rgba(3, 9, 12, 0.9)'
        }
      },
      hovertemplate: 'ADS-B Target: %{text}<br>Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'ADS-B'
    },
    {
      x: [],
      y: [],
      text: [],
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 18,
        opacity: 1,
        color: 'rgba(255, 241, 120, 1)',
        line: {
          width: 1,
          color: 'rgba(3, 9, 12, 0.9)'
        }
      },
      hovertemplate: 'ADS-B Target: %{text}<br>Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'Selected ADS-B'
    },
    {
      x: [],
      y: [],
      text: [],
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 6,
        opacity: 1,
        color: 'rgba(255, 120, 114, 1)'
      },
      hovertemplate: 'Track: %{text}<br>Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'Tracks'
    }
  ];
}

function getAdsbTargetFilterValue() {
  return document.querySelector('#adsb-target-filter')?.value.trim().toLowerCase() || '';
}

function formatAdsbLabel(targetId, targetInfo) {
  return targetInfo.flight ? targetInfo.flight + ' (' + targetId + ')' : targetId;
}

function coerceNumericArray(value) {
  var values = Array.isArray(value) ? value : value !== undefined ? [value] : [];
  return values.map(function (entry) {
    var numericValue = Number(entry);
    return Number.isFinite(numericValue) ? numericValue : NaN;
  });
}

function normalizeAdsbTarget(target, targetId) {
  if (!target) {
    return {delay: [], doppler: [], flight: []};
  }
  var rawDelay = coerceNumericArray(target.delay);
  var rawDoppler = coerceNumericArray(target.doppler);
  var flightLabel = target.flight || targetId;
  var length = Math.min(rawDelay.length, rawDoppler.length);
  var delay = [];
  var doppler = [];
  for (var index = 0; index < length; index += 1) {
    if (Number.isFinite(rawDelay[index]) && Number.isFinite(rawDoppler[index])) {
      delay.push(rawDelay[index]);
      doppler.push(rawDoppler[index]);
    }
  }
  var flight = Array(length).fill(flightLabel);
  flight = Array(delay.length).fill(flightLabel);
  return {delay: delay, doppler: doppler, flight: flight};
}

function formatAdsbRangeValue(target, targetId) {
  var finiteDelay = coerceNumericArray(target ? target.delay : undefined).filter(function (value) {
    return Number.isFinite(value);
  });

  if (finiteDelay.length === 0) {
    return '-';
  }

  if (finiteDelay.length === 1) {
    return finiteDelay[0].toFixed(2);
  }

  var minDelay = Math.min.apply(null, finiteDelay);
  var maxDelay = Math.max.apply(null, finiteDelay);
  if (Math.abs(maxDelay - minDelay) < 0.01) {
    return minDelay.toFixed(2);
  }

  return minDelay.toFixed(1) + ' to ' + maxDelay.toFixed(1);
}

function getAllAdsbTraceSelection() {
  var delay = [];
  var doppler = [];
  var flight = [];
  Object.keys(adsbTargets).sort().forEach(function (targetId) {
    var normalized = normalizeAdsbTarget(adsbTargets[targetId], targetId);
    delay.push.apply(delay, normalized.delay);
    doppler.push.apply(doppler, normalized.doppler);
    flight.push.apply(flight, normalized.flight);
  });
  return {delay: delay, doppler: doppler, flight: flight};
}

function getSelectedAdsbTraceSelection() {
  if (selectedAdsbTarget === 'all' || !adsbTargets[selectedAdsbTarget]) {
    return {delay: [], doppler: [], flight: []};
  }
  return normalizeAdsbTarget(adsbTargets[selectedAdsbTarget], selectedAdsbTarget);
}

function normalizeTrackData(track) {
  if (!track || !Array.isArray(track.data)) {
    return {delay: [], doppler: [], flight: []};
  }
  var delay = [];
  var doppler = [];
  var flight = [];
  track.data.forEach(function (target) {
    if (target.delay !== undefined && target.doppler !== undefined) {
      delay.push(target.delay);
      doppler.push(target.doppler);
      flight.push('Track ' + target.id + ' (' + target.state + ')');
    }
  });
  return {delay: delay, doppler: doppler, flight: flight};
}

function getTrackTraceSelection(track) {
  var current = normalizeTrackData(track);
  if (!isMaxholdMap) {
    return current;
  }

  maxholdFrame += 1;

  if (track && Array.isArray(track.data)) {
    track.data.forEach(function (target, i) {
      if (target.delay !== undefined && target.doppler !== undefined) {
        var id = target.id || ('idx_' + i);
        maxholdTrackHistory[id] = {
          delay: target.delay,
          doppler: target.doppler,
          flight: 'Track ' + id + ' (' + target.state + ')',
          frame: maxholdFrame
        };
      }
    });
  }

  Object.keys(maxholdTrackHistory).forEach(function (id) {
    if ((maxholdFrame - maxholdTrackHistory[id].frame) >= maxholdTrackTtlCpi) {
      delete maxholdTrackHistory[id];
    }
  });

  var merged = {delay: [], doppler: [], flight: []};
  Object.keys(maxholdTrackHistory).sort().forEach(function (id) {
    merged.delay.push(maxholdTrackHistory[id].delay);
    merged.doppler.push(maxholdTrackHistory[id].doppler);
    merged.flight.push(maxholdTrackHistory[id].flight);
  });

  return merged;
}

function updateAdsbTargetTable() {
  var tableBody = document.querySelector('#adsb-target-table tbody');
  if (!tableBody) {
    return;
  }

  var rows = [];
  var filterValue = getAdsbTargetFilterValue();
  var hasFilter = filterValue.length > 0;
  Object.keys(adsbTargets).sort().forEach(function (targetId) {
    var target = adsbTargets[targetId];
    var flight = target.flight || '';
    var filterText = (flight + ' ' + targetId).toLowerCase();
    if (hasFilter && filterText.indexOf(filterValue) === -1) {
      return;
    }
    var row = document.createElement('tr');
    if (selectedAdsbTarget === targetId) {
      row.classList.add('table-active');
    }
    row.dataset.targetId = targetId;
    row.innerHTML = '<td>' + (flight || '-') + '</td>' +
                    '<td>' + targetId + '</td>' +
                    '<td>' + formatAdsbRangeValue(target, targetId) + '</td>';
    row.addEventListener('click', function () {
      selectedAdsbTarget = this.dataset.targetId;
      updateAdsbTargetTable();
      updateAdsbTrace();
    });
    rows.push(row);
  });

  tableBody.innerHTML = '';
  if (Object.keys(adsbTargets).length === 0) {
    var emptyRow = document.createElement('tr');
    var emptyCell = document.createElement('td');
    emptyCell.setAttribute('colspan', '3');
    emptyCell.className = 'text-center text-muted small';
    emptyCell.textContent = 'No ADS-B targets available';
    emptyRow.appendChild(emptyCell);
    tableBody.appendChild(emptyRow);
    return;
  }

  var allRow = document.createElement('tr');
  if (selectedAdsbTarget === 'all') {
    allRow.classList.add('table-active');
  }
  allRow.dataset.targetId = 'all';
  allRow.innerHTML = '<td>All ADS-B targets</td><td>-</td><td>-</td>';
  allRow.addEventListener('click', function () {
    selectedAdsbTarget = 'all';
    updateAdsbTargetTable();
    updateAdsbTrace();
  });
  tableBody.appendChild(allRow);
  if (rows.length === 0) {
    var emptyRow = document.createElement('tr');
    var emptyCell = document.createElement('td');
    emptyCell.setAttribute('colspan', '3');
    emptyCell.className = 'text-center text-muted small';
    emptyCell.textContent = hasFilter ? 'No ADS-B targets match the filter' : 'No ADS-B targets available';
    emptyRow.appendChild(emptyCell);
    tableBody.appendChild(emptyRow);
    return;
  }
  rows.forEach(function (row) { tableBody.appendChild(row); });
}

function updateAdsbTrace() {
  var allSelection = getAllAdsbTraceSelection();
  var selectedSelection = getSelectedAdsbTraceSelection();
  Plotly.update('data', {
    x: [allSelection.delay, selectedSelection.delay],
    y: [allSelection.doppler, selectedSelection.doppler],
    text: [allSelection.flight, selectedSelection.flight]
  }, {}, [2, 3]);
}

function extractAdsbTargets(data_adsb) {
  var nextTargets = {};
  Object.keys(data_adsb || {}).forEach(function (aircraft) {
    if (data_adsb[aircraft] && 'doppler' in data_adsb[aircraft]) {
      nextTargets[aircraft] = {
        delay: data_adsb[aircraft].delay,
        doppler: data_adsb[aircraft].doppler,
        flight: data_adsb[aircraft].flight
      };
    }
  });
  return nextTargets;
}

function updateMapMetrics(mapData) {
  if (!livePanel) {
    return;
  }

  livePanel.setMetrics({
    detections: Array.isArray(detection.delay) ? detection.delay.length : 0,
    tracks: Array.isArray(track.data) ? track.data.length : 0,
    adsb: Object.keys(adsbTargets).length,
    power: Number.isFinite(mapData.maxPower) ? Math.max(13, mapData.maxPower).toFixed(1) + ' dB' : '-'
  });
}

function buildMapTraces(mapData) {
  var allSelection = getAllAdsbTraceSelection();
  var selectedSelection = getSelectedAdsbTraceSelection();
  var trackSelection = getTrackTraceSelection(track);
  var detectionText = Array.isArray(detection.delay) ? Array(detection.delay.length).fill('Blah2 Target') : [];

  return [
    {
      z: mapData.data,
      x: mapData.delay,
      y: mapData.doppler,
      colorscale: 'Viridis',
      zauto: false,
      zmin: 0,
      zmax: Math.max(13, mapData.maxPower || 13),
      type: 'heatmap'
    },
    {
      x: detection.delay,
      y: detection.doppler,
      text: detectionText,
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 16,
        opacity: 0.82,
        color: 'rgba(255, 165, 0, 0.85)',
        line: {
          width: 1,
          color: 'rgba(3, 9, 12, 0.9)'
        }
      },
      hovertemplate: 'Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'Blah2 Target'
    },
    {
      x: allSelection.delay,
      y: allSelection.doppler,
      text: allSelection.flight,
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 14,
        opacity: 0.68,
        color: 'rgba(211, 243, 106, 0.85)',
        line: {
          width: 1,
          color: 'rgba(3, 9, 12, 0.9)'
        }
      },
      hovertemplate: 'ADS-B Target: %{text}<br>Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'ADS-B'
    },
    {
      x: selectedSelection.delay,
      y: selectedSelection.doppler,
      text: selectedSelection.flight,
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 18,
        opacity: 1,
        color: 'rgba(255, 241, 120, 1)',
        line: {
          width: 1,
          color: 'rgba(3, 9, 12, 0.9)'
        }
      },
      hovertemplate: 'ADS-B Target: %{text}<br>Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'Selected ADS-B'
    },
    {
      x: trackSelection.delay,
      y: trackSelection.doppler,
      text: trackSelection.flight,
      mode: 'markers',
      type: 'scatter',
      marker: {
        size: 6,
        opacity: 1,
        color: 'rgba(255, 120, 114, 1)'
      },
      hovertemplate: 'Track: %{text}<br>Range: %{x}<br>Doppler: %{y}<extra></extra>',
      name: 'Tracks'
    }
  ];
}

function lockMapAxes(mapData) {
  var nextBounds = {};
  var axisUpdate = {};

  if (Array.isArray(mapData.delay) && mapData.delay.length > 0) {
    nextBounds.x = [mapData.delay[0], mapData.delay[mapData.delay.length - 1]];
    axisUpdate['xaxis.autorange'] = false;
    axisUpdate['xaxis.range'] = nextBounds.x;
  }

  if (Array.isArray(mapData.doppler) && mapData.doppler.length > 0) {
    nextBounds.y = [mapData.doppler[0], mapData.doppler[mapData.doppler.length - 1]];
    axisUpdate['yaxis.autorange'] = false;
    axisUpdate['yaxis.range'] = nextBounds.y;
  }

  if (Object.keys(axisUpdate).length === 0) {
    return Promise.resolve();
  }

  if (
    lastLockedAxisBounds &&
    JSON.stringify(lastLockedAxisBounds) === JSON.stringify(nextBounds)
  ) {
    return Promise.resolve();
  }

  return Promise.resolve(Plotly.relayout('data', axisUpdate)).then(function () {
    lastLockedAxisBounds = nextBounds;
  });
}

function renderMap(mapData) {
  var traces = buildMapTraces(mapData);
  var plotPromise;
  if (mapData.nRows !== nRows) {
    nRows = mapData.nRows;
    plotPromise = Plotly.react('data', traces, layout, config);
  }
  else {
    plotPromise = Plotly.update('data', {
      x: [traces[0].x, traces[1].x, traces[2].x, traces[3].x, traces[4].x],
      y: [traces[0].y, traces[1].y, traces[2].y, traces[3].y, traces[4].y],
      z: [traces[0].z, undefined, undefined, undefined, undefined],
      zmax: [traces[0].zmax, undefined, undefined, undefined, undefined],
      text: [undefined, traces[1].text, traces[2].text, traces[3].text, traces[4].text]
    });
  }

  return Promise.resolve(plotPromise)
    .then(function () {
      return lockMapAxes(mapData);
    })
    .then(function () {
      updateMapMetrics(mapData);
    });
}

function refreshMap() {
  return configReady.then(function () {
    return Promise.all([
      request_json(urlMap),
      request_json(urlDetection).catch(function () {
        return detection;
      }),
      request_json(urlTracker).catch(function () {
        return track;
      }),
      isTruth ? request_json(urlAdsb).catch(function () {
        return null;
      }) : Promise.resolve(null)
    ]).then(function (responses) {
      var mapData = responses[0];
      detection = responses[1] || { delay: [], doppler: [] };
      track = responses[2] || {};

      if (!isTruth) {
        adsbTargets = {};
        selectedAdsbTarget = 'all';
      }
      else if (responses[3] !== null) {
        adsbTargets = extractAdsbTargets(responses[3]);
      }

      if (selectedAdsbTarget !== 'all' && !adsbTargets[selectedAdsbTarget]) {
        selectedAdsbTarget = 'all';
      }

      updateAdsbTargetTable();
      return renderMap(mapData);
    });
  });
}

Plotly.newPlot('data', get_placeholder_traces(), layout, config);
resizeBinding = bind_plot_resize('data');
exportBinding = bind_plot_exports({
  plotId: 'data',
  baseFilename: isMaxholdMap ? 'blah2-maxhold-map' : 'blah2-delay-doppler-map'
});
themeBinding = bind_plot_theme('data', layout);

mapPoller = create_timestamp_poller({
  livePanel: livePanel,
  visibleMinDelayMs: 400,
  visibleMaxDelayMs: 2400,
  idleStepMs: 240,
  onRefresh: refreshMap
});
mapPoller.start();

window.addEventListener('beforeunload', function () {
  if (mapPoller) {
    mapPoller.stop();
  }
  if (resizeBinding) {
    resizeBinding.disconnect();
  }
  if (themeBinding) {
    themeBinding.disconnect();
  }
});
