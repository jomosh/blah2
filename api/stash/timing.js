const http = require('http');

var nCpi = 20;
var ts = '';
var cpi = [];
var output = {};
var pollInterval = null;
const DEFAULT_API_PORT = 3000;
var options_timestamp = {
  host: '127.0.0.1',
  path: '/api/timestamp',
  port: null
};
var options_iqdata = {
  host: '127.0.0.1',
  path: '/api/timing',
  port: null
};

function request_with_error_handling(options, onResponse) {
  const req = http.get(options, onResponse);
  req.on('error', (err) => {
    console.error(`stash timing request failed (${options.path}): ${err.code || err.message}`);
  });
}

function update_data(callback) {
  request_with_error_handling(options_timestamp, function (res) {
    res.setEncoding('utf8');
    res.on('data', function (body) {
      if (ts != body) {
        ts = body;
        request_with_error_handling(options_iqdata, function (res) {
          let body_map = '';
          res.setEncoding('utf8');
          res.on('data', (chunk) => {
            body_map += chunk;
          });
          res.on('end', () => {
            try {
              cpi = JSON.parse(body_map);
              keys = Object.keys(cpi);
              keys = keys.filter(item => item !== "uptime");
              keys = keys.filter(item => item !== "nCpi");
              for (i = 0; i < keys.length; i++) {
                if (!(keys[i] in output)) {
                  output[keys[i]] = [];
                }
                output[keys[i]].push(cpi[keys[i]]);
                if (output[keys[i]].length > nCpi) {
                  output[keys[i]].shift();
                }
              }
            } catch (e) {
              console.error(e.message);
            }
          });
        });
      }
    });
  });
}

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
}

module.exports.get_data_timing = get_data;
module.exports.init = init;
