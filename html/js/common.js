function is_localhost(ip) {
  if (ip === 'localhost' || ip === '::1') {
    return true;
  }

  const localRanges = ['127.0.0.0/8', '192.168.0.0/16', '10.0.0.0/8', '172.16.0.0/12'];

  const ipToInt = input => {
    const octets = input.split('.');
    if (octets.length !== 4 || octets.some(octet => !/^\d+$/.test(octet))) {
      return null;
    }

    const values = octets.map(Number);
    if (values.some(value => value < 0 || value > 255)) {
      return null;
    }

    return values.reduce((acc, octet) => (acc << 8) + octet, 0) >>> 0;
  };

  const ipInt = ipToInt(ip);
  if (ipInt === null) {
    return false;
  }

  return localRanges.some(range => {
    const [rangeStart, maskBitsRaw = '32'] = range.split('/');
    const maskBits = Number(maskBitsRaw);
    if (!Number.isInteger(maskBits) || maskBits < 0 || maskBits > 32) {
      return false;
    }

    const start = ipToInt(rangeStart);
    if (start === null) {
      return false;
    }

    const mask = maskBits === 0 ? 0 : (0xFFFFFFFF << (32 - maskBits)) >>> 0;
    return (ipInt & mask) === (start & mask);
  });

}

function sanitize_api_base(apiBase) {
  if (!apiBase || apiBase.trim().length === 0) {
    return null;
  }

  const trimmedBase = apiBase.trim().replace(/\/+$/, '');

  try {
    if (trimmedBase.startsWith('//')) {
      const parsed = new URL(window.location.protocol + trimmedBase);
      if (parsed.pathname !== '/' || parsed.search || parsed.hash) {
        return null;
      }

      return '//' + parsed.host;
    }

    const parsed = new URL(trimmedBase);
    if ((parsed.protocol !== 'http:' && parsed.protocol !== 'https:') || parsed.pathname !== '/' || parsed.search || parsed.hash) {
      return null;
    }

    return parsed.protocol + '//' + parsed.host;
  } catch (error) {
    return null;
  }
}

function format_host_for_url(host) {
  if (host.includes(':') && !host.startsWith('[') && !host.endsWith(']')) {
    return '[' + host + ']';
  }

  return host;
}

function get_api_base_url() {
  const host = window.location.hostname;
  const hostForUrl = format_host_for_url(host);
  const query = new URLSearchParams(window.location.search);
  const apiBase = query.get('api_base');
  const apiPort = query.get('api_port');
  const sanitizedApiBase = sanitize_api_base(apiBase);

  if (sanitizedApiBase) {
    return sanitizedApiBase;
  }

  if (apiPort && /^\d+$/.test(apiPort)) {
    const port = Number(apiPort);
    if (port >= 1 && port <= 65535) {
      return '//' + hostForUrl + ':' + port;
    }
  }

  if (is_localhost(host)) {
    return '//' + hostForUrl + ':3000';
  }

  return '//' + hostForUrl;
}

function build_api_url(path) {
  if (/^https?:\/\//.test(path) || /^\/\//.test(path)) {
    return path;
  }
  const normalizedPath = path.startsWith('/') ? path : '/' + path;
  return get_api_base_url() + normalizedPath;
}

function append_api_query_to_path(rawHref) {
  const currentQuery = new URLSearchParams(window.location.search);
  const keys = ['api_base', 'api_port'];

  if (!rawHref || rawHref.startsWith('#') || /^https?:\/\//.test(rawHref) || /^\/\//.test(rawHref)) {
    return rawHref;
  }

  if (!keys.some(function (key) { return currentQuery.has(key); })) {
    return rawHref;
  }

  const nextUrl = new URL(rawHref, window.location.origin);
  keys.forEach(function (key) {
    if (currentQuery.has(key) && !nextUrl.searchParams.has(key)) {
      nextUrl.searchParams.set(key, currentQuery.get(key));
    }
  });

  return nextUrl.pathname + nextUrl.search + nextUrl.hash;
}

function preserve_api_query_links() {
  document.querySelectorAll('a[data-preserve-api-query]').forEach(function (anchor) {
    const rawHref = anchor.getAttribute('href');
    const nextHref = append_api_query_to_path(rawHref);
    if (nextHref) {
      anchor.setAttribute('href', nextHref);
    }
  });
}

