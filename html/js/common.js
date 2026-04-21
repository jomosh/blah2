function is_localhost(ip) {
  
  if (ip === 'localhost') {
    return true;
  }
  
  const localRanges = ['127.0.0.1', '192.168.0.0/16', '10.0.0.0/8', '172.16.0.0/12'];

  const ipToInt = ip => ip.split('.').reduce((acc, octet) => (acc << 8) + +octet, 0) >>> 0;

  return localRanges.some(range => {
    const [rangeStart, rangeSize = 32] = range.split('/');
    const start = ipToInt(rangeStart);
    const end = (start | (1 << (32 - +rangeSize))) >>> 0;
    return ipToInt(ip) >= start && ipToInt(ip) <= end;
  });

}

function get_api_base_url() {
  const host = window.location.hostname;
  const query = new URLSearchParams(window.location.search);
  const apiBase = query.get('api_base');
  const apiPort = query.get('api_port');

  if (apiBase && apiBase.length > 0) {
    return apiBase.replace(/\/$/, '');
  }

  if (apiPort && /^\d+$/.test(apiPort)) {
    return '//' + host + ':' + apiPort;
  }

  if (is_localhost(host)) {
    return '//' + host + ':3000';
  }

  return '//' + host;
}

function build_api_url(path) {
  if (/^https?:\/\//.test(path) || /^\/\//.test(path)) {
    return path;
  }
  const normalizedPath = path.startsWith('/') ? path : '/' + path;
  return get_api_base_url() + normalizedPath;
}