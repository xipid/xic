/**
 * @file HTTP.cpp
 * @brief HTTP/1.1 transport and DNS-over-HTTPS implementation for Xi.
 *
 * Only compiled when XI_TLS_ENABLED is defined by CMake (mbedTLS found).
 */

#include "../../include/Resource/HTTP.hpp"

#ifdef XI_TLS_ENABLED

#include <arpa/inet.h>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Resource {

// =========================================================================
// DNSResolver — Singleton
// =========================================================================

static DNSResolver *_dnsInstance = nullptr;

DNSResolver &DNSResolver::instance() {
  if (!_dnsInstance)
    _dnsInstance = new DNSResolver();
  return *_dnsInstance;
}

DNSResolver::DNSResolver() {}
DNSResolver::~DNSResolver() {}

String DNSResolver::resolve(const String &url) {
  // Parse the URL to extract hostname and port
  Path p(url);
  String hostname = p.address().toString(true);

  // Strip port from hostname if present
  long long colonIdx = hostname.find(":");
  String bareHost = hostname;
  String portStr;
  if (colonIdx != -1) {
    bareHost = hostname.substring(0, (usz)colonIdx);
    portStr = hostname.substring((usz)colonIdx + 1);
  }

  // Check if already an IP address (all digits and dots)
  bool isIP = true;
  for (usz i = 0; i < bareHost.size(); ++i) {
    char c = (char)bareHost.data()[i];
    if ((c < '0' || c > '9') && c != '.') {
      isIP = false;
      break;
    }
  }
  if (isIP)
    return url;

  // Check cache
  String *cached = _cache.get(bareHost);
  if (cached) {
    // Reconstruct URL with resolved IP
    String result = p.protocol() + "://" + *cached;
    if (portStr.size() > 0) {
      result += ":";
      result += portStr;
    }
    String pathStr = p.toString(true, false, true);
    if (pathStr.size() > 0 && pathStr.data()[0] != '/')
      result += "/";
    result += pathStr;
    return result;
  }

  // Blocking resolve
  String ip = _resolveBlocking(bareHost);
  if (ip.size() > 0) {
    _cache.set(bareHost, ip);
    String result = p.protocol() + "://" + ip;
    if (portStr.size() > 0) {
      result += ":";
      result += portStr;
    }
    String pathStr = p.toString(true, false, true);
    if (pathStr.size() > 0 && pathStr.data()[0] != '/')
      result += "/";
    result += pathStr;
    return result;
  }

  return url; // Failed, return original
}

bool DNSResolver::has(const String &hostname) {
  if (_cache.has(hostname))
    return true;

  // If not inflight, start async lookup
  if (!_inflight.has(hostname)) {
    _inflight.set(hostname, true);
    _startAsyncLookup(hostname);
  }

  return false;
}

String DNSResolver::ip(const String &hostname) const {
  const String *cached = _cache.get(hostname);
  if (cached)
    return *cached;
  return String();
}

void DNSResolver::update() {
  // Pump any pending UDP-based DNS responses
  // In a full implementation this would check non-blocking sockets.
  // For the current implementation, async lookups use the blocking path
  // in a deferred manner — the next call to has() after _resolveBlocking
  // completes will return true.
}

String DNSResolver::_resolveBlocking(const String &hostname) {
  // Use plain UDP DNS to 1.1.1.1:53 for bootstrap resolution.
  return _resolveUDP(hostname);
}

void DNSResolver::_startAsyncLookup(const String &hostname) {
  // For the initial implementation, we do a blocking resolve and cache it.
  // A fully async version would use a non-blocking UDP socket + update().
  String resolved = _resolveUDP(hostname);
  if (resolved.size() > 0) {
    _cache.set(hostname, resolved);
  }
  _inflight.remove(hostname);
}

// -------------------------------------------------------------------------
// Plain UDP DNS query to 1.1.1.1:53
// -------------------------------------------------------------------------