var controllerNavigationGroups = [
  {
    title: 'Primary',
    links: [
      { href: '/', label: 'Radar', type: 'page' },
      { href: '/controller', label: 'Controller', type: 'page' }
    ]
  },
  {
    title: 'Live Views',
    links: [
      { href: '/display/map', label: 'Delay-Doppler map', type: 'page' },
      { href: '/display/maxhold', label: 'Max-hold map', type: 'page' },
      { href: '/display/detection/delay', label: 'Detections in delay', type: 'page' },
      { href: '/display/detection/doppler', label: 'Detections in Doppler', type: 'page' },
      { href: '/display/detection/delay-doppler', label: 'Detections in delay-Doppler', type: 'page' },
      { href: '/display/spectrum', label: 'Spectrum reference', type: 'page' },
      { href: '/display/timing', label: 'Timing display', type: 'page' }
    ]
  },
  {
    title: 'Processor Outputs',
    links: [
      { href: '/api/map', label: 'Map data', type: 'api' },
      { href: '/api/detection', label: 'Detection data', type: 'api' },
      { href: '/api/tracker', label: 'Tracker data', type: 'api' },
      { href: '/api/timing', label: 'Timing data', type: 'api' },
      { href: '/api/iqdata', label: 'IQ metadata', type: 'api' },
      { href: '/api/timestamp', label: 'Latest timestamp', type: 'api' }
    ]
  },
  {
    title: 'Cached Views',
    links: [
      { href: '/stash/map', label: 'Map data stash', type: 'api' },
      { href: '/stash/detection', label: 'Detection stash', type: 'api' },
      { href: '/stash/iqdata', label: 'Spectrum stash', type: 'api' },
      { href: '/stash/timing', label: 'Timing stash', type: 'api' }
    ]
  }
];

var themeToggleButtons = [];

function get_saved_theme() {
  try {
    return window.localStorage.getItem('blah2-theme');
  } catch (error) {
    return null;
  }
}

function get_valid_theme(theme) {
  return theme === 'light' ? 'light' : 'dark';
}

function get_current_theme() {
  return get_valid_theme(document.documentElement.dataset.theme);
}

function update_theme_toggle_buttons() {
  var currentTheme = get_current_theme();
  var nextTheme = currentTheme === 'dark' ? 'light' : 'dark';

  themeToggleButtons.forEach(function (button) {
    button.dataset.theme = currentTheme;
    button.setAttribute('aria-label', 'Switch to ' + nextTheme + ' mode');
    button.setAttribute('title', 'Switch to ' + nextTheme + ' mode');
  });
}

function set_theme(theme, options) {
  var settings = options || {};
  var nextTheme = get_valid_theme(theme);
  document.documentElement.dataset.theme = nextTheme;

  if (settings.persist !== false) {
    try {
      window.localStorage.setItem('blah2-theme', nextTheme);
    } catch (error) {
    }
  }

  update_theme_toggle_buttons();

  if (settings.notify !== false) {
    window.dispatchEvent(new CustomEvent('blah2:themechange', {
      detail: { theme: nextTheme }
    }));
  }
}

function initialize_theme() {
  set_theme(get_saved_theme(), {
    persist: false,
    notify: false
  });
}

function resolve_controller_nav_href(link) {
  if (link.type === 'api') {
    return build_api_url(link.href);
  }

  return append_api_query_to_path(link.href);
}

function create_controller_menu() {
  var menu = document.createElement('details');
  menu.className = 'controller-menu';

  var trigger = document.createElement('summary');
  trigger.className = 'chrome-icon-button controller-menu__trigger';
  trigger.setAttribute('aria-label', 'Open controller navigation');
  trigger.innerHTML = '<span class="burger-icon" aria-hidden="true"><span></span><span></span><span></span></span><span class="chrome-button-label">Menu</span>';
  menu.appendChild(trigger);

  var panel = document.createElement('div');
  panel.className = 'controller-menu__panel';

  controllerNavigationGroups.forEach(function (group) {
    var section = document.createElement('section');
    section.className = 'controller-menu__group';

    var heading = document.createElement('div');
    heading.className = 'controller-menu__group-title';
    heading.textContent = group.title;
    section.appendChild(heading);

    var list = document.createElement('div');
    list.className = 'controller-menu__links';

    group.links.forEach(function (link) {
      var anchor = document.createElement('a');
      anchor.className = 'controller-menu__link';
      anchor.href = resolve_controller_nav_href(link);
      anchor.textContent = link.label;
      anchor.addEventListener('click', function () {
        menu.removeAttribute('open');
      });
      list.appendChild(anchor);
    });

    section.appendChild(list);
    panel.appendChild(section);
  });

  menu.appendChild(panel);
  return menu;
}

