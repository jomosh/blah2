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