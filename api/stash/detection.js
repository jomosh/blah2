const http = require('http');

var time = 300;
var map = [];
var timestamp = [];
var delay = [];
var doppler = [];
var detection = '';
var ts = '';
var output = [];
var pollInterval = null;
const DEFAULT_API_PORT = 3000;
var options_timestamp = {
  host: '127.0.0.1',
  path: '/api/timestamp',
  port: null
};
var options_detection = {
  host: '127.0.0.1',
  path: '/api/detection',
  port: null
};

function request_with_error_handling(options, onResponse) {
  const req = http.get(options, onResponse);
  req.on('error', (err) => {
    console.error(`stash detection request failed (${options.path}): ${err.code || err.message}`);
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
        request_with_error_handling(options_detection, function(res) {
          let body_map = '';
          res.setEncoding('utf8');
          res.on('data', (chunk) => {
            body_map += chunk;
          });
          res.on('end', () => {
            try {
              detection = JSON.parse(body_map);
              map.push(detection);
              for (i = 0; i < map.length; i++)
              {
                if ((ts - map[i].timestamp)/1000 > time)
                {
                  map.shift();
                }
                else
                {
                  break;
                }
              }
              delay = [];
              doppler = [];
              timestamp = [];
              snr = [];
              for (var i = 0; i < map.length; i++)
              {
                for (var j = 0; j < map[i].delay.length; j++)
                {
                  delay.push(map[i].delay[j]);
                  doppler.push(map[i].doppler[j]);
                  snr.push(map[i].snr[j]);
                  timestamp.push(map[i].timestamp);
                }
              }
              output = {
                timestamp: timestamp,
                delay: delay,
                doppler: doppler,
                snr: snr
              };
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
  options_detection.port = apiPort;
  if (pollInterval === null) {
    pollInterval = setInterval(update_data, 100);
  }
}

function get_data() {
  return output;
};

module.exports.get_data_detection = get_data;
module.exports.init = init;