function create_theme_toggle_button() {
  var button = document.createElement('button');
  button.type = 'button';
  button.className = 'chrome-icon-button theme-toggle';
  button.innerHTML = '<span class="theme-toggle__icons" aria-hidden="true"><span class="theme-icon" data-theme-icon="light">&#9728;</span><span class="theme-icon" data-theme-icon="dark">&#9790;</span></span><span class="visually-hidden">Toggle theme</span>';
  button.addEventListener('click', function () {
    set_theme(get_current_theme() === 'dark' ? 'light' : 'dark');
  });
  themeToggleButtons.push(button);
  update_theme_toggle_buttons();
  return button;
}

function initialize_shell_controls() {
  var shell = document.querySelector('.app-shell');
  if (!shell || shell.querySelector('.shell-controls')) {
    return;
  }

  var controls = document.createElement('div');
  controls.className = 'shell-controls';

  var left = document.createElement('div');
  left.className = 'shell-controls__left';
  var right = document.createElement('div');
  right.className = 'shell-controls__right';

  if (/^\/display\//.test(window.location.pathname) || window.location.pathname === '/' || window.location.pathname === '/index.html') {
    left.appendChild(create_controller_menu());
  } else {
    controls.classList.add('shell-controls--align-end');
  }

  right.appendChild(create_theme_toggle_button());
  controls.appendChild(left);
  controls.appendChild(right);
  shell.insertBefore(controls, shell.firstChild);
}

function get_theme_palette() {
  var styles = window.getComputedStyle(document.documentElement);

  function read_value(name, fallback) {
    var value = styles.getPropertyValue(name).trim();
    return value || fallback;
  }

  return {
    plotFont: read_value('--plot-font', read_value('--ink', '#ebe6dc')),
    plotGrid: read_value('--plot-grid', 'rgba(206, 152, 97, 0.12)'),
    plotAxis: read_value('--plot-axis', 'rgba(235, 230, 220, 0.28)'),
    plotZeroline: read_value('--plot-zeroline', 'rgba(235, 230, 220, 0.18)'),
    plotHoverBg: read_value('--plot-hover-bg', 'rgba(8, 18, 22, 0.96)'),
    plotHoverBorder: read_value('--plot-hover-border', read_value('--accent', '#ce9861')),
    plotLegendBg: read_value('--plot-legend-bg', 'rgba(8, 18, 22, 0.55)'),
    plotLegendBorder: read_value('--plot-legend-border', 'rgba(206, 152, 97, 0.25)')
  };
}

function apply_plot_theme(layout) {
  var palette = get_theme_palette();

  layout.font = layout.font || {};
  layout.font.color = palette.plotFont;
  layout.hoverlabel = layout.hoverlabel || {};
  layout.hoverlabel.bgcolor = palette.plotHoverBg;
  layout.hoverlabel.bordercolor = palette.plotHoverBorder;
  layout.hoverlabel.font = layout.hoverlabel.font || {};
  layout.hoverlabel.font.color = palette.plotFont;
  layout.plot_bgcolor = 'rgba(0,0,0,0)';
  layout.paper_bgcolor = 'rgba(0,0,0,0)';

  ['xaxis', 'yaxis'].forEach(function (axisName) {
    if (!layout[axisName]) {
      return;
    }

    layout[axisName].gridcolor = palette.plotGrid;
    layout[axisName].linecolor = palette.plotAxis;
    layout[axisName].tickfont = layout[axisName].tickfont || {};
    layout[axisName].tickfont.color = palette.plotFont;
    layout[axisName].title = layout[axisName].title || {};
    layout[axisName].title.font = layout[axisName].title.font || {};
    layout[axisName].title.font.color = palette.plotFont;
    if (Object.prototype.hasOwnProperty.call(layout[axisName], 'zerolinecolor')) {
      layout[axisName].zerolinecolor = palette.plotZeroline;
    }
  });

  if (layout.legend) {
    layout.legend.bgcolor = palette.plotLegendBg;
    layout.legend.bordercolor = palette.plotLegendBorder;
    layout.legend.font = layout.legend.font || {};
    layout.legend.font.color = palette.plotFont;
  }
}

