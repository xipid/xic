/**
 * @file Path.hpp
 * @brief URL and filesystem path manipulation utilities for the Xi framework.

 */

#ifndef XI_CORE_PATH_HPP
#define XI_CORE_PATH_HPP

#include "../Collection/Map.hpp"
#include "../Collection/String.hpp"

using namespace Collection;

namespace Resource {

class Address;

/**
 * @class NumericalAddress
 * @brief Represents a network address as a sequence of numbers (e.g., IPv4/IPv6
 * segments).
 */
class NumericalAddress : public Array<u64> {
private:
  static bool _isHex(u8 c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  }
  static u64 _hexToU64(const String &s) {
    u64 v = 0;
    for (usz i = 0; i < s.size(); i++) {
      u8 c = s[i];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= (c - '0');
      else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
    }
    return v;
  }
  static bool _strIsNumeric(const String &s) {
    if (s.isEmpty()) return false;
    for (usz i = 0; i < s.size(); i++)
      if (s[i] < '0' || s[i] > '9') return false;
    return true;
  }
  static bool _strIsHex(const String &s) {
    if (s.isEmpty()) return false;
    for (usz i = 0; i < s.size(); i++)
      if (!_isHex(s[i])) return false;
    return true;
  }

  void _parseIPv6Body(const String &str) {
    long long dc = str.find("::");
    if (dc != -1) {
      String before = str.substring(0, (usz)dc);
      String after;
      if ((usz)dc + 2 < str.size())
        after = str.substring((usz)dc + 2);

      Array<u64> bSegs, aSegs;
      if (!before.isEmpty()) {
        Array<String> p = before.split(":");
        for (usz i = 0; i < p.size(); i++)
          bSegs.push(_hexToU64(p[i]));
      }
      if (!after.isEmpty()) {
        Array<String> p = after.split(":");
        for (usz i = 0; i < p.size(); i++)
          aSegs.push(_hexToU64(p[i]));
      }
      usz zeros = 8 - bSegs.size() - aSegs.size();
      for (usz i = 0; i < bSegs.size(); i++) push(bSegs[i]);
      for (usz i = 0; i < zeros; i++) push(0);
      for (usz i = 0; i < aSegs.size(); i++) push(aSegs[i]);
    } else {
      Array<String> p = str.split(":");
      for (usz i = 0; i < p.size(); i++)
        push(_hexToU64(p[i]));
    }
  }

public:
  NumericalAddress() {}
  NumericalAddress(const Address &hn);
  NumericalAddress(const char *str) : NumericalAddress(String(str)) {}
  NumericalAddress& operator=(const String &str) {
    *this = NumericalAddress(str);
    return *this;
  }
  NumericalAddress& operator=(const char *str) {
    *this = NumericalAddress(String(str));
    return *this;
  }

  /**
   * @brief Construct from a string. Recognizes:
   *   - Rho comma-separated:  "7,0,0,0,0,9000"
   *   - IPv4 dotted:          "192.168.1.1"  or  "192.168.1.1:8080"
   *   - IPv6 full/compressed: "::1", "2001:db8::1", "fe80::1%eth0"
   *   - IPv6 bracketed+port:  "[::1]:8080"
   *   - Rho dotted fallback:  "7.0.0.0.0.9000" (non-4-octet dot notation)
   */
  NumericalAddress(const String &str) {
    if (str.isEmpty()) return;

    // ── Rho comma-separated: "7,0,0,0,0,9000" ──
    if (str.find(",") != -1) {
      Array<String> parts = str.split(",");
      for (usz i = 0; i < parts.size(); i++)
        push((u64)parseLong(parts[i]));
      return;
    }

    // ── IPv6 bracket notation: "[::1]:8080" ──
    if (str.size() > 0 && str[0] == '[') {
      long long cb = str.find("]");
      if (cb == -1) return;
      String body = str.substring(1, (usz)cb);
      push(8); // TopLevel::IPv6
      _parseIPv6Body(body);
      if ((usz)cb + 2 < str.size() && str[(usz)cb + 1] == ':')
        push((u64)parseLong(str.substring((usz)cb + 2)));
      return;
    }

    // Count colons to distinguish IPv6 from IPv4
    int colonCount = 0;
    for (usz i = 0; i < str.size(); i++)
      if (str[i] == ':') colonCount++;

    // ── IPv6 (2+ colons): "::1", "2001:db8::1", "fe80::1" ──
    if (colonCount >= 2) {
      push(8); // TopLevel::IPv6
      _parseIPv6Body(str);
      return;
    }

    // ── IPv4 with optional port: "192.168.1.1" or "192.168.1.1:8080" ──
    String host = str;
    String portStr;
    if (colonCount == 1) {
      long long ci = str.find(":");
      host = str.substring(0, (usz)ci);
      portStr = str.substring((usz)ci + 1);
    }

    Array<String> octets = host.split(".");
    if (octets.size() == 4) {
      bool allNum = true;
      for (usz i = 0; i < 4 && allNum; i++)
        allNum = _strIsNumeric(octets[i]);
      if (allNum) {
        push(7); // TopLevel::IPv4
        for (usz i = 0; i < 4; i++)
          push((u64)parseLong(octets[i]));
        if (!portStr.isEmpty())
          push((u64)parseLong(portStr));
        return;
      }
    }

    // ── Rho dot-separated fallback: "7.0.0.0.0.9000" ──
    Array<String> parts = host.split(".");
    for (usz i = 0; i < parts.size(); i++)
      push((u64)parseLong(parts[i]));
    if (!portStr.isEmpty())
      push((u64)parseLong(portStr));
  }

