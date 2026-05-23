(function () {
  var controlRoot = null;
  var statusBadge = null;
  var toggleButton = null;
  var pollTimerId = null;
  var currentCaptureState = null;
  var togglePending = false;
  var pollInFlight = false;
  var pollDelayMs = 1000;

  function normalize_capture_state(value) {
    if (typeof value === 'boolean') {
      return value;
    }

    if (typeof value === 'number') {
      return value !== 0;
    }

    if (typeof value === 'string') {
      var normalized = value.trim().toLowerCase();
      if (normalized === 'true') {
        return true;
      }
      if (normalized === 'false') {
        return false;
      }
    }

    return null;
  }

  function render_capture_state(captureEnabled, statusText, statusClass) {
    if (!statusBadge || !toggleButton) {
      return;
    }

    currentCaptureState = captureEnabled;
    statusBadge.className = 'badge ' + statusClass;
    statusBadge.textContent = statusText;

    if (captureEnabled === true) {
      toggleButton.className = 'btn btn-sm btn-danger';
      toggleButton.textContent = 'Stop IQ Capture';
      toggleButton.setAttribute('aria-pressed', 'true');
    }
    else if (captureEnabled === false) {
      toggleButton.className = 'btn btn-sm btn-success';
      toggleButton.textContent = 'Start IQ Capture';
      toggleButton.setAttribute('aria-pressed', 'false');
    }
    else {
      toggleButton.className = 'btn btn-sm btn-outline-secondary';
      toggleButton.textContent = 'IQ Capture Unavailable';
      toggleButton.setAttribute('aria-pressed', 'false');
    }

    toggleButton.disabled = togglePending || captureEnabled === null;
  }

  function fetch_capture_state() {
    return $.get(build_api_url('/capture'))
      .done(function (data) {
        var captureEnabled = normalize_capture_state(data);
        if (captureEnabled === null) {
          render_capture_state(null, 'State Unknown', 'text-bg-warning');
          return;
        }

        render_capture_state(
          captureEnabled,
          captureEnabled ? 'Capture Started' : 'Capture Stopped',
          captureEnabled ? 'text-bg-danger' : 'text-bg-secondary'
        );
      })
      .fail(function () {
        render_capture_state(null, 'API Unreachable', 'text-bg-warning');
      });
  }

  function clear_capture_poll_timer() {
    if (pollTimerId !== null) {
      window.clearTimeout(pollTimerId);
      pollTimerId = null;
    }
  }

  function schedule_capture_poll(delayMs) {
    clear_capture_poll_timer();
    if (document.hidden) {
      return;
    }
    pollTimerId = window.setTimeout(run_capture_poll, delayMs);
  }

  function run_capture_poll() {
    if (document.hidden) {
      clear_capture_poll_timer();
      return;
    }

    if (pollInFlight) {
      schedule_capture_poll(pollDelayMs);
      return;
    }

    pollInFlight = true;
    fetch_capture_state().always(function () {
      pollInFlight = false;
      schedule_capture_poll(pollDelayMs);
    });
  }

  function toggle_capture_state() {
    if (togglePending || currentCaptureState === null) {
      return;
    }

    togglePending = true;
    toggleButton.disabled = true;
    statusBadge.className = 'badge text-bg-info';
    statusBadge.textContent = 'Updating...';

    $.getJSON(build_api_url('/capture/toggle'))
      .done(function () {
        fetch_capture_state().always(function () {
          togglePending = false;
          if (currentCaptureState !== null) {
            toggleButton.disabled = false;
          }
        });
      })
      .fail(function () {
        togglePending = false;
        render_capture_state(null, 'Toggle Failed', 'text-bg-warning');
      });
  }

  function init_capture_control() {
    controlRoot = document.querySelector('[data-iq-capture-control]');
    if (!controlRoot) {
      return;
    }

    statusBadge = controlRoot.querySelector('[data-iq-capture-status]');
    toggleButton = controlRoot.querySelector('[data-iq-capture-toggle]');
    if (!statusBadge || !toggleButton) {
      return;
    }

    toggleButton.addEventListener('click', toggle_capture_state);
    render_capture_state(null, 'Checking...', 'text-bg-secondary');
    fetch_capture_state().always(function () {
      schedule_capture_poll(pollDelayMs);
    });
    document.addEventListener('visibilitychange', run_capture_poll);
    window.addEventListener('beforeunload', function () {
      clear_capture_poll_timer();
      document.removeEventListener('visibilitychange', run_capture_poll);
    });
  }

  $(init_capture_control);
})();