function get_plot_theme_relayout(layout) {
  var palette = get_theme_palette();
  var update = {
    'font.color': palette.plotFont,
    'hoverlabel.bgcolor': palette.plotHoverBg,
    'hoverlabel.bordercolor': palette.plotHoverBorder,
    'paper_bgcolor': 'rgba(0,0,0,0)',
    'plot_bgcolor': 'rgba(0,0,0,0)'
  };

  ['xaxis', 'yaxis'].forEach(function (axisName) {
    if (!layout[axisName]) {
      return;
    }

    update[axisName + '.gridcolor'] = palette.plotGrid;
    update[axisName + '.linecolor'] = palette.plotAxis;
    update[axisName + '.tickfont.color'] = palette.plotFont;
    update[axisName + '.title.font.color'] = palette.plotFont;
    if (Object.prototype.hasOwnProperty.call(layout[axisName], 'zerolinecolor')) {
      update[axisName + '.zerolinecolor'] = palette.plotZeroline;
    }
  });

  if (layout.legend) {
    update['legend.bgcolor'] = palette.plotLegendBg;
    update['legend.bordercolor'] = palette.plotLegendBorder;
    update['legend.font.color'] = palette.plotFont;
  }

  return update;
}

function bind_plot_theme(plotId, layout) {
  function handle_theme_change() {
    apply_plot_theme(layout);
    if (typeof Plotly === 'undefined' || typeof Plotly.relayout !== 'function') {
      return;
    }

    Plotly.relayout(plotId, get_plot_theme_relayout(layout));
  }

  window.addEventListener('blah2:themechange', handle_theme_change);
  return {
    disconnect: function () {
      window.removeEventListener('blah2:themechange', handle_theme_change);
    }
  };
}

initialize_theme();
window.addEventListener('DOMContentLoaded', function () {
  preserve_api_query_links();
  initialize_shell_controls();
});

function request_text(url) {
  return new Promise(function (resolve, reject) {
    $.get(url)
      .done(function (data) {
        resolve(data);
      })
      .fail(function (_jqXHR, textStatus, errorThrown) {
        reject(new Error(errorThrown || textStatus || 'Request failed'));
      });
  });
}

function request_json(url) {
  return new Promise(function (resolve, reject) {
    $.getJSON(url)
      .done(function (data) {
        resolve(data);
      })
      .fail(function (_jqXHR, textStatus, errorThrown) {
        reject(new Error(errorThrown || textStatus || 'Request failed'));
      });
  });
}

function format_clock_time(value) {
  if (!value) {
    return 'No updates yet';
  }

  return new Date(value).toLocaleTimeString([], {
    hour: '2-digit',
    minute: '2-digit',
    second: '2-digit'
  });
}

function format_elapsed_time(deltaMs) {
  if (!Number.isFinite(deltaMs) || deltaMs < 0) {
    return 'just now';
  }

  const totalSeconds = Math.round(deltaMs / 1000);
  if (totalSeconds < 60) {
    return totalSeconds + 's ago';
  }

  const totalMinutes = Math.round(totalSeconds / 60);
  if (totalMinutes < 60) {
    return totalMinutes + 'm ago';
  }

  const totalHours = Math.round(totalMinutes / 60);
  return totalHours + 'h ago';
}

function format_poll_delay(delayMs) {
  if (!Number.isFinite(delayMs)) {
    return 'paused';
  }

  if (delayMs < 1000) {
    return Math.round(delayMs) + ' ms';
  }

  if (delayMs % 1000 === 0) {
    return (delayMs / 1000) + ' s';
  }

  return (delayMs / 1000).toFixed(1) + ' s';
}

function parse_timestamp_value(timestamp) {
  if (typeof timestamp === 'number' && Number.isFinite(timestamp)) {
    return timestamp;
  }

  if (typeof timestamp !== 'string') {
    return null;
  }

  const trimmed = timestamp.trim();
  if (/^\d+$/.test(trimmed)) {
    const numeric = Number(trimmed);
    return Number.isFinite(numeric) ? numeric : null;
  }

  const parsed = Date.parse(trimmed);
  return Number.isFinite(parsed) ? parsed : null;
}

function clamp_number(value, minValue, maxValue) {
  return Math.min(Math.max(value, minValue), maxValue);
}

function sanitize_filename(value) {
  return (value || 'plot')
    .toLowerCase()
    .replace(/[^a-z0-9._-]+/g, '-')
    .replace(/^-+|-+$/g, '');
}

