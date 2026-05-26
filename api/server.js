const express = require('express');
const net = require("net");
const fs = require('fs');
const yaml = require('js-yaml');
const dns = require('dns');

// parse config file
var config;
try {
  const file = process.argv[2];
  if (!file) {
    console.error('Missing config path argument. Usage: node server.js <config.yml>');
    process.exit(1);
  }

  config = yaml.load(fs.readFileSync(file, 'utf8'));
} catch (e) {
  console.error('Error reading or parsing the YAML file:', e);
  process.exit(1);
}

if (!config || !config.network || !config.network.ports || !config.network.ip) {
  console.error('Invalid config: expected network.ip and network.ports to be defined.');
  process.exit(1);
}

var stash_map = require('./stash/maxhold.js');
var stash_detection = require('./stash/detection.js');
var stash_iqdata = require('./stash/iqdata.js');
var stash_timing = require('./stash/timing.js');

stash_map.init(config);
stash_detection.init(config);
stash_iqdata.init(config);
stash_timing.init(config);

// constants
const PORT = config.network.ports.api;
const HOST = config.network.ip;
var map = '';
var detection = '';
var track = '';
var adsb = '{}';
var timestamp = '';
var timing = '';
var iqdata = '';
var capture = false;

// api server
const app = express();
// header on all requests
app.use(function(req, res, next) {
  res.header("Access-Control-Allow-Origin", "*");
  res.header('Cache-Control', 'private, no-cache, no-store, must-revalidate');
  res.header('Expires', '-1');
  res.header('Pragma', 'no-cache');
  next();
});
app.get('/', (req, res) => {
  res.send('Hello World');
});
app.get('/api/map', (req, res) => {
  res.send(map);
});
app.get('/api/detection', (req, res) => {
  res.send(detection);
});
app.get('/api/tracker', (req, res) => {
  res.send(track);
});
app.get('/api/adsb', (req, res) => {
  res.header('Content-Type', 'application/json');
  res.send(adsb);
});
app.get('/api/timestamp', (req, res) => {
  res.send(timestamp);
});
app.get('/api/timing', (req, res) => {
  res.send(timing);
});
app.get('/api/iqdata', (req, res) => {
  res.send(iqdata);
});
app.get('/api/config', (req, res) => {
  res.send(config);
});
app.get('/api/adsb2dd', (req, res) => {
  if (config.truth.adsb.enabled == true) {
    const api_url = "http://" + config.truth.adsb.adsb2dd + "/api/dd";
    const api_query =
      api_url +
      "?rx=" + config.location.rx.latitude + "," +
      config.location.rx.longitude + "," +
      config.location.rx.altitude +
      "&tx=" + config.location.tx.latitude + "," +
      config.location.tx.longitude + "," +
      config.location.tx.altitude +
      "&fc=" + (config.capture.fc / 1000000) +
      "&server=" + "http://" + config.truth.adsb.tar1090;
  const jsonResponse = {
    url: api_query
  };
  res.json(jsonResponse);
  }
  else {
    res.status(400).end();
  }
});

// stash API
app.get('/stash/map', (req, res) => {
  res.send(stash_map.get_data_map());
});
app.get('/stash/detection', (req, res) => {
  res.send(stash_detection.get_data_detection());
});
app.get('/stash/iqdata', (req, res) => {
  res.send(stash_iqdata.get_data_iqdata());
});
app.get('/stash/timing', (req, res) => {
  res.send(stash_timing.get_data_timing());
});

function is_local_ip(ip) {
  const addr = ip.startsWith('::ffff:') ? ip.slice(7) : ip;
  if (addr === '::1') return true;
  const parts = addr.split('.').map(Number);
  if (parts.length !== 4 || parts.some(p => !Number.isInteger(p) || p < 0 || p > 255)) return false;
  if (parts[0] === 127) return true;
  if (parts[0] === 10) return true;
  if (parts[0] === 172 && parts[1] >= 16 && parts[1] <= 31) return true;
  if (parts[0] === 192 && parts[1] === 168) return true;
  return false;
}

// read state of capture
app.get('/capture', (req, res) => {
  res.send(capture);
});
// toggle state of capture — localhost and RFC1918 only
app.get('/capture/toggle', (req, res) => {
  if (!req.socket.remoteAddress || !is_local_ip(req.socket.remoteAddress)) {
    res.status(403).end();
    return;
  }
  capture = !capture;
  res.send('{}');
});
app.listen(PORT, HOST, () => {
  console.log(`Running on http://${HOST}:${PORT}`);
});

const MAX_PENDING_BYTES = 8 * 1024 * 1024;

function createFramedTcpServer(port, onFrame) {
  const server = net.createServer((socket) => {
    let pending = '';

    socket.on('data', (msg) => {
      pending += msg.toString();
      if (pending.length > MAX_PENDING_BYTES) {
        console.error(`Closing connection on port ${port}: frame exceeded ${MAX_PENDING_BYTES} bytes without delimiter.`);
        pending = '';
        socket.destroy();
        return;
      }

      const frames = pending.split('\n');
      pending = frames.pop();

      for (let i = 0; i < frames.length; i++) {
        if (frames[i].length > 0) {
          onFrame(frames[i]);
        }
      }
    });

    socket.on('close', () => {
      console.log('Connection closed.');
    });

    socket.on('error', (err) => {
      console.error(`Socket error on port ${port}:`, err.message);
      if (!socket.destroyed) {
        socket.destroy();
      }
    });
  });

  server.on('error', (err) => {
    console.error(`TCP server error on port ${port}:`, err.message);
  });

  server.listen(port);
  return server;
}

createFramedTcpServer(config.network.ports.map, (frame) => {
  map = frame;
});

createFramedTcpServer(config.network.ports.detection, (frame) => {
  detection = frame;
});

createFramedTcpServer(config.network.ports.track, (frame) => {
  track = frame;
});

createFramedTcpServer(config.network.ports.adsb, (frame) => {
  adsb = frame;
});

createFramedTcpServer(config.network.ports.timestamp, (frame) => {
  timestamp = frame;
});

createFramedTcpServer(config.network.ports.timing, (frame) => {
  timing = frame;
});

createFramedTcpServer(config.network.ports.iqdata, (frame) => {
  iqdata = frame;
});

process.on('SIGTERM', () => {
  console.log('SIGTERM signal received.');
  process.exit(0);
});
