#ifndef XI_PATH_HPP
#define XI_PATH_HPP 1

#include "Array.hpp"
#include "Map.hpp"
#include "String.hpp"

namespace Xi {

class Hostname; // Forward declaration

class NumericalHostname : public Array<u64> {
public:
  NumericalHostname() {}
  NumericalHostname(const Hostname &hn);
};

class Hostname : public Array<String> {
private:
  static bool strIsNumeric(const String &s) {
    if (s.isEmpty())
      return false;
    for (usz i = 0; i < s.size(); i++) {
      if (s[i] < '0' || s[i] > '9')
        return false;
    }
    return true;
  }

public:
  Hostname() {}
  Hostname(const NumericalHostname &nhn);
  Hostname(const String &hn) {
    if (hn.isEmpty())
      return;

    // If it contains commas, it is already in our internal format
    if (hn.find(",") != -1) {
      Array<String> parts = hn.split(",");
      for (usz i = 0; i < parts.size(); i++)
        push(parts[i]);
      return;
    }

    // Try to parse traditional format
    String host = hn;
    String portStr;
    long long colonIdx = hn.find(":");
    if (colonIdx != -1) {
      host = hn.substring(0, (usz)colonIdx);
      portStr = hn.substring((usz)colonIdx + 1);
    }

    // Check if IPv4
    Array<String> ipParts = host.split(".");
    bool isIP = (ipParts.size() == 4);
    if (isIP) {
      for (usz i = 0; i < 4; i++) {
        if (!strIsNumeric(ipParts[i])) {
          isIP = false;
          break;
        }
      }
    }

    if (isIP) {
      push("1");
      for (usz i = 0; i < 4; i++)
        push(ipParts[i]);
      if (portStr.size() > 0)
        push(portStr);
      else
        push("80");
    } else {
      // DNS or other
      Array<String> segs = host.split(".");
      for (long long i = (long long)segs.size() - 1; i >= 0; i--) {
        push(segs[(usz)i]);
      }
      if (portStr.size() > 0)
        push(portStr);
      else
        push("80");
    }
  }

  bool includesNames() const {
    for (usz i = 0; i < size(); i++) {
      if (!strIsNumeric((*this)[i]))
        return true;
    }
    return false;
  }

  Hostname beforeNamed() const {
    Hostname res;
    for (usz i = 0; i < size(); i++) {
      if (!strIsNumeric((*this)[i]))
        break;
      res.push((*this)[i]);
    }
    return res;
  }

  Hostname named() const {
    Hostname res;
    bool found = false;
    for (usz i = 0; i < size(); i++) {
      if (!found && !strIsNumeric((*this)[i]))
        found = true;
      if (found)
        res.push((*this)[i]);
    }
    return res;
  }

  u16 port() const {
    usz sz = size();
    if (sz == 0)
      return 80;
    if (isIPv4()) {
      if (sz >= 6)
        return (u16)parseLong((*this)[5]);
      return 80;
    }
    if (isIPv6()) {
      if (sz >= 10)
        return (u16)parseLong((*this)[9]);
      return 80;
    }
    // Named: find first numeric after the names
    for (usz i = 0; i < sz; i++) {
      if (!strIsNumeric((*this)[i])) {
        // After name(s), look for numeric
        for (usz j = i + 1; j < sz; j++) {
          if (strIsNumeric((*this)[j]))
            return (u16)parseLong((*this)[j]);
        }
        break;
      }
    }
    return 80;
  }

  bool isIPv4() const { return size() > 0 && (*this)[0] == "1"; }
  bool isIPv6() const { return size() > 0 && (*this)[0] == "2"; }

  Array<u8> ipv4() const {
    Array<u8> res;
    if (!isIPv4() || size() < 5)
      return res;
    for (usz i = 1; i <= 4; i++)
      res.push((u8)parseLong((*this)[i]));
    return res;
  }

  Array<u16> ipv6() const {
    Array<u16> res;
    if (!isIPv6() || size() < 9)
      return res;
    for (usz i = 1; i <= 8; i++)
      res.push((u16)parseLong((*this)[i]));
    return res;
  }

