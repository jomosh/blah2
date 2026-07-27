var timestamp = -1;
var nRows = 3;
var range_x = [];
var range_y = [];
var viewMode = 'ref'; // 'ref', 'surv', 'both'

// setup API
var urlTimestamp = build_api_url('/api/timestamp');
var urlMap = build_api_url('/stash/iqdata');

// setup plotly
var layout = {
  autosize: true,
  margin: {
    l: 50,
    r: 50,
    b: 50,
    t: 30,
    pad: 0
  },
  hoverlabel: {
    namelength: 0
  },
  plot_bgcolor: "rgba(0,0,0,0)",
  paper_bgcolor: "rgba(0,0,0,0)",
  annotations: [],
  displayModeBar: false,
  xaxis: {
    title: {
      text: 'Frequency (MHz)',
      font: {
        size: 24
      }
    },
    ticks: '',
    side: 'bottom'
  },
  yaxis: {
    title: {
      text: 'Timestamp',
      font: {
        size: 24
      }
    },
    ticks: '',
    ticksuffix: ' ',
    autosize: false,
    categoryorder: "total descending"
  }
};
var config = {
  responsive: true,
  displayModeBar: false
};

// setup plotly data
var data = [
  {
    z: [[0, 0, 0], [0, 0, 0], [0, 0, 0]],
    colorscale: 'Jet',
    type: 'heatmap'
  }
];
var detection = [];

Plotly.newPlot('data', data, layout, config);

// callback function
var intervalId = window.setInterval(function () {

  // check if timestamp is updated
  $.get(urlTimestamp, function () { })

    .done(function (timestampResp) {
      if (timestamp != timestampResp) {
        timestamp = timestampResp;

        // get new spectrum data
        $.getJSON(urlMap, function () { })
          .done(function (data) {

            // convert frequency from kHz to MHz for display
            var freqMhz = null;
            if (data.frequency && data.frequency.length > 0) {
              freqMhz = [];
              for (var i = 0; i < data.frequency.length; i++) {
                freqMhz.push(data.frequency[i] / 1000);
              }
            }

            // timestamp posix to js
            for (var i = 0; i < data.timestamp.length; i++) {
              data.timestamp[i] = new Date(data.timestamp[i]);
            }

            // pick the spectrum based on view mode
            var spectrumZ;
            if (viewMode === 'surv' && data.spectrumSurv && data.spectrumSurv.length > 0) {
              spectrumZ = data.spectrumSurv;
            } else if (viewMode === 'both' && data.spectrumSurv && data.spectrumSurv.length > 0) {
              // interleave: each row becomes two rows (ref then surv)
              spectrumZ = [];
              var timestampsBoth = [];
              var suffix = '';
              for (var j = 0; j < data.spectrum.length && j < data.spectrumSurv.length; j++) {
                spectrumZ.push(data.spectrum[j]);
                spectrumZ.push(data.spectrumSurv[j]);
                // duplicate timestamp with suffix
                var t = new Date(data.timestamp[j].getTime());
                timestampsBoth.push(data.timestamp[j].toISOString().replace('T', ' ').slice(0, 19) + ' Ref');
                timestampsBoth.push(data.timestamp[j].toISOString().replace('T', ' ').slice(0, 19) + ' Surv');
              }
              data.timestamp = timestampsBoth;
            } else {
              spectrumZ = data.spectrum;
            }

            // Get number of rows
            var currentRows = Array.isArray(spectrumZ) ? spectrumZ.length : 0;

            // case draw new plot
            if (currentRows !== nRows) {
              nRows = currentRows;
              var trace1 = {
                  y: data.timestamp,
                  z: spectrumZ,
                  colorscale: 'Jet',
                  zauto: false,
                  type: 'heatmap'
              };

              var titleText = 'Reference Spectrum';
              if (viewMode === 'surv') titleText = 'Surveillance Spectrum';
              else if (viewMode === 'both') titleText = 'Spectrum (Ref + Surv interleaved)';

              var newLayout = $.extend(true, {}, layout);
              // Don't add a title element since original doesn't have one
              Plotly.newPlot('data', [trace1], newLayout, config);
            }
            // case update plot
            else {
              var trace_update = {
                y: [data.timestamp],
                z: [spectrumZ]
              };
              Plotly.update('data', trace_update);
            }

          })
          .fail(function () {
          })
          .always(function () {
          });
      }
    })
    .fail(function () {
    })
    .always(function () {
    });
}, 100);