String DNSResolver::_resolveUDP(const String &hostname) {
  int sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0)
    return String();

  struct sockaddr_in dns_addr;
  memset(&dns_addr, 0, sizeof(dns_addr));
  dns_addr.sin_family = AF_INET;
  dns_addr.sin_port = htons(53);
  inet_pton(AF_INET, "1.1.1.1", &dns_addr.sin_addr);

  // Build DNS query packet
  u8 packet[512];
  memset(packet, 0, sizeof(packet));

  // Header
  u16 txid = (u16)(millis() & 0xFFFF);
  packet[0] = (u8)(txid >> 8);
  packet[1] = (u8)(txid & 0xFF);
  packet[2] = 0x01; // RD = 1 (recursion desired)
  packet[3] = 0x00;
  packet[4] = 0x00;
  packet[5] = 0x01; // QDCOUNT = 1
  // ANCOUNT, NSCOUNT, ARCOUNT = 0

  // Question section — encode hostname as DNS labels
  usz offset = 12;
  const u8 *hn = hostname.data();
  usz hn_len = hostname.size();
  usz labelStart = 0;

  for (usz i = 0; i <= hn_len; ++i) {
    if (i == hn_len || hn[i] == '.') {
      usz labelLen = i - labelStart;
      if (labelLen > 63 || offset + labelLen + 1 >= 500) {
        ::close(sockfd);
        return String();
      }
      packet[offset++] = (u8)labelLen;
      for (usz j = 0; j < labelLen; ++j)
        packet[offset++] = hn[labelStart + j];
      labelStart = i + 1;
    }
  }
  packet[offset++] = 0x00; // Root label

  // QTYPE = A (1)
  packet[offset++] = 0x00;
  packet[offset++] = 0x01;
  // QCLASS = IN (1)
  packet[offset++] = 0x00;
  packet[offset++] = 0x01;

  usz queryLen = offset;

  // Send
  ssize_t sent = ::sendto(sockfd, packet, queryLen, 0,
                           (struct sockaddr *)&dns_addr, sizeof(dns_addr));
  if (sent < 0) {
    ::close(sockfd);
    return String();
  }

  // Set timeout for blocking receive (2 seconds)
  struct timeval tv;
  tv.tv_sec = 2;
  tv.tv_usec = 0;
  setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  u8 resp[512];
  ssize_t recvLen = ::recv(sockfd, resp, sizeof(resp), 0);
  ::close(sockfd);

  if (recvLen < 12)
    return String();

  // Parse response
  u16 flags = ((u16)resp[2] << 8) | resp[3];
  u16 ancount = ((u16)resp[6] << 8) | resp[7];

  // Check QR bit and RCODE
  if (!(flags & 0x8000) || (flags & 0x000F) != 0)
    return String();

  if (ancount == 0)
    return String();

  // Skip question section
  usz pos = 12;
  // Skip QNAME
  while (pos < (usz)recvLen && resp[pos] != 0) {
    if ((resp[pos] & 0xC0) == 0xC0) {
      pos += 2;
      goto question_done;
    }
    pos += 1 + resp[pos];
  }
  pos++; // Skip null terminator
question_done:
  pos += 4; // Skip QTYPE + QCLASS

  // Parse answer records
  for (u16 i = 0; i < ancount && pos < (usz)recvLen; ++i) {
    // Skip NAME (may be compressed)
    if ((resp[pos] & 0xC0) == 0xC0) {
      pos += 2;
    } else {
      while (pos < (usz)recvLen && resp[pos] != 0)
        pos += 1 + resp[pos];
      pos++;
    }

    if (pos + 10 > (usz)recvLen)
      break;

    u16 rtype = ((u16)resp[pos] << 8) | resp[pos + 1];
    // u16 rclass = ((u16)resp[pos + 2] << 8) | resp[pos + 3];
    // u32 ttl = ...;
    u16 rdlength = ((u16)resp[pos + 8] << 8) | resp[pos + 9];
    pos += 10;

    if (rtype == 1 && rdlength == 4 && pos + 4 <= (usz)recvLen) {
      // A record — IPv4
      char ipStr[20];
      snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", resp[pos], resp[pos + 1],
               resp[pos + 2], resp[pos + 3]);
      return String(ipStr);
    }

    pos += rdlength;
  }

  return String();
}

// =========================================================================
// HTTPBind
// =========================================================================

HTTPBind::HTTPBind(const NumericalAddress &address) : TLSBind(address) { name = "HTTPBind"; }

HTTPBind::~HTTPBind() {
  // Clean up HTTPClient allocations
  for (auto &kv : _httpClients) {
    delete kv.value;
  }
}

void HTTPBind::onPacket(Func<void(HTTPClient &, String)> cb) {
  _httpPacketListener = Move(cb);
}

HTTPClient *HTTPBind::_getOrCreateClient(const String &key) {
  HTTPClient **existing = _httpClients.get(key);
  if (existing)
    return *existing;
  HTTPClient *cli = new HTTPClient();
  cli->id = key;
  _httpClients.set(key, cli);
  return cli;
}

