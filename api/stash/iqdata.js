const http = require('http');

var nCpi = 20;
var spectrum = [];
frequency = [];
var timestamp = [];
var ts = '';
var output = [];
var pollInterval = null;
const DEFAULT_API_PORT = 3000;
var options_timestamp = {
  host: '127.0.0.1',
  path: '/api/timestamp',
  port: null
};
var options_iqdata = {
  host: '127.0.0.1',
  path: '/api/iqdata',
  port: null
};

function request_with_error_handling(options, onResponse) {
  const req = http.get(options, onResponse);
  req.on('error', (err) => {
    console.error(`stash iqdata request failed (${options.path}): ${err.code || err.message}`);
  });
}

function update_data() {

  // check if timestamp is updated
  request_with_error_handling(options_timestamp, function(res) {
    res.setEncoding('utf8');
    res.on('data', function (body) {
      if (ts != body)
      {
        ts = body;
        request_with_error_handling(options_iqdata, function(res) {
          let body_map = '';
          res.setEncoding('utf8');
          res.on('data', (chunk) => {
            body_map += chunk;
          });
          res.on('end', () => {
            try {
              output = JSON.parse(body_map);
              // spectrum
              spectrum.push(output.spectrum);
              if (spectrum.length > nCpi) {
                spectrum.shift();
              }
              output.spectrum = spectrum;
              // frequency
              frequency.push(output.frequency);
              if (frequency.length > nCpi) {
                frequency.shift();
              }
              output.frequency = frequency;
              // timestamp
              timestamp.push(output.timestamp);
              if (timestamp.length > nCpi) {
                timestamp.shift();
              }
              output.timestamp = timestamp;
            } catch (e) {
              console.error(e.message);
            }
          });
        });
      }
    });
  });

};

function init(config) {
  const apiPort = config && config.network && config.network.ports ? config.network.ports.api : DEFAULT_API_PORT;
  options_timestamp.port = apiPort;
  options_iqdata.port = apiPort;
  if (pollInterval === null) {
    pollInterval = setInterval(update_data, 100);
  }
}

function get_data() {
  return output;
};

module.exports.get_data_iqdata = get_data;
module.exports.init = init;