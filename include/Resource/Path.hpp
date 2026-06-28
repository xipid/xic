#ifndef XI_CORE_PATH_HPP
#define XI_CORE_PATH_HPP

#include "../Collection/Array.hpp"
#include "../Collection/Map.hpp"
#include "../Collection/String.hpp"
#include "../Xi/Primitives.hpp"
#include <cstdio>

namespace Resource {

using namespace Xi;
using namespace Collection;

class Path;

// ---------------------------------------------------------------------------
// NumericalPath — Represents network segments as u32 values (ports/IP octets)
// ---------------------------------------------------------------------------
class NumericalPath : public Array<u32> {
private:
    static bool _isHex(u8 c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }
    
    static u32 _hexToU32(const String &s) {
        u32 v = 0;
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

    void _parseIPv6Body(const String &str) {
        long long dc = str.find("::");
        if (dc != -1) {
            String before = str.substring(0, (usz)dc);
            String after;
            if ((usz)dc + 2 < str.size())
                after = str.substring((usz)dc + 2);

            Array<u32> bSegs, aSegs;
            if (!before.isEmpty()) {
                Array<String> p = before.split(":");
                for (usz i = 0; i < p.size(); i++)
                    bSegs.push(_hexToU32(p[i]));
            }
            if (!after.isEmpty()) {
                Array<String> p = after.split(":");
                for (usz i = 0; i < p.size(); i++)
                    aSegs.push(_hexToU32(p[i]));
            }
            usz zeros = 8 - bSegs.size() - aSegs.size();
            for (usz i = 0; i < bSegs.size(); i++) push(bSegs[i]);
            for (usz i = 0; i < zeros; i++) push(0);
            for (usz i = 0; i < aSegs.size(); i++) push(aSegs[i]);
        } else {
            Array<String> p = str.split(":");
            for (usz i = 0; i < p.size(); i++)
                push(_hexToU32(p[i]));
        }
    }

public:
    NumericalPath() {}
    NumericalPath(const Path &hn);
    NumericalPath(const char *str) : NumericalPath(String(str)) {}
    NumericalPath& operator=(const String &str) {
        *this = NumericalPath(str);
        return *this;
    }
    NumericalPath& operator=(const char *str) {
        *this = NumericalPath(String(str));
        return *this;
    }

    NumericalPath(const String &str) {
        if (str.isEmpty()) return;
        if (str == "halt" || str == "4294967295") {
            push(0xFFFFFFFF);
            return;
        }

        // Comma-separated segments
        if (str.find(",") != -1) {
            Array<String> parts = str.split(",");
            for (usz i = 0; i < parts.size(); i++)
                push((u32)parseLong(parts[i]));
            return;
        }

        // Bracketed IPv6 notation
        if (str.size() > 0 && str[0] == '[') {
            long long cb = str.find("]");
            if (cb == -1) return;
            String body = str.substring(1, (usz)cb);
            push(6); // IPv6
            _parseIPv6Body(body);
            if ((usz)cb + 2 < str.size() && str[(usz)cb + 1] == ':')
                push((u32)parseLong(str.substring((usz)cb + 2)));
            return;
        }

        int colonCount = 0;
        for (usz i = 0; i < str.size(); i++)
            if (str[i] == ':') colonCount++;

        // IPv6 (2+ colons)
        if (colonCount >= 2) {
            push(6); // IPv6
            _parseIPv6Body(str);
            return;
        }

        // IPv4 with optional port
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
                push(4); // IPv4
                for (usz i = 0; i < 4; i++)
                    push((u32)parseLong(octets[i]));
                if (!portStr.isEmpty())
                    push((u32)parseLong(portStr));
                return;
            }
        }

        // Fallback dotted segments
        Array<String> parts = host.split(".");
        for (usz i = 0; i < parts.size(); i++)
            push((u32)parseLong(parts[i]));
        if (!portStr.isEmpty())
            push((u32)parseLong(portStr));
    }

    NumericalPath common(const NumericalPath &other) const {
        NumericalPath res;
        usz minLen = size() < other.size() ? size() : other.size();
        for (usz i = 0; i < minLen; i++) {
            if ((*this)[i] == other[i])
                res.push((*this)[i]);
            else
                break;
        }
        return res;
    }