void HTTPBind::fetch(HTTPClient &cli, const String &url) {
  DNSResolver &resolv = DNSResolver::instance();

  // Propagate CAs
  for (usz i = 0; i < resolv.ca.size(); ++i) {
    load(resolv.ca[i].data, resolv.ca[i].type);
  }

  // Resolve DNS (blocking)
  String resolvedUrl = resolv.resolve(url);
  Path p(resolvedUrl);

  String host = p.address().toString(true);
  long long colonIdx = host.find(":");
  String bareHost = host;
  int destPort = 80;

  if (colonIdx != -1) {
    bareHost = host.substring(0, (usz)colonIdx);
    destPort = (int)parseLong(host.substring((usz)colonIdx + 1));
  } else {
    destPort = p.port();
  }

  if (p.protocol() == "https" && destPort == 80)
    destPort = 443;

  cli.id = bareHost + ":" + String((long long)destPort);
  cli.url = resolvedUrl;
  cli.method = "GET";

  // Store in our map
  HTTPClient *heap = _getOrCreateClient(cli.id);
  *heap = cli; // copy state

  // Initiate TLS/TCP connection
  if (p.protocol() == "https") {
    connect(bareHost, destPort);
  } else {
    // Plain HTTP — still use the TCP layer from TCPBind
    connect(bareHost, destPort);
  }
}

void HTTPBind::get(HTTPClient &cli, const String &url) {
  cli.method = "GET";
  fetch(cli, url);
}

void HTTPBind::method(HTTPClient &cli, const String &m) { cli.method = m; }

void HTTPBind::header(HTTPClient &cli, const String &key,
                       const String &value) {
  String line = key + ": " + value + "\r\n";
  Path target(cli.id);
  TLSBind::send(line, target);
  cli.headers.set(key, value);
}

void HTTPBind::header(HTTPClient &cli, const Map<String, String> &headers) {
  for (auto &kv : headers) {
    header(cli, kv.key, kv.value);
  }
}

void HTTPBind::status(HTTPClient &cli, int code, const String &message) {
  cli.status = code;
  cli.statusMessage = message;
  String line = "HTTP/1.1 " + String((long long)code) + " " + message + "\r\n";
  Path target(cli.id);
  TLSBind::send(line, target);
}

void HTTPBind::begin(HTTPClient &cli) {
  // Send blank line to signal end of headers / start of body
  Path target(cli.id);
  TLSBind::send(String("\r\n"), target);
  cli.began = true;
}

void HTTPBind::send(HTTPClient &cli, const String &data) {
  Path target(cli.id);
  TLSBind::send(data, target);
}

void HTTPBind::destroy(HTTPClient &cli) {
  // Close the underlying TLS session + TCP
  Path target(cli.id);
  TLSBind::send(String(), target); // Flush

  // Remove from tracking
  String key = cli.id;
  HTTPClient **existing = _httpClients.get(key);
  if (existing) {
    delete *existing;
    _httpClients.remove(key);
  }
  clients.remove(key);

  cli.end = true;
}

// -------------------------------------------------------------------------
// HTTP Parser — non-blocking, chunk-by-chunk
// -------------------------------------------------------------------------

void HTTPBind::_parseHeaderLine(HTTPClient *cli, const String &line) {
  if (line.isEmpty())
    return;

  // First line: "GET /path HTTP/1.1" or "HTTP/1.1 200 OK"
  if (!cli->_headersDone && cli->headers.size() == 0 &&
      cli->method.isEmpty() && cli->status == 0) {
    // Detect if it's a request or response
    if (line.size() > 5 && line.data()[0] == 'H' && line.data()[1] == 'T' &&
        line.data()[2] == 'T' && line.data()[3] == 'P') {
      // Response: "HTTP/1.1 200 OK"
      usz sp1 = 0;
      for (usz i = 0; i < line.size(); ++i) {
        if (line.data()[i] == ' ') {
          sp1 = i;
          break;
        }
      }
      usz sp2 = 0;
      for (usz i = sp1 + 1; i < line.size(); ++i) {
        if (line.data()[i] == ' ') {
          sp2 = i;
          break;
        }
      }
      if (sp1 > 0) {
        String codeStr = line.substring(sp1 + 1, sp2 > 0 ? sp2 : line.size());
        cli->status = parseLong(codeStr);
        if (sp2 > 0)
          cli->statusMessage = line.substring(sp2 + 1);
      }
    } else {
      // Request: "GET /path HTTP/1.1"
      usz sp1 = 0, sp2 = 0;
      for (usz i = 0; i < line.size(); ++i) {
        if (line.data()[i] == ' ') {
          if (sp1 == 0)
            sp1 = i;
          else {
            sp2 = i;
            break;
          }
        }
      }
      if (sp1 > 0) {
        cli->method = line.substring(0, sp1);
        cli->url = line.substring(sp1 + 1, sp2 > 0 ? sp2 : line.size());
      }
    }
    return;
  }

  // Regular header: "Key: Value"
  long long colonPos = line.find(": ");
  if (colonPos == -1)
    colonPos = line.find(":");
  if (colonPos != -1) {
    String key = line.substring(0, (usz)colonPos);
    usz valStart = (usz)colonPos + 1;
    if (valStart < line.size() && line.data()[valStart] == ' ')
      valStart++;
    String val = line.substring(valStart);
    cli->headers.set(key, val);

    // Check for transfer-encoding and content-length
    // Case-insensitive check
    bool isContentLength = true;
    const char *cl = "content-length";
    if (key.size() == 14) {
      for (usz i = 0; i < 14; ++i) {
        if (!ch_eq((char)key.data()[i], cl[i])) {
          isContentLength = false;
          break;
        }
      }
    } else {
      isContentLength = false;
    }
    if (isContentLength) {
      cli->_contentLength = parseLong(val);
    }

    bool isTE = true;
    const char *te = "transfer-encoding";
    if (key.size() == 17) {
      for (usz i = 0; i < 17; ++i) {
        if (!ch_eq((char)key.data()[i], te[i])) {
          isTE = false;
          break;
        }
      }
    } else {
      isTE = false;
    }
    if (isTE && val.find("chunked") != -1) {
      cli->_chunked = true;
    }
  }
}

