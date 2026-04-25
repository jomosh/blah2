(function () {
  var controlRoot = null;
  var statusBadge = null;
  var toggleButton = null;
  var pollId = null;
  var currentCaptureState = null;
  var togglePending = false;

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
    fetch_capture_state();
    pollId = window.setInterval(fetch_capture_state, 1000);
    window.addEventListener('beforeunload', function () {
      if (pollId !== null) {
        window.clearInterval(pollId);
      }
    });
  }

  $(init_capture_control);
})();