  /**
   * @brief Finds the longest common prefix between two addresses.
   */
  NumericalAddress common(const NumericalAddress &other) const {
    NumericalAddress res;
    usz minLen = size() < other.size() ? size() : other.size();
    for (usz i = 0; i < minLen; i++) {
      if ((*this)[i] == other[i])
        res.push((*this)[i]);
      else
        break;
    }
    return res;
  }
};

/**
 * @class Address
 * @brief High-level network address representation (DNS, IPv4, IPv6).
 */
class Address : public Array<String> {
private:
  static bool strIsNumeric(const String &s) {
    if (s.isEmpty())
      return false;
    for (usz i = 0; i < s.size(); i++)
      if (s[i] < '0' || s[i] > '9')
        return false;
    return true;
  }

public:
  Address() {}
  Address(const NumericalAddress &nhn);
  Address(const String &hn) {
    if (hn.isEmpty())
      return;

    if (hn.find(",") != -1) {
      Array<String> parts = hn.split(",");
      for (usz i = 0; i < parts.size(); i++)
        push(parts[i]);
      return;
    }

    String host = hn;
    String portStr;
    long long colonIdx = hn.find(":");
    if (colonIdx != -1) {
      host = hn.substring(0, (usz)colonIdx);
      portStr = hn.substring((usz)colonIdx + 1);
    }

    Array<String> ipParts = host.split(".");
    bool isIP = (ipParts.size() == 4);
    if (isIP) {
      for (usz i = 0; i < 4; i++)
        if (!strIsNumeric(ipParts[i])) {
          isIP = false;
          break;
        }
    }

    if (isIP) {
      push("1");
      for (usz i = 0; i < 4; i++)
        push(ipParts[i]);
      push(portStr.size() > 0 ? portStr : "80");
    } else {
      Array<String> segs = host.split(".");
      for (long long i = (long long)segs.size() - 1; i >= 0; i--)
        push(segs[(usz)i]);
      push(portStr.size() > 0 ? portStr : "80");
    }
  }

  bool includesNames() const {
    for (usz i = 0; i < size(); i++)
      if (!strIsNumeric((*this)[i]))
        return true;
    return false;
  }

  Address beforeNamed() const {
    Address res;
    for (usz i = 0; i < size(); i++) {
      if (!strIsNumeric((*this)[i]))
        break;
      res.push((*this)[i]);
    }
    return res;
  }