    bool isHalt() const { return size() > 0 && (*this)[0] == 0xFFFFFFFF; }
    bool isIPv4() const { return size() > 0 && (*this)[0] == 4; }
    bool isIPv6() const { return size() > 0 && (*this)[0] == 6; }

    u16 port() const {
        if (isIPv4()) return size() >= 6 ? (u16)(*this)[5] : 0;
        if (isIPv6()) return size() >= 10 ? (u16)(*this)[9] : 0;
        return 0;
    }

    String toString() const {
        if (size() == 0) return "";
        if (isIPv4()) {
            if (size() < 5) return "";
            String res = String((long long)(*this)[1]) + "." + String((long long)(*this)[2]) + "." +
                         String((long long)(*this)[3]) + "." + String((long long)(*this)[4]);
            if (size() >= 6) {
                res += ":" + String((long long)(*this)[5]);
            }
            return res;
        }
        if (isIPv6()) {
            if (size() < 9) return "";
            String res = "[";
            for (usz i = 1; i <= 8; i++) {
                if (i > 1) res += ":";
                char buf[16];
                snprintf(buf, sizeof(buf), "%x", (*this)[i]);
                res += buf;
            }
            res += "]";
            if (size() >= 10) {
                res += ":" + String((long long)(*this)[9]);
            }
            return res;
        }
        String res;
        for (usz i = 0; i < size(); i++) {
            if (i > 0) res += ",";
            res += String((long long)(*this)[i]);
        }
        return res;
    }
};