function download_blob(blob, filename) {
  const objectUrl = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = objectUrl;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  document.body.removeChild(anchor);
  window.setTimeout(function () {
    URL.revokeObjectURL(objectUrl);
  }, 0);
}

function escape_csv_value(value) {
  if (value === null || value === undefined) {
    return '';
  }

  const stringValue = String(value);
  if (/[",\n]/.test(stringValue)) {
    return '"' + stringValue.replace(/"/g, '""') + '"';
  }

  return stringValue;
}

function plot_trace_rows(trace, traceIndex) {
  const traceName = trace.name || trace.type || ('trace-' + traceIndex);
  const rows = [];

  if (trace.type === 'heatmap' && Array.isArray(trace.z)) {
    const yValues = Array.isArray(trace.y) && trace.y.length ? trace.y : trace.z.map(function (_row, rowIndex) {
      return rowIndex;
    });
    const xValues = Array.isArray(trace.x) && trace.x.length ? trace.x : ((trace.z[0] || []).map(function (_value, columnIndex) {
      return columnIndex;
    }));

    trace.z.forEach(function (row, rowIndex) {
      if (!Array.isArray(row)) {
        return;
      }

      row.forEach(function (cellValue, columnIndex) {
        rows.push({
          trace: traceName,
          trace_type: 'heatmap',
          row_index: rowIndex,
          column_index: columnIndex,
          x: xValues[columnIndex],
          y: yValues[rowIndex],
          value: cellValue
        });
      });
    });

    return rows;
  }

  const xValues = Array.isArray(trace.x) ? trace.x : [];
  const yValues = Array.isArray(trace.y) ? trace.y : [];
  const textValues = Array.isArray(trace.text) ? trace.text : [];
  const seriesLength = Math.max(xValues.length, yValues.length, textValues.length);
  for (let index = 0; index < seriesLength; index += 1) {
    rows.push({
      trace: traceName,
      trace_type: trace.type || 'scatter',
      point_index: index,
      x: xValues[index],
      y: yValues[index],
      text: textValues[index]
    });
  }

  return rows;
}

function plot_rows_to_csv(rows) {
  if (!rows.length) {
    return 'trace\n';
  }

  const columns = [];
  rows.forEach(function (row) {
    Object.keys(row).forEach(function (key) {
      if (!columns.includes(key)) {
        columns.push(key);
      }
    });
  });

  const csvLines = [columns.join(',')];
  rows.forEach(function (row) {
    csvLines.push(columns.map(function (key) {
      return escape_csv_value(row[key]);
    }).join(','));
  });

  return csvLines.join('\n') + '\n';
}

function collect_plot_rows(plotElement) {
  if (!plotElement || !Array.isArray(plotElement.data)) {
    return [];
  }

  const rows = [];
  plotElement.data.forEach(function (trace, traceIndex) {
    plot_trace_rows(trace, traceIndex).forEach(function (row) {
      rows.push(row);
    });
  });
  return rows;
}

function bind_plot_exports(options) {
  const settings = options || {};
  const plotId = settings.plotId;
  const plotElement = document.getElementById(plotId);
  const pngButton = document.querySelector(settings.pngButtonSelector || '[data-export-png]');
  const csvButton = document.querySelector(settings.csvButtonSelector || '[data-export-csv]');
  const baseFilename = sanitize_filename(settings.baseFilename || plotId || 'plot');

  if (!plotElement) {
    return null;
  }

  function get_png_filename() {
    return baseFilename;
  }

  function get_csv_filename() {
    return baseFilename + '.csv';
  }

  if (pngButton && typeof Plotly !== 'undefined' && typeof Plotly.downloadImage === 'function') {
    pngButton.addEventListener('click', function () {
      Plotly.downloadImage(plotElement, {
        format: 'png',
        filename: get_png_filename(),
        width: Math.max(plotElement.clientWidth * 2, 1200),
        height: Math.max(plotElement.clientHeight * 2, 800),
        scale: 1
      });
    });
  }

  if (csvButton) {
    csvButton.addEventListener('click', function () {
      const rows = typeof settings.getRows === 'function' ? settings.getRows(plotElement) : collect_plot_rows(plotElement);
      const csvText = plot_rows_to_csv(rows);
      download_blob(new Blob([csvText], { type: 'text/csv;charset=utf-8' }), get_csv_filename());
    });
  }

  return {
    plotElement: plotElement,
    pngButton: pngButton,
    csvButton: csvButton
  };
}

function create_live_panel(root) {
  if (!root) {
    return null;
  }

  const metricScope = root.closest('.panel') || root.parentElement || root;
  const metricElements = {};
  let stableDisplayMode = 'offline';
  metricScope.querySelectorAll('[data-stat-value]').forEach(function (element) {
    metricElements[element.getAttribute('data-stat-value')] = element;
  });

  const panel = {
    root: root,
    stateElement: root.querySelector('[data-live-state]'),
    detailElement: root.querySelector('[data-live-detail]'),
    updatedElement: root.querySelector('[data-live-updated]'),
    nextElement: root.querySelector('[data-live-next]'),
    toggleElement: root.querySelector('[data-live-toggle]'),
    metricElements: metricElements,
    setMode: function (mode, label, detail) {
      var displayMode = mode;
      var displayLabel = label;

      if (mode === 'syncing' || mode === 'paused') {
        displayMode = stableDisplayMode;
        displayLabel = stableDisplayMode === 'live' ? 'Live' : 'Offline';
      }
      else if (mode === 'waiting') {
        displayMode = stableDisplayMode;
        displayLabel = stableDisplayMode === 'live' ? 'Live' : 'Offline';
      }
      else if (mode === 'stale') {
        displayMode = 'offline';
        displayLabel = 'Offline';
      }

      if (displayMode === 'live' || displayMode === 'offline') {
        stableDisplayMode = displayMode;
      }

      root.dataset.pollState = displayMode;
      if (this.stateElement) {
        this.stateElement.dataset.state = displayMode;
        this.stateElement.textContent = displayLabel;
      }
      if (this.detailElement) {
        this.detailElement.textContent = detail || '';
      }
    },
    setLastUpdated: function (timestampMs) {
      if (!this.updatedElement) {
        return;
      }

      if (!timestampMs) {
        this.updatedElement.textContent = 'Waiting for first frame';
        return;
      }

      this.updatedElement.textContent = 'Updated ' + format_clock_time(timestampMs) + ' • ' + format_elapsed_time(Date.now() - timestampMs);
    },
    setNextPoll: function (delayMs) {
      if (!this.nextElement) {
        return;
      }

      if (!Number.isFinite(delayMs)) {
        this.nextElement.textContent = 'Polling paused';
        return;
      }

      this.nextElement.textContent = 'Next check ' + format_poll_delay(delayMs);
    },
    setPaused: function (isPaused) {
      if (!this.toggleElement) {
        return;
      }

      this.toggleElement.textContent = isPaused ? 'Resume stream' : 'Pause stream';
      this.toggleElement.setAttribute('aria-pressed', String(isPaused));
    },
    setMetric: function (key, value) {
      if (this.metricElements[key]) {
        this.metricElements[key].textContent = value;
      }
    },
    setMetrics: function (metrics) {
      const panelRef = this;
      Object.keys(metrics).forEach(function (key) {
        panelRef.setMetric(key, metrics[key]);
      });
    },
    bindToggle: function (handler) {
      if (!this.toggleElement) {
        return;
      }

      this.toggleElement.addEventListener('click', handler);
    }
  };

  panel.setMode('offline', 'Offline', 'CPI unavailable');
  panel.setLastUpdated(null);
  panel.setNextPoll(NaN);
  panel.setPaused(false);
  return panel;
}

function normalize_delay(value, fallback) {
  if (!Number.isFinite(value) || value < 0) {
    return fallback;
  }

  return value;
}

function create_timestamp_poller(options) {
  const settings = options || {};
  const panel = settings.livePanel || null;
  const timestampUrl = settings.timestampUrl || build_api_url('/api/timestamp');
  const visibleMinDelayMs = normalize_delay(settings.visibleMinDelayMs, 350);
  const visibleMaxDelayMs = normalize_delay(settings.visibleMaxDelayMs, 2500);
  const idleStepMs = normalize_delay(settings.idleStepMs, 225);
  const errorDelayMs = normalize_delay(settings.errorDelayMs, 3000);
  const pauseWhenHidden = settings.pauseWhenHidden !== false;
  const staleAfterFactor = normalize_delay(settings.staleAfterFactor, 2.4);
  const staleCriticalFactor = normalize_delay(settings.staleCriticalFactor, 4.0);

  let timerId = null;
  let lastTimestamp = null;
  let lastUpdateAt = null;
  let isPending = false;
  let isStopped = false;
  let isManuallyPaused = false;
  let unchangedCount = 0;
  let nextDelayMs = visibleMinDelayMs;
  let estimatedCadenceMs = null;
  let lastTimestampValueMs = null;
  let lastCycleMode = 'waiting';
  let lastCycleLabel = 'Waiting';
  let lastCycleDetail = 'CPI unavailable';

  function get_cpi_detail() {
    if (!Number.isFinite(estimatedCadenceMs)) {
      return 'CPI unavailable';
    }

    return 'CPI ' + format_poll_delay(estimatedCadenceMs);
  }

  function get_tuned_delay() {
    if (!Number.isFinite(estimatedCadenceMs)) {
      return visibleMinDelayMs;
    }

    return clamp_number(Math.round(estimatedCadenceMs * 0.35), visibleMinDelayMs, visibleMaxDelayMs);
  }

  function get_idle_step() {
    if (!Number.isFinite(estimatedCadenceMs)) {
      return idleStepMs;
    }

    return Math.max(idleStepMs, Math.round(estimatedCadenceMs * 0.18));
  }

  function get_stale_threshold() {
    if (!Number.isFinite(estimatedCadenceMs)) {
      return 5000;
    }

    return Math.max(2500, Math.round(estimatedCadenceMs * staleAfterFactor));
  }

  function get_stale_critical_threshold() {
    if (!Number.isFinite(estimatedCadenceMs)) {
      return 9000;
    }

    return Math.max(5000, Math.round(estimatedCadenceMs * staleCriticalFactor));
  }

  function get_frame_age() {
    if (!lastUpdateAt) {
      return null;
    }

    return Date.now() - lastUpdateAt;
  }

  function refresh_staleness_state() {
    const frameAgeMs = get_frame_age();
    if (!frameAgeMs || isManuallyPaused || document.hidden && pauseWhenHidden) {
      return;
    }

    if (frameAgeMs >= get_stale_critical_threshold()) {
      render_panel('stale', 'Offline', get_cpi_detail(), nextDelayMs);
      return;
    }

    if (lastCycleMode === 'stale' && frameAgeMs < get_stale_threshold()) {
      render_panel('live', 'Live', get_cpi_detail(), nextDelayMs);
    }
  }

  function update_estimated_cadence(timestamp) {
    const parsedTimestamp = parse_timestamp_value(timestamp);
    if (!Number.isFinite(parsedTimestamp)) {
      return;
    }

    if (Number.isFinite(lastTimestampValueMs)) {
      const delta = parsedTimestamp - lastTimestampValueMs;
      if (delta > 0) {
        estimatedCadenceMs = Number.isFinite(estimatedCadenceMs) ? Math.round((estimatedCadenceMs * 0.65) + (delta * 0.35)) : delta;
      }
    }

    lastTimestampValueMs = parsedTimestamp;
  }

  function clear_timer() {
    if (timerId !== null) {
      window.clearTimeout(timerId);
      timerId = null;
    }
  }

  function render_panel(mode, label, detail, delayOverride) {
    if (!panel) {
      lastCycleMode = mode;
      lastCycleLabel = label;
      lastCycleDetail = detail;
      return;
    }

    lastCycleMode = mode;
    lastCycleLabel = label;
    lastCycleDetail = detail;
    panel.setMode(mode, label, detail);
    panel.setLastUpdated(lastUpdateAt);
    panel.setPaused(isManuallyPaused);
    panel.setNextPoll(delayOverride);
  }

  function schedule_next(delayMs) {
    clear_timer();

    if (isStopped) {
      return;
    }

    if (isManuallyPaused) {
      render_panel('paused', 'Paused', 'Manual pause is active', NaN);
      return;
    }

    if (document.hidden && pauseWhenHidden) {
      render_panel('paused', 'Paused', 'Tab hidden; polling suspended', NaN);
      return;
    }

    nextDelayMs = delayMs;
    if (panel) {
      panel.setPaused(isManuallyPaused);
      panel.setLastUpdated(lastUpdateAt);
      panel.setNextPoll(delayMs);
    }
    refresh_staleness_state();
    timerId = window.setTimeout(run_cycle, delayMs);
  }

  function run_cycle() {
    if (isStopped || isManuallyPaused) {
      return;
    }

    if (document.hidden && pauseWhenHidden) {
      schedule_next(NaN);
      return;
    }

    if (isPending) {
      schedule_next(visibleMinDelayMs);
      return;
    }

    isPending = true;
    render_panel(lastUpdateAt ? 'live' : 'offline', lastUpdateAt ? 'Live' : 'Offline', get_cpi_detail(), nextDelayMs);

    request_text(timestampUrl)
      .then(function (timestamp) {
        update_estimated_cadence(timestamp);

        if (timestamp === lastTimestamp) {
          unchangedCount += 1;
          nextDelayMs = Math.min(get_tuned_delay() + (unchangedCount * get_idle_step()), visibleMaxDelayMs);

          if (lastUpdateAt && get_frame_age() >= get_stale_threshold()) {
            render_panel('stale', 'Offline', get_cpi_detail(), nextDelayMs);
            return;
          }

          render_panel(lastUpdateAt ? 'live' : 'offline', lastUpdateAt ? 'Live' : 'Offline', get_cpi_detail(), nextDelayMs);
          return;
        }

        lastTimestamp = timestamp;
        unchangedCount = 0;
        return Promise.resolve(settings.onRefresh ? settings.onRefresh(timestamp) : null)
          .then(function () {
            lastUpdateAt = Date.now();
            nextDelayMs = get_tuned_delay();
            render_panel('live', 'Live', get_cpi_detail(), nextDelayMs);
          });
      })
      .catch(function (error) {
        nextDelayMs = errorDelayMs;
        if (typeof settings.onError === 'function') {
          settings.onError(error);
        }
        render_panel('offline', 'Offline', get_cpi_detail(), nextDelayMs);
      })
      .finally(function () {
        isPending = false;
        if (!isStopped) {
          schedule_next(nextDelayMs);
        }
      });
  }

  function handle_visibility_change() {
    if (isStopped) {
      return;
    }

    if (document.hidden && pauseWhenHidden) {
      clear_timer();
      render_panel('paused', 'Paused', 'Tab hidden; polling suspended', NaN);
      return;
    }

    if (!isManuallyPaused) {
      clear_timer();
      nextDelayMs = get_tuned_delay();
      run_cycle();
    }
  }

  function set_manual_pause(isPaused) {
    isManuallyPaused = isPaused;
    clear_timer();

    if (isPaused) {
      render_panel('paused', 'Paused', 'Manual pause is active', NaN);
      return;
    }

    nextDelayMs = get_tuned_delay();
    run_cycle();
  }

  if (panel) {
    panel.bindToggle(function () {
      set_manual_pause(!isManuallyPaused);
    });
  }

  document.addEventListener('visibilitychange', handle_visibility_change);

  return {
    start: function () {
      isStopped = false;
      nextDelayMs = 0;
      render_panel(lastUpdateAt ? 'live' : 'offline', lastUpdateAt ? 'Live' : 'Offline', get_cpi_detail(), nextDelayMs);
      schedule_next(0);
    },
    stop: function () {
      isStopped = true;
      clear_timer();
      document.removeEventListener('visibilitychange', handle_visibility_change);
    },
    pause: function () {
      set_manual_pause(true);
    },
    resume: function () {
      set_manual_pause(false);
    },
    triggerNow: function () {
      if (isStopped || isManuallyPaused) {
        return;
      }
      clear_timer();
      nextDelayMs = 0;
      run_cycle();
    },
    getLastTimestamp: function () {
      return lastTimestamp;
    },
    getEstimatedCadenceMs: function () {
      return estimatedCadenceMs;
    }
  };
}

function bind_plot_resize(plotId) {
  const plotElement = document.getElementById(plotId);
  if (!plotElement || typeof Plotly === 'undefined' || !Plotly.Plots || typeof Plotly.Plots.resize !== 'function') {
    return null;
  }

  let resizeTimer = null;

  function queue_resize() {
    if (resizeTimer !== null) {
      window.clearTimeout(resizeTimer);
    }

    resizeTimer = window.setTimeout(function () {
      if (document.body.contains(plotElement)) {
        Plotly.Plots.resize(plotElement);
      }
    }, 100);
  }

  if (typeof ResizeObserver !== 'undefined') {
    const observer = new ResizeObserver(queue_resize);
    if (plotElement.parentElement) {
      observer.observe(plotElement.parentElement);
    }
    observer.observe(plotElement);
    return {
      disconnect: function () {
        observer.disconnect();
        if (resizeTimer !== null) {
          window.clearTimeout(resizeTimer);
        }
      }
    };
  }

  window.addEventListener('resize', queue_resize);
  return {
    disconnect: function () {
      window.removeEventListener('resize', queue_resize);
      if (resizeTimer !== null) {
        window.clearTimeout(resizeTimer);
      }
    }
  };
}