  Address named() const {
    Address res;
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
    if (isIPv4())
      return (sz >= 6) ? (u16)parseLong((*this)[5]) : 80;
    if (isIPv6())
      return (sz >= 10) ? (u16)parseLong((*this)[9]) : 80;
    for (usz i = 0; i < sz; i++) {
      if (!strIsNumeric((*this)[i])) {
        for (usz j = i + 1; j < sz; j++)
          if (strIsNumeric((*this)[j]))
            return (u16)parseLong((*this)[j]);
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

  /**
   * @brief Converts the address to a human-readable string.
   * @param traditional Use standard IP/DNS format if true.
   */
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
      if (sz < 9)
        return "";
      String res;
      for (usz i = 1; i <= 8; i++) {
        if (i > 1)
          res += ":";
        res += (*this)[i];
      }
      if (sz >= 10) {
        res += ":";
        res += (*this)[9];
      }
      return res;
    }

    String res;
    long long lastNamed = -1;
    for (usz i = 0; i < sz; i++)
      if (!strIsNumeric((*this)[i]))
        lastNamed = (long long)i;
    if (lastNamed == -1)
      return (sz > 0 && strIsNumeric((*this)[sz - 1])) ? (*this)[sz - 1] : "";

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

inline NumericalAddress::NumericalAddress(const Address &hn) {
  for (usz i = 0; i < hn.size(); i++)
    push(parseLong(hn[i]));
}

inline Address::Address(const NumericalAddress &nhn) {
  for (usz i = 0; i < nhn.size(); i++)
    push(String(nhn[i]));
}

inline bool operator==(const NumericalAddress& a, const NumericalAddress& b) {
  if (a.size() != b.size()) return false;
  for (usz i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

inline bool operator!=(const NumericalAddress& a, const NumericalAddress& b) {
  return !(a == b);
}

/**
 * @class Path
 * @brief Advanced path and URL manipulation utility.
 *
 * Handles protocols, network addresses, hierarchical segments, and query
 * parameters.
 */
class XI_EXPORT Path {
private:
  String _protocol;
  Address _address;
  Array<String> _segments;
  bool _isAbsolute;

  mutable Map<String, String> _queryMap;
  mutable bool _queryParsed;
  mutable String _rawQuery;

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
      if (eq != -1)
        _queryMap.put(urlDecode(pair.substring(0, (usz)eq)),
                      urlDecode(pair.substring((usz)eq + 1)));
      else
        _queryMap.put(urlDecode(pair), "");
    }
    _queryParsed = true;
  }

  void mergePath(const String &rawPath, bool resetStack) {
    if (resetStack)
      _segments = Array<String>();
    const u8 *data = rawPath.data();
    usz len = rawPath.size();
    usz start = 0;
    for (usz i = 0; i < len; ++i) {
      char c = (char)data[i];
      if (c == '/' || c == '\\') {
        if (i > start)
          processSegment(rawPath.substring(start, i));
        start = i + 1;
      }
    }
    if (start < len)
      processSegment(rawPath.substring(start));
  }

  void processSegment(const String &p) {
    if (p.isEmpty() || p == ".")
      return;
    if (p == "..") {
      if (_segments.size() > 0)
        _segments.pop();
    } else
      _segments.push(p);
  }

public:
  /** @brief Default constructor. */
  Path() : _isAbsolute(false), _queryParsed(false) {}

  /** @brief Constructs from a path string. */
  Path(const String &path) : _isAbsolute(false), _queryParsed(false) {
    resolve(true, path);
  }

  /** @brief Constructs by merging multiple path strings. */
  Path(const Array<String> &paths) : _isAbsolute(false), _queryParsed(false) {
    if (paths.size() == 0)
      return;
    for (usz i = 0; i < paths.size(); ++i)
      resolve((i == 0), paths[i]);
  }

private:
  void resolve(bool isLeader, const String &raw) {
    if (raw.isEmpty())
      return;
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

    long long protoIdx = pathPart.find("://");
    usz pathStart = 0;
    if (protoIdx != -1) {
      _isAbsolute = true;
      if (isLeader) {
        _protocol = pathPart.substring(0, (usz)protoIdx);
        usz afterProto = (usz)protoIdx + 3;
        long long pathSlash = pathPart.find("/", afterProto);
        usz hostEnd = (pathSlash == -1) ? pathPart.size() : (usz)pathSlash;
        _address = Address(pathPart.substring(afterProto, hostEnd));
        pathStart = hostEnd;
      } else {
        usz afterProto = (usz)protoIdx + 3;
        long long pathSlash = pathPart.find("/", afterProto);
        pathStart = (pathSlash != -1) ? (usz)pathSlash : pathPart.size();
        _segments = Array<String>();
      }
    }

    String p = pathPart.substring(pathStart);
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
  /** @brief Returns the protocol string (e.g., "http"). */
  String protocol() const { return _protocol; }
  /** @brief Accesses the network address component. */
  Address &address() { return _address; }
  /** @brief Accesses the network address component (const). */
  const Address &address() const { return _address; }

  /** @brief Extracts the port number. */
  u16 port() const { return _address.port(); }
  /** @brief Returns the host as a human-readable string. */
  String host() const { return _address.toString(true); }

  /** @brief Returns the last segment of the path. */
  String basename() const {
    return (_segments.size() == 0) ? "" : _segments[_segments.size() - 1];
  }

  /** @brief Accesses the query parameter map. */
  Map<String, String> &query() {
    parseQuery();
    return _queryMap;
  }

  /**
   * @brief Reconstructs the full path as a string.
   */
  String toString(bool forwardSlash = true, bool protocolAndAddress = true,
                  bool withQuery = true) const {
    String out;
    if (protocolAndAddress && !_protocol.isEmpty()) {
      out += _protocol;
      out += "://";
      out += _address.toString(true);
    }
    const char *sep = forwardSlash ? "/" : "\\";
    bool addLeadingSlash = (protocolAndAddress && !_protocol.isEmpty())
                               ? (_segments.size() > 0)
                               : _isAbsolute;
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

  /**
   * @brief Calculates the relative path from a parent.
   */
  String relativeTo(const Path &parent) const {
    if (_protocol != parent._protocol ||
        _address.toString(false) != parent._address.toString(false))
      return "";
    usz common = 0;
    usz minLen = (_segments.size() < parent._segments.size())
                     ? _segments.size()
                     : parent._segments.size();
    while (common < minLen && _segments[common] == parent._segments[common])
      common++;
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

} // namespace Resource

#endif // XI_CORE_PATH_HPP