void HTTPBind::_parseChunk(HTTPClient *cli, const String &raw) {
  if (!cli->_headersDone) {
    // Accumulate into header buffer
    cli->_headerBuf += raw;

    // Look for \r\n\r\n
    long long endOfHeaders = cli->_headerBuf.find("\r\n\r\n");
    if (endOfHeaders == -1)
      return; // Need more data

    // Parse all header lines
    String headerBlock = cli->_headerBuf.substring(0, (usz)endOfHeaders);
    usz lineStart = 0;
    for (usz i = 0; i < headerBlock.size(); ++i) {
      if (i + 1 < headerBlock.size() && headerBlock.data()[i] == '\r' &&
          headerBlock.data()[i + 1] == '\n') {
        String line = headerBlock.substring(lineStart, i);
        _parseHeaderLine(cli, line);
        lineStart = i + 2;
        i++; // skip \n
      }
    }
    // Last line (if no trailing \r\n)
    if (lineStart < headerBlock.size()) {
      _parseHeaderLine(cli, headerBlock.substring(lineStart));
    }

    cli->_headersDone = true;
    cli->began = true;

    // Extract body portion after headers
    String body =
        cli->_headerBuf.substring((usz)endOfHeaders + 4);
    cli->_headerBuf = String(); // Free

    if (body.size() > 0) {
      cli->_bodyReceived += (i64)body.size();
      if (_httpPacketListener.isValid())
        _httpPacketListener(*cli, body);
    }

    // Check if complete
    if (cli->_contentLength >= 0 &&
        cli->_bodyReceived >= cli->_contentLength) {
      cli->end = true;
      if (_httpPacketListener.isValid())
        _httpPacketListener(*cli, String());
    }
  } else {
    // Body data
    cli->_bodyReceived += (i64)raw.size();
    if (_httpPacketListener.isValid())
      _httpPacketListener(*cli, raw);

    if (cli->_contentLength >= 0 &&
        cli->_bodyReceived >= cli->_contentLength) {
      cli->end = true;
      if (_httpPacketListener.isValid())
        _httpPacketListener(*cli, String());
    }
  }
}

void HTTPBind::update() {
  // Set TLS-level listener to route into HTTP parser
  TLSBind::onPacket([this](String data) {
    HTTPClient *cli = _getOrCreateClient(lastSenderPath);
    _parseChunk(cli, data);
  });

  TLSBind::update();

  // For any client-side fetches, if the handshake just finished, send the request
  for (auto &kv : _httpClients) {
    HTTPClient *cli = kv.value;
    if (cli->_requestSent || cli->method.isEmpty())
      continue;

    // Find the TLS session for this client
    int cfd = -1;
    NetClient *nc = clients.get(cli->id);
    if (!nc)
      continue;
    cfd = (int)nc->lastSeen;

    TLSSession *s = _sessionByFd(cfd);
    if (s && s->handshakeDone) {
      // Send Request Line
      Path p(cli->url);
      String pathStr = p.toString(true, false, true);
      if (pathStr.isEmpty())
        pathStr = "/";
      String req = cli->method + " " + pathStr + " HTTP/1.1\r\n";
      req += "Host: " + p.address().toString(true) + "\r\n";
      req += "Connection: close\r\n"; // Keep it simple for now
      req += "Accept: */*\r\n";
      req += "User-Agent: Xi/1.0\r\n";
      req += "\r\n";

      TLSBind::send(req, Path(cli->id));
      cli->_requestSent = true;
    }
  }
}

} // namespace Resource

#endif // XI_TLS_ENABLED