inline bool operator==(const NumericalPath& a, const NumericalPath& b) {
    if (a.size() != b.size()) return false;
    for (usz i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

inline bool operator!=(const NumericalPath& a, const NumericalPath& b) {
    return !(a == b);
}

using NumericalAddress = NumericalPath;

// ---------------------------------------------------------------------------
// Path — Represents hostnames, paths, protocols, methods, queries in Array<String>
// ---------------------------------------------------------------------------
class Path : public Array<String> {
private:
    static bool _isAllNumeric(const String& s) {
        if (s.isEmpty()) return false;
        for (usz i = 0; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') return false;
        }
        return true;
    }

    void _rebuild(const Array<String>& newHard, const Array<String>& newSoft, const String& newHash, const Array<String>& newQueries) {
        clear();
        push("5");
        push(method());
        push(String((long long)newHard.size()));
        for (usz i = 0; i < newHard.size(); ++i) push(newHard[i]);
        push(String((long long)newSoft.size()));
        for (usz i = 0; i < newSoft.size(); ++i) push(newSoft[i]);
        push(newHash);
        for (usz i = 0; i < newQueries.size(); ++i) push(newQueries[i]);
    }

    Array<String> _getHardItems() const {
        Array<String> res;
        if (size() == 0 || (*this)[0] != "5") return res;
        usz hc = _hardCount();
        usz hs = _hardStartOffset();
        for (usz i = 0; i < hc; ++i) res.push((*this)[hs + i]);
        return res;
    }

    Array<String> _getSoftItems() const {
        Array<String> res;
        if (size() == 0 || (*this)[0] != "5") return res;
        usz sc = _softCount();
        usz ss = _softStartOffset();
        for (usz i = 0; i < sc; ++i) res.push((*this)[ss + i]);
        return res;
    }

    Array<String> _getQueryItems() const {
        Array<String> res;
        if (size() == 0 || (*this)[0] != "5") return res;
        usz qs = _queryStartOffset();
        for (usz i = qs; i < size(); ++i) res.push((*this)[i]);
        return res;
    }

public:
    Path() {}
    Path(const String& str) { resolve(str); }
    Path(const char* str) : Path(String(str)) {}
    Path(const Array<String>& parts) {
        for (usz i = 0; i < parts.size(); i++) push(parts[i]);
    }
    Path(const NumericalPath& np) {
        for (usz i = 0; i < np.size(); ++i) {
            push(String((long long)np[i]));
        }
    }

    Path& operator=(const String& str) {
        clear();
        resolve(str);
        return *this;
    }
    Path& operator=(const char* str) {
        clear();
        resolve(String(str));
        return *this;
    }

    bool isNumerical() const {
        if (size() == 0) return false;
        if ((*this)[0] == "4" || (*this)[0] == "6") {
            for (usz i = 1; i < size(); ++i) {
                if (!_isAllNumeric((*this)[i])) return false;
            }
            return true;
        }
        for (usz i = 0; i < size(); ++i) {
            if (!_isAllNumeric((*this)[i])) return false;
        }
        return true;
    }

    NumericalPath toNumerical() const {
        NumericalPath np;
        for (usz i = 0; i < size(); ++i) {
            np.push((u32)parseLong((*this)[i]));
        }
        return np;
    }

    void resolve(const String& raw) {
        if (raw.isEmpty()) return;

        long long protoIdx = raw.find("://");
        long long spaceIdx = raw.find(" ");
        bool isProto = (protoIdx != -1);

        if (isProto) {
            push("5"); // Protocol marker
            String methodVal = "get";
            String urlPart = raw;

            if (spaceIdx != -1 && spaceIdx < protoIdx) {
                methodVal = raw.substring(0, (usz)spaceIdx);
                methodVal.trim();
                for (usz i = 0; i < methodVal.size(); ++i) {
                    if (methodVal[i] >= 'A' && methodVal[i] <= 'Z')
                        methodVal[i] = methodVal[i] - 'A' + 'a';
                }
                urlPart = raw.substring((usz)spaceIdx + 1);
                urlPart.trim();
            }
            push(methodVal);

            long long curProtoIdx = urlPart.find("://");
            String scheme = urlPart.substring(0, (usz)curProtoIdx);
            for (usz i = 0; i < scheme.size(); ++i) {
                if (scheme[i] >= 'A' && scheme[i] <= 'Z')
                    scheme[i] = scheme[i] - 'A' + 'a';
            }

            String rest = urlPart.substring((usz)curProtoIdx + 3);
            long long qIdx = rest.find("?");
            long long hIdx = rest.find("#");

            String hostAndPath = rest;
            String queryPart;
            String hashPart;

            if (hIdx != -1) {
                hashPart = rest.substring((usz)hIdx + 1);
                hostAndPath = rest.substring(0, (usz)hIdx);
            }
            if (qIdx != -1) {
                if (!(hIdx != -1 && qIdx > hIdx)) {
                    queryPart = hostAndPath.substring((usz)qIdx + 1);
                    hostAndPath = hostAndPath.substring(0, (usz)qIdx);
                }
            }

            long long slashIdx = hostAndPath.find("/");
            String hostPart = hostAndPath;
            String pathPart;
            if (slashIdx != -1) {
                hostPart = hostAndPath.substring(0, (usz)slashIdx);
                pathPart = hostAndPath.substring((usz)slashIdx);
            }

            long long atIdx = hostPart.find("@");
            String credsPart;
            String realHost = hostPart;
            if (atIdx != -1) {
                credsPart = hostPart.substring(0, (usz)atIdx);
                realHost = hostPart.substring((usz)atIdx + 1);
            }

            long long colonIdx = realHost.find(":");
            String hostName = realHost;
            String portVal;
            if (colonIdx != -1) {
                hostName = realHost.substring(0, (usz)colonIdx);
                portVal = realHost.substring((usz)colonIdx + 1);
            }

            Array<String> hardItems;
            Array<String> domainSegs = hostName.split(".");
            for (long long i = (long long)domainSegs.size() - 1; i >= 0; --i) {
                hardItems.push(domainSegs[(usz)i]);
            }

            if (!credsPart.isEmpty()) {
                long long cIdx = credsPart.find(":");
                String usr = credsPart;
                String pwd;
                if (cIdx != -1) {
                    usr = credsPart.substring(0, (usz)cIdx);
                    pwd = credsPart.substring((usz)cIdx + 1);
                }
                hardItems.push(String("usr:") + usr);
                if (cIdx != -1) {
                    hardItems.push(String("pwd:") + pwd);
                }
            }

            if (!portVal.isEmpty()) {
                hardItems.push(portVal);
            }

            push(String((long long)hardItems.size()));
            for (usz i = 0; i < hardItems.size(); ++i) push(hardItems[i]);

            Array<String> softItems;
            if (!pathPart.isEmpty()) {
                Array<String> pathSegs = pathPart.split("/");
                for (usz i = 0; i < pathSegs.size(); ++i) {
                    if (!pathSegs[i].isEmpty() && pathSegs[i] != ".") {
                        if (pathSegs[i] == "..") {
                            if (softItems.size() > 0) softItems.pop();
                        } else {
                            softItems.push(pathSegs[i]);
                        }
                    }
                }
            }

            push(String((long long)softItems.size()));
            for (usz i = 0; i < softItems.size(); ++i) push(softItems[i]);

            push(hashPart); // hashtag slot

            if (!queryPart.isEmpty()) {
                Array<String> pairs = queryPart.split("&");
                for (usz i = 0; i < pairs.size(); ++i) {
                    long long eq = pairs[i].find("=");
                    if (eq != -1) {
                        push(pairs[i].substring(0, (usz)eq));
                        push(pairs[i].substring((usz)eq + 1));
                    } else {
                        push(pairs[i]);
                        push("");
                    }
                }
            }
            return;
        }

        // IPv4 dotted numeric
        int dotCount = 0;
        int colonCount = 0;
        bool allNumeric = true;
        for (usz i = 0; i < raw.size(); i++) {
            char c = raw[i];
            if (c == '.') dotCount++;
            else if (c == ':') colonCount++;
            else if (c < '0' || c > '9') allNumeric = false;
        }

        if (dotCount == 3 && colonCount <= 1 && allNumeric) {
            push("4");
            String host = raw;
            String portVal;
            if (colonCount == 1) {
                long long ci = raw.find(":");
                host = raw.substring(0, (usz)ci);
                portVal = raw.substring((usz)ci + 1);
            }
            Array<String> octets = host.split(".");
            for (usz i = 0; i < 4; ++i) push(octets[i]);
            if (!portVal.isEmpty()) push(portVal);
            return;
        }

        // IPv6 address
        if (colonCount >= 2) {
            push("6");
            String body = raw;
            String portStr;
            if (raw[0] == '[') {
                long long cb = raw.find("]");
                if (cb != -1) {
                    body = raw.substring(1, (usz)cb);
                    if ((usz)cb + 2 < raw.size() && raw[(usz)cb + 1] == ':') {
                        portStr = raw.substring((usz)cb + 2);
                    }
                }
            }

            long long dc = body.find("::");
            if (dc != -1) {
                String before = body.substring(0, (usz)dc);
                String after;
                if ((usz)dc + 2 < body.size())
                    after = body.substring((usz)dc + 2);

                Array<String> bSegs, aSegs;
                if (!before.isEmpty()) bSegs = before.split(":");
                if (!after.isEmpty()) aSegs = after.split(":");
                usz zeros = 8 - bSegs.size() - aSegs.size();
                for (usz i = 0; i < bSegs.size(); i++) push(bSegs[i]);
                for (usz i = 0; i < zeros; i++) push("0");
                for (usz i = 0; i < aSegs.size(); i++) push(aSegs[i]);
            } else {
                Array<String> p = body.split(":");
                for (usz i = 0; i < p.size() && i < 8; i++) push(p[i]);
            }
            if (!portStr.isEmpty()) push(portStr);
            return;
        }

        // Control Scheme
        if (raw == "//" || raw == "EMPTY" || raw == "halt") {
            push("7");
            push(raw);
            return;
        }

        // Route containing slashes
        if (raw.find("/") != -1) {
            Array<String> parts = raw.split("/");
            for (usz i = 0; i < parts.size(); i++)
                if (!parts[i].isEmpty()) push(parts[i]);
            return;
        }

        // Comma-separated general segments
        if (raw.find(",") != -1) {
            Array<String> parts = raw.split(",");
            for (usz i = 0; i < parts.size(); i++) push(parts[i]);
            return;
        }

        // Reversed dot-notation domain fallback
        if (raw.find(".") != -1) {
            Array<String> parts = raw.split(".");
            for (long long i = (long long)parts.size() - 1; i >= 0; --i)
                push(parts[(usz)i]);
            return;
        }

        push(raw);
    }

    // --- Offset helpers ---
    usz _hardCountOffset() const { return 2; }
    usz _hardStartOffset() const { return 3; }
    usz _hardCount() const { return (usz)parseLong((*this)[_hardCountOffset()]); }
    usz _softCountOffset() const { return 3 + _hardCount(); }
    usz _softStartOffset() const { return 3 + _hardCount() + 1; }
    usz _softCount() const { return (usz)parseLong((*this)[_softCountOffset()]); }
    usz _metadataStartOffset() const { return 3 + _hardCount() + 1 + _softCount(); }
    usz _hashtagOffset() const { return _metadataStartOffset(); }
    usz _queryStartOffset() const { return _metadataStartOffset() + 1; }

    // --- Protocol / Method ---
    String protocol() const {
        if (size() == 0 || (*this)[0] != "5") return "";
        String m = method();
        u16 p = port();
        if ((m == "get" || m == "put" || m == "delete") && p == 80) return "http";
        return "https";
    }

    void protocol(const String& p) {
        if (size() == 0 || (*this)[0] != "5") {
            clear();
            push("5");
            push("get");
            push("0");
            push("0");
            push("");
        }
        if (p == "http" || p == "https") method("get");
    }

    String method() const {
        if (size() > 1 && (*this)[0] == "5") return (*this)[1];
        return "";
    }

    void method(const String& m) {
        if (size() > 1 && (*this)[0] == "5") (*this)[1] = m;
    }

    // --- Hostname / Port ---
    bool hasHostname() const {
        if (size() == 0 || (*this)[0] != "5") return false;
        return _hardCount() > 0;
    }

    String hostname() const {
        if (size() == 0) return "";
        if ((*this)[0] == "4") {
            String res = (*this)[1] + "." + (*this)[2] + "." + (*this)[3] + "." + (*this)[4];
            if (size() >= 6) res += ":" + (*this)[5];
            return res;
        }
        if ((*this)[0] == "6") {
            String res = "[";
            for (usz i = 1; i <= 8; i++) {
                if (i > 1) res += ":";
                res += (*this)[i];
            }
            res += "]";
            if (size() >= 10) res += ":" + (*this)[9];
            return res;
        }
        if ((*this)[0] == "5") {
            usz hc = _hardCount();
            usz hs = _hardStartOffset();
            String res;
            long long lastDomainSeg = -1;
            for (usz i = 0; i < hc; ++i) {
                String item = (*this)[hs + i];
                if (!item.startsWith("usr:") && !item.startsWith("pwd:") && !_isAllNumeric(item)) {
                    lastDomainSeg = (long long)i;
                }
            }
            for (long long i = lastDomainSeg; i >= 0; --i) {
                if (!res.isEmpty()) res += ".";
                res += (*this)[hs + (usz)i];
            }
            u16 prt = port();
            if (prt != 0 && prt != 443) {
                res += ":" + String((long long)prt);
            }
            return res;
        }
        return (*this)[0];
    }

    String host() const {
        return hostname();
    }

    String basename() const {
        if (size() == 0) return "";
        if ((*this)[0] == "5") {
            usz sc = _softCount();
            usz ss = _softStartOffset();
            if (sc > 0) return (*this)[ss + sc - 1];
            return "";
        }
        return (*this)[size() - 1];
    }

    void hostname(const String& h) {
        if (size() == 0 || (*this)[0] != "5") return;
        long long colonIdx = h.find(":");
        String hostName = h;
        String portVal;
        if (colonIdx != -1) {
            hostName = h.substring(0, (usz)colonIdx);
            portVal = h.substring((usz)colonIdx + 1);
        }

        Array<String> domainSegs = hostName.split(".");
        Array<String> newHard;
        for (long long i = (long long)domainSegs.size() - 1; i >= 0; --i) {
            newHard.push(domainSegs[(usz)i]);
        }

        String usr = username();
        String pwd = password();
        if (!usr.isEmpty()) newHard.push(String("usr:") + usr);
        if (!pwd.isEmpty()) newHard.push(String("pwd:") + pwd);

        if (!portVal.isEmpty()) {
            newHard.push(portVal);
        } else {
            u16 prt = port();
            if (prt != 0) newHard.push(String((long long)prt));
        }

        _rebuild(newHard, _getSoftItems(), hashtag(), _getQueryItems());
    }

    u16 port() const {
        if (size() == 0) return 0;
        if ((*this)[0] == "4") return size() >= 6 ? (u16)parseLong((*this)[5]) : 0;
        if ((*this)[0] == "6") return size() >= 10 ? (u16)parseLong((*this)[9]) : 0;
        if ((*this)[0] == "5") {
            usz hc = _hardCount();
            usz hs = _hardStartOffset();
            if (hc > 0) {
                String last = (*this)[hs + hc - 1];
                if (_isAllNumeric(last)) return (u16)parseLong(last);
            }
            return 443;
        }
        return 0;
    }

    void port(u16 p) {
        if (size() == 0) return;
        if ((*this)[0] == "4") {
            if (size() >= 6) (*this)[5] = String((long long)p);
            else push(String((long long)p));
        } else if ((*this)[0] == "6") {
            if (size() >= 10) (*this)[9] = String((long long)p);
            else {
                while (size() < 9) push("0");
                push(String((long long)p));
            }
        } else if ((*this)[0] == "5") {
            Array<String> newHard = _getHardItems();
            if (newHard.size() > 0 && _isAllNumeric(newHard[newHard.size() - 1])) {
                newHard[newHard.size() - 1] = String((long long)p);
            } else {
                newHard.push(String((long long)p));
            }
            _rebuild(newHard, _getSoftItems(), hashtag(), _getQueryItems());
        }
    }

    // --- Path / segments ---
    String path() const {
        if (size() == 0 || (*this)[0] != "5") return "";
        usz sc = _softCount();
        usz ss = _softStartOffset();
        String res;
        for (usz i = 0; i < sc; ++i) {
            res += "/";
            res += (*this)[ss + i];
        }
        return res;
    }

    void path(const String& p) {
        if (size() == 0 || (*this)[0] != "5") {
            clear();
            push("5");
            push("get");
            push("0");
            push("0");
            push("");
        }

        bool reset = p.startsWith("/");
        Array<String> parts = p.split("/");
        Array<String> newSoft;
        for (usz i = 0; i < parts.size(); ++i) {
            if (!parts[i].isEmpty() && parts[i] != ".") {
                if (parts[i] == "..") {
                    if (newSoft.size() > 0) newSoft.pop();
                } else {
                    newSoft.push(parts[i]);
                }
            }
        }

        if (reset) {
            _rebuild(_getHardItems(), newSoft, hashtag(), _getQueryItems());
        } else {
            Array<String> currentSoft = _getSoftItems();
            if (currentSoft.size() > 0) {
                currentSoft.pop();
                for (usz i = 0; i < newSoft.size(); ++i) currentSoft.push(newSoft[i]);
            } else {
                currentSoft = newSoft;
            }
            _rebuild(_getHardItems(), currentSoft, hashtag(), _getQueryItems());
        }
    }

    // --- Hashtag ---
    String hashtag() const {
        if (size() == 0 || (*this)[0] != "5") return "";
        usz hOffset = _hashtagOffset();
        if (hOffset < size()) return (*this)[hOffset];
        return "";
    }

    void hashtag(const String& h) {
        if (size() == 0 || (*this)[0] != "5") return;
        _rebuild(_getHardItems(), _getSoftItems(), h, _getQueryItems());
    }

    // --- Credentials ---
    String username() const {
        if (size() == 0 || (*this)[0] != "5") return "";
        usz hc = _hardCount();
        usz hs = _hardStartOffset();
        for (usz i = 0; i < hc; ++i) {
            if ((*this)[hs + i].startsWith("usr:"))
                return (*this)[hs + i].substring(4);
        }
        return "";
    }

    void username(const String& u) {
        if (size() == 0 || (*this)[0] != "5") return;
        Array<String> newHard = _getHardItems();
        bool found = false;
        for (usz i = 0; i < newHard.size(); ++i) {
            if (newHard[i].startsWith("usr:")) {
                newHard[i] = String("usr:") + u;
                found = true;
                break;
            }
        }
        if (!found) {
            usz insertPos = newHard.size();
            if (newHard.size() > 0 && _isAllNumeric(newHard[newHard.size() - 1])) insertPos--;
            newHard.splice(insertPos, 0);
            newHard[insertPos] = String("usr:") + u;
        }
        _rebuild(newHard, _getSoftItems(), hashtag(), _getQueryItems());
    }

    String password() const {
        if (size() == 0 || (*this)[0] != "5") return "";
        usz hc = _hardCount();
        usz hs = _hardStartOffset();
        for (usz i = 0; i < hc; ++i) {
            if ((*this)[hs + i].startsWith("pwd:"))
                return (*this)[hs + i].substring(4);
        }
        return "";
    }

    void password(const String& p) {
        if (size() == 0 || (*this)[0] != "5") return;
        Array<String> newHard = _getHardItems();
        bool found = false;
        for (usz i = 0; i < newHard.size(); ++i) {
            if (newHard[i].startsWith("pwd:")) {
                newHard[i] = String("pwd:") + p;
                found = true;
                break;
            }
        }
        if (!found) {
            usz insertPos = newHard.size();
            if (newHard.size() > 0 && _isAllNumeric(newHard[newHard.size() - 1])) insertPos--;
            newHard.splice(insertPos, 0);
            newHard[insertPos] = String("pwd:") + p;
        }
        _rebuild(newHard, _getSoftItems(), hashtag(), _getQueryItems());
    }

    void deleteCreds() {
        if (size() == 0 || (*this)[0] != "5") return;
        Array<String> newHard = _getHardItems();
        for (usz i = 0; i < newHard.size(); ++i) {
            if (newHard[i].startsWith("usr:") || newHard[i].startsWith("pwd:")) {
                newHard.splice(i, 1);
                i--;
            }
        }
        _rebuild(newHard, _getSoftItems(), hashtag(), _getQueryItems());
    }

    // --- Query Parameters ---
    String query(const String& key) const {
        if (size() == 0 || (*this)[0] != "5") return "";
        usz qs = _queryStartOffset();
        for (usz i = qs; i + 1 < size(); i += 2) {
            if ((*this)[i] == key) return (*this)[i + 1];
        }
        return "";
    }

    void query(const String& key, const String& value) {
        if (size() == 0 || (*this)[0] != "5") {
            clear();
            push("5");
            push("get");
            push("0");
            push("0");
            push("");
        }
        Array<String> newQueries = _getQueryItems();
        bool found = false;
        for (usz i = 0; i + 1 < newQueries.size(); i += 2) {
            if (newQueries[i] == key) {
                newQueries[i + 1] = value;
                found = true;
                break;
            }
        }
        if (!found) {
            newQueries.push(key);
            newQueries.push(value);
        }
        _rebuild(_getHardItems(), _getSoftItems(), hashtag(), newQueries);
    }

    Array<String> queryAll(const String& key) const {
        Array<String> res;
        if (size() == 0 || (*this)[0] != "5") return res;
        usz qs = _queryStartOffset();
        for (usz i = qs; i + 1 < size(); i += 2) {
            if ((*this)[i] == key) res.push((*this)[i + 1]);
        }
        return res;
    }

    bool isHalt() const {
        if (size() > 1 && (*this)[0] == "7" && ((*this)[1] == "halt" || (*this)[1] == "4294967295")) return true;
        if (size() > 0 && (*this)[0] == "4294967295") return true;
        return false;
    }
    bool isControl() const { return size() > 0 && (*this)[0] == "7"; }
    bool isIPv4() const { return size() > 0 && (*this)[0] == "4"; }
    bool isIPv6() const { return size() > 0 && (*this)[0] == "6"; }
    bool isName() const {
        if (size() == 0) return false;
        char c = (*this)[0][0];
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    }

    Array<u8> ipv4() const {
        Array<u8> res;
        if (!isIPv4() || size() < 5) return res;
        for (usz i = 1; i <= 4; ++i) res.push((u8)parseLong((*this)[i]));
        return res;
    }

    Array<u16> ipv6() const {
        Array<u16> res;
        if (!isIPv6() || size() < 9) return res;
        for (usz i = 1; i <= 8; ++i) res.push((u16)parseLong((*this)[i]));
        return res;
    }

    Path common(const Path& other) const {
        Path res;
        usz minLen = size() < other.size() ? size() : other.size();
        for (usz i = 0; i < minLen; i++) {
            if ((*this)[i] == other[i])
                res.push((*this)[i]);
            else
                break;
        }
        return res;
    }

    Path commonPath(const Path& other) const {
        Path res;
        if (size() == 0 || (*this)[0] != "5" || other.size() == 0 || other[0] != "5") return res;
        usz sc1 = _softCount();
        usz ss1 = _softStartOffset();
        usz sc2 = other._softCount();
        usz ss2 = other._softStartOffset();
        usz minLen = sc1 < sc2 ? sc1 : sc2;
        for (usz i = 0; i < minLen; ++i) {
            if ((*this)[ss1 + i] == other[ss2 + i]) {
                res.push((*this)[ss1 + i]);
            } else {
                break;
            }
        }
        return res;
    }

    // --- Compatibility Methods ---
    Path address() const {
        return Path(hostname());
    }

    Path address() {
        return Path(hostname());
    }

    bool includesNames() const { return !isNumerical(); }

    Path beforeNamed() const {
        Path res;
        for (usz i = 0; i < size(); i++) {
            if (!_isAllNumeric((*this)[i])) break;
            res.push((*this)[i]);
        }
        return res;
    }

    Path named() const {
        Path res;
        bool found = false;
        for (usz i = 0; i < size(); i++) {
            if (!found && !_isAllNumeric((*this)[i])) found = true;
            if (found) res.push((*this)[i]);
        }
        return res;
    }

    String toString(bool forwardSlash = true, bool protocolAndAddress = true,
                    bool withQuery = true) const {
        if (size() == 0) return "";
        if ((*this)[0] == "4") {
            if (size() < 5) return "";
            String res = (*this)[1] + "." + (*this)[2] + "." + (*this)[3] + "." + (*this)[4];
            if (size() >= 6) {
                res += ":" + (*this)[5];
            }
            return res;
        }
        if ((*this)[0] == "6") {
            if (size() < 9) return "";
            String res = "[";
            for (usz i = 1; i <= 8; i++) {
                if (i > 1) res += ":";
                res += (*this)[i];
            }
            res += "]";
            if (size() >= 10) {
                res += ":" + (*this)[9];
            }
            return res;
        }
        if ((*this)[0] == "7") return (*this)[1];

        if ((*this)[0] == "5") {
            String res;
            if (protocolAndAddress) {
                res += protocol() + "://";
                String usr = username();
                String pwd = password();
                if (!usr.isEmpty()) {
                    res += usr;
                    if (!pwd.isEmpty()) res += ":" + pwd;
                    res += "@";
                }
                usz hc = _hardCount();
                usz hs = _hardStartOffset();
                String hostDomain;
                long long lastDomainSeg = -1;
                for (usz i = 0; i < hc; ++i) {
                    String item = (*this)[hs + i];
                    if (!item.startsWith("usr:") && !item.startsWith("pwd:") && !_isAllNumeric(item)) {
                        lastDomainSeg = (long long)i;
                    }
                }
                for (long long i = lastDomainSeg; i >= 0; --i) {
                    if (!hostDomain.isEmpty()) hostDomain += ".";
                    hostDomain += (*this)[hs + (usz)i];
                }
                res += hostDomain;

                u16 prt = port();
                if (prt != 0 && prt != 443) res += ":" + String((long long)prt);
            }

            const char* sep = forwardSlash ? "/" : "\\";
            usz sc = _softCount();
            usz ss = _softStartOffset();
            if (protocolAndAddress && sc > 0) {
                res += "/";
            }
            for (usz i = 0; i < sc; ++i) {
                if (i > 0) res += sep;
                res += (*this)[ss + i];
            }

            if (withQuery) {
                String hash = hashtag();
                if (!hash.isEmpty()) res += "#" + hash;

                usz qs = _queryStartOffset();
                bool first = true;
                for (usz i = qs; i + 1 < size(); i += 2) {
                    if (first) res += "?";
                    else res += "&";
                    res += (*this)[i];
                    if (!(*this)[i + 1].isEmpty()) {
                        res += "=" + (*this)[i + 1];
                    }
                    first = false;
                }
            }
            return res;
        }

        const char* sep = forwardSlash ? "/" : "\\";
        String res;
        for (usz i = 0; i < size(); i++) {
            if (i > 0) res += sep;
            res += (*this)[i];
        }
        return res;
    }
};

inline NumericalPath::NumericalPath(const Path &hn) {
    if (hn.isHalt()) {
        push(0xFFFFFFFF);
        return;
    }
    if (hn.isNumerical()) {
        for (usz i = 0; i < hn.size(); i++)
            push((u32)parseLong(hn[i]));
    } else {
        for (usz i = 0; i < hn.size(); i++) {
            if (_strIsNumeric(hn[i])) push((u32)parseLong(hn[i]));
            else push(0);
        }
    }
}

using Address = Path;

} // namespace Resource

#endif // XI_CORE_PATH_HPP