  String toString(bool traditional = true) const {
    usz sz = size();
    if (sz == 0)
      return "";
    if (!traditional) {
      String res;
      for (usz i = 0; i < sz; i++) {
        if (i > 0)
          res += ",";
        res += (*this)[i];
      }
      return res;
    }

    if (isIPv4()) {
      if (sz < 5)
        return "";
      String res;
      for (usz i = 1; i <= 4; i++) {
        if (i > 1)
          res += ".";
        res += (*this)[i];
      }
      if (sz >= 6) {
        res += ":";
        res += (*this)[5];
      }
      return res;
    }

    if (isIPv6()) {
      // Simple hex join for now? User didn't specify exactly, but 1.1.2.3.4:80
      // was example for IPv4
      if (sz < 9)
        return "";
      String res;
      for (usz i = 1; i <= 8; i++) {
        if (i > 1)
          res += ":";
        res += (*this)[i]; // Assuming they are kept as strings, maybe hex?
      }
      if (sz >= 10) {
        res += ":";
        res += (*this)[9];
      }
      return res;
    }

    // Named path: Reverse the names back to DNS order
    String res;
    long long lastNamed = -1;
    for (usz i = 0; i < sz; i++) {
      if (!strIsNumeric((*this)[i]))
        lastNamed = (long long)i;
    }

    if (lastNamed ==
        -1) { // No named segments found, return empty or handle as error
      // If there's a port, it might be a numeric-only path that isn't IPv4/6
      if (sz > 0 && strIsNumeric((*this)[sz - 1])) {
        res += (*this)[sz - 1]; // Just the port
      }
      return res;
    }

    for (long long i = lastNamed; i >= 0; i--) {
      if (!strIsNumeric((*this)[(usz)i])) {
        if (!res.isEmpty())
          res += ".";
        res += (*this)[(usz)i];
      }
    }

    if ((usz)lastNamed + 1 < sz) {
      res += ":";
      res += (*this)[(usz)lastNamed + 1];
    }
    return res;
  }
};

inline NumericalHostname::NumericalHostname(const Hostname &hn) {
  for (usz i = 0; i < hn.size(); i++) {
    push(parseLong(hn[i]));
  }
}

inline Hostname::Hostname(const NumericalHostname &nhn) {
  for (usz i = 0; i < nhn.size(); i++) {
    push(String(nhn[i]));
  }
}

class XI_EXPORT Path {
private:
  String _protocol;
  Hostname _hostname;
  Array<String> _segments;
  bool _isAbsolute; // Track if path is absolute (starts with /)

  // Query handling
  mutable Map<String, String> _queryMap;
  mutable bool _queryParsed;
  mutable String _rawQuery;

  // -------------------------------------------------------------------------
  // Helpers
  // -------------------------------------------------------------------------

  static String urlDecode(const String &in) {
    String out;
    const u8 *d = in.data();
    usz len = in.size();
    for (usz i = 0; i < len; ++i) {
      if (d[i] == '%' && i + 2 < len) {
        auto hexVal = [](u8 c) -> int {
          if (c >= '0' && c <= '9')
            return c - '0';
          if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
          if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
          return 0;
        };
        out.push((u8)((hexVal(d[i + 1]) << 4) | hexVal(d[i + 2])));
        i += 2;
      } else if (d[i] == '+')
        out.push(' ');
      else
        out.push(d[i]);
    }
    return out;
  }

  void parseQuery() const {
    if (_queryParsed)
      return;
    const_cast<Map<String, String> &>(_queryMap).clear();

    if (_rawQuery.isEmpty()) {
      _queryParsed = true;
      return;
    }

    Array<String> pairs = _rawQuery.split("&");
    for (usz i = 0; i < pairs.size(); i++) {
      String &pair = pairs[i];
      long long eq = pair.find("=");
      if (eq != -1) {
        String k = urlDecode(pair.substring(0, (usz)eq));
        String v = urlDecode(pair.substring((usz)eq + 1));
        _queryMap.put(k, v);
      } else {
        _queryMap.put(urlDecode(pair), "");
      }
    }
    _queryParsed = true;
  }

  // Optimized mergePath that avoids String::replace/split bugs
  void mergePath(const String &rawPath, bool resetStack) {
    if (resetStack)
      _segments = Array<String>();

    const u8 *data = rawPath.data();
    usz len = rawPath.size();
    usz start = 0;

    for (usz i = 0; i < len; ++i) {
      char c = (char)data[i];
      if (c == '/' || c == '\\') {
        if (i > start) {
          String seg = rawPath.substring(start, i);
          processSegment(seg);
        }
        start = i + 1;
      }
    }

    // Handle tail segment
    if (start < len) {
      String seg = rawPath.substring(start);
      processSegment(seg);
    }
  }

  void processSegment(const String &p) {
    if (p.isEmpty() || p == ".")
      return;
    if (p == "..") {
      if (_segments.size() > 0)
        _segments.pop();
    } else {
      _segments.push(p);
    }
  }

public:
  // -------------------------------------------------------------------------
  // Constructors
  // -------------------------------------------------------------------------
  Path() : _isAbsolute(false), _queryParsed(false) {}

  Path(const String &path) : _isAbsolute(false), _queryParsed(false) {
    resolve(true, path);
  }

  Path(const Array<String> &paths) : _isAbsolute(false), _queryParsed(false) {
    if (paths.size() == 0)
      return;
    for (usz i = 0; i < paths.size(); ++i) {
      resolve((i == 0), paths[i]);
    }
  }

private:
  void resolve(bool isLeader, const String &raw) {
    if (raw.isEmpty())
      return;

    // 1. Extract Query
    String pathPart = raw;
    long long qIdx = raw.find("?");
    if (qIdx != -1) {
      pathPart = raw.substring(0, (usz)qIdx);
      String q = raw.substring((usz)qIdx + 1);
      if (!_rawQuery.isEmpty())
        _rawQuery += "&";
      _rawQuery += q;
      _queryParsed = false;
    }

    // 2. Extract Protocol
    long long protoIdx = pathPart.find("://");
    usz pathStart = 0;

    if (protoIdx != -1) {
      _isAbsolute = true; // Protocol implies absolute
      if (isLeader) {
        _protocol = pathPart.substring(0, (usz)protoIdx);
        usz afterProto = (usz)protoIdx + 3;
        long long pathSlash = pathPart.find("/", afterProto);
        usz hostEnd = (pathSlash == -1) ? pathPart.size() : (usz)pathSlash;

        String auth = pathPart.substring(afterProto, hostEnd);
        _hostname = Hostname(auth);

        pathStart = hostEnd;
      } else {
        // Protocol found in later part -> Reset (absolute override)
        usz afterProto = (usz)protoIdx + 3;
        long long pathSlash = pathPart.find("/", afterProto);
        pathStart = (pathSlash != -1) ? (usz)pathSlash : pathPart.size();
        _segments = Array<String>();
      }
    }

    // 3. Process Path
    String p = pathPart.substring(pathStart);

    // Check for absolute path char
    const u8 *pData = const_cast<String &>(p).data();
    bool isAbsLocal = false;
    if (!p.isEmpty() && (pData[0] == '/' || pData[0] == '\\'))
      isAbsLocal = true;
    if (p.size() >= 3 && pData[1] == ':' &&
        (pData[2] == '/' || pData[2] == '\\'))
      isAbsLocal = true;

    if (isAbsLocal) {
      _segments = Array<String>();
      _isAbsolute = true;
    }

    mergePath(p, false);
  }

public:
  // -------------------------------------------------------------------------
  // Properties
  // -------------------------------------------------------------------------

  String protocol() const { return _protocol; }
  Hostname &hostname() { return _hostname; }
  const Hostname &hostname() const { return _hostname; }

  u16 port() const { return _hostname.port(); }

  String host() const { return _hostname.toString(true); }

  String basename() const {
    if (_segments.size() == 0)
      return "";
    return _segments[_segments.size() - 1];
  }

  Map<String, String> &query() {
    parseQuery();
    return _queryMap;
  }

  // -------------------------------------------------------------------------
  // Methods
  // -------------------------------------------------------------------------

  String toString(bool forwardSlash = true, bool protocolAndHostname = true,
                  bool withQuery = true) const {
    String out;

    if (protocolAndHostname && !_protocol.isEmpty()) {
      out += _protocol;
      out += "://";
      out += _hostname.toString(true);
    }

    const char *sep = forwardSlash ? "/" : "\\";

    // Logic for leading slash
    bool addLeadingSlash = false;
    if (protocolAndHostname && !_protocol.isEmpty()) {
      // URL: add slash if we have path segments
      if (_segments.size() > 0)
        addLeadingSlash = true;
    } else {
      // FS: add slash if absolute
      if (_isAbsolute)
        addLeadingSlash = true;
    }

    if (addLeadingSlash)
      out += "/";

    for (usz i = 0; i < _segments.size(); ++i) {
      if (i > 0)
        out += sep;
      out += _segments[i];
    }

    if (withQuery) {
      if (_queryParsed) {
        if (_queryMap.size() > 0) {
          out += "?";
          bool first = true;
          for (auto it = _queryMap.begin(); it != _queryMap.end(); ++it) {
            if (!first)
              out += "&";
            out += it->key;
            if (!it->value.isEmpty()) {
              out += "=";
              out += it->value;
            }
            first = false;
          }
        }
      } else if (!_rawQuery.isEmpty()) {
        out += "?";
        out += _rawQuery;
      }
    }
    return out;
  }

  String relativeTo(const Path &parent) const {
    if (_protocol != parent._protocol ||
        _hostname.toString(false) != parent._hostname.toString(false)) {
      return "";
    }

    usz common = 0;
    usz minLen = (_segments.size() < parent._segments.size())
                     ? _segments.size()
                     : parent._segments.size();

    while (common < minLen && _segments[common] == parent._segments[common]) {
      common++;
    }

    String res;

    usz up = parent._segments.size() - common;
    for (usz i = 0; i < up; ++i) {
      if (!res.isEmpty())
        res += "/";
      res += "..";
    }

    for (usz i = common; i < _segments.size(); ++i) {
      if (!res.isEmpty() || up > 0)
        res += "/";
      res += _segments[i];
    }

    return res;
  }
};

} // namespace Xi

#endif // XI_PATH_HPP