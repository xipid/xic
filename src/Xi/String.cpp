#include <Xi/String.hpp>
#include <Xi/Regex.hpp>

namespace Xi {

int parseInt(const String &s) {
    const u8 *d = const_cast<String &>(s).data();
    usz length = s.size();
    if (length == 0 || !d)
        return 0;
    int result = 0;
    int sign = (d[0] == '-') ? -1 : 1;
    usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;
    for (; i < length; ++i) {
        if (d[i] >= '0' && d[i] <= '9')
            result = result * 10 + (d[i] - '0');
        else
            break;
    }
    return result * sign;
}

long long parseLong(const String &s) {
    const u8 *d = const_cast<String &>(s).data();
    usz length = s.size();
    if (length == 0 || !d)
        return 0;
    long long result = 0;
    long long sign = (d[0] == '-') ? -1 : 1;
    usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;
    for (; i < length; ++i) {
        if (d[i] >= '0' && d[i] <= '9')
            result = result * 10 + (d[i] - '0');
        else
            break;
    }
    return result * sign;
}

f64 parseDouble(const String &s) {
    const u8 *d = const_cast<String &>(s).data();
    usz length = s.size();
    if (length == 0 || !d)
        return 0.0;
    f64 result = 0.0;
    f64 sign = (d[0] == '-') ? -1.0 : 1.0;
    usz i = (d[0] == '-' || d[0] == '+') ? 1 : 0;
    while (i < length && d[i] >= '0' && d[i] <= '9') {
        result = result * 10.0 + (d[i] - '0');
        i++;
    }
    if (i < length && d[i] == '.') {
        i++;
        f64 weight = 0.1;
        while (i < length && d[i] >= '0' && d[i] <= '9') {
            result += (d[i] - '0') * weight;
            weight /= 10.0;
            i++;
        }
    }
    if (i < length && (d[i] == 'e' || d[i] == 'E')) {
        i++;
        int expSign = 1;
        if (i < length && d[i] == '-') {
            expSign = -1;
            i++;
        } else if (i < length && d[i] == '+')
            i++;
        int expVal = 0;
        while (i < length && d[i] >= '0' && d[i] <= '9') {
            expVal = expVal * 10 + (d[i] - '0');
            i++;
        }
        f64 factor = 1.0;
        f64 base = 10.0;
        int p = expVal;
        while (p > 0) {
            if (p & 1)
                factor *= base;
            base *= base;
            p >>= 1;
        }
        if (expSign == -1)
            result /= factor;
        else
            result *= factor;
    }
    return result * sign;
}

usz String::str_len(const char *s) {
    if (!s) return 0;
    usz len = 0;
    while (s[len]) len++;
    return len;
}

void String::append_raw(const u8 *b, usz c) { pushEach(b, c); }

void String::append_f32(f64 n, int precision) {
    if (n < 0) {
        push('-');
        n = -n;
    }
    append_int((long long)n);
    push('.');
    f64 frac = n - (long long)n;
    for (int i = 0; i < precision; i++) {
        frac *= 10;
        int digit = (int)frac;
        push(digit + '0');
        frac -= digit;
    }
}

void String::concat(const String &other) { pushEach(other.data(), other.size()); }

String::String(const char *s) { if (s) append_raw((const u8 *)s, str_len(s)); }

String::String(const u8 *buffer, usz l) : InlineArray<u8>() {
    if (buffer && l > 0) {
        append_raw(buffer, l);
    }
}

void String::setFromRawAddress(unsigned long long ptrAddr, usz len) {
    destroy();
    if (len == 0 || ptrAddr == 0)
        return;
    const u8 *ptr = reinterpret_cast<const u8 *>(ptrAddr);
    this->append_raw(ptr, len);
}

String::String(int n) : InlineArray<u8>() { append_int(n); }
String::String(long long n) : InlineArray<u8>() { append_int(n); }
String::String(u64 n) : InlineArray<u8>() { append_int(n); }
String::String(f64 n) : InlineArray<u8>() { append_f32(n); }
String::String(f32 n) : InlineArray<u8>() { append_f32((f64)n); }

const char *String::c_str() {
    if (size() == 0)
        return "";
    push(0);
    char *ptr = (char *)data();
    pop();
    return ptr;
}

void String::fill(u8 val) {
    u8 *d = data();
    usz n = size();
    for (usz i = 0; i < n; i++)
        d[i] = val;
}

long long String::indexOf(const char *needle, usz start) const {
    return find(needle, start);
}

long long String::indexOf(char needle, usz start) const {
    usz n = size();
    const u8* d = data();
    for (usz i = start; i < n; i++) {
        if (d[i] == (u8)needle)
            return (long long)i;
    }
    return -1;
}

bool String::includes(const char *needle, usz start) const {
    return find(needle, start) != -1;
}

bool String::startsWith(const char *prefix) const {
    usz pLen = str_len(prefix);
    if (pLen > size())
        return false;
    const u8 *d = data();
    const u8 *p = (const u8 *)prefix;
    for (usz i = 0; i < pLen; i++)
        if (d[i] != p[i])
            return false;
    return true;
}

bool String::endsWith(const char *suffix) const {
    usz sLen = str_len(suffix);
    if (sLen > size())
        return false;
    const u8 *d = data();
    const u8 *s = (const u8 *)suffix;
    usz start = size() - sLen;
    for (usz i = 0; i < sLen; i++)
        if (d[start + i] != s[i])
            return false;
    return true;
}

String String::slice(long long start, long long end) const {
    long long len = (long long)size();
    if (start < 0)
        start = len + start;
    if (end < 0) {
        if (end == -1 && size() > 0)
            end = len;
        else
            end = len + end;
    }
    if (start < 0)
        start = 0;
    if (end > len)
        end = len;
    if (start >= end)
        return String();
    return begin((usz)start, (usz)end);
}

String String::substring(usz start, usz end) const {
    if (end == (usz)-1)
        end = size();
    if (start >= end)
        return String();
    if (end > size())
        end = size();
    return begin(start, end);
}

String String::trim() const {
    if (size() == 0)
        return String();
    const u8 *d = data();
    usz n = size();
    usz s = 0;
    while (s < n && d[s] <= ' ')
        s++;
    if (s == n)
        return String();
    usz e = n - 1;
    while (e > s && d[e] <= ' ')
        e--;
    return begin(s, e + 1);
}

String String::toUpperCase() const {
    String res = *this;
    res.InlineArray<u8>::allocate(size());
    u8 *d = res.data();
    usz n = res.size();
    for (usz i = 0; i < n; i++) {
        if (d[i] >= 'a' && d[i] <= 'z')
            d[i] -= 32;
    }
    return res;
}

String String::toLowerCase() const {
    String res = *this;
    res.InlineArray<u8>::allocate(size());
    u8 *d = res.data();
    usz n = res.size();
    for (usz i = 0; i < n; i++) {
        if (d[i] >= 'A' && d[i] <= 'Z')
            d[i] += 32;
    }
    return res;
}

char String::charAt(usz idx) const {
    if (idx >= size())
        return 0;
    return (char)operator[](idx);
}

int String::charCodeAt(usz idx) const {
    if (idx >= size())
        return -1;
    return (int)operator[](idx);
}

String String::padStart(usz targetLen, char padChar) const {
    if (size() >= targetLen)
        return *this;
    String res;
    usz toAdd = targetLen - size();
    for (usz i = 0; i < toAdd; i++)
        res.push((u8)padChar);
    res += *this;
    return res;
}

String String::padEnd(usz targetLen, char padChar) const {
    if (size() >= targetLen)
        return *this;
    String res = *this;
    usz toAdd = targetLen - size();
    for (usz i = 0; i < toAdd; i++)
        res.push((u8)padChar);
    return res;
}

String &String::operator+=(const char *s) {
    append_raw((const u8 *)s, str_len(s));
    return *this;
}
String &String::operator+=(const String &o) {
    concat(o);
    return *this;
}
String &String::operator+=(char c) {
    push((u8)c);
    return *this;
}
String &String::operator+=(int n) {
    append_int(n);
    return *this;
}
String &String::operator+=(long long n) {
    append_int(n);
    return *this;
}

bool String::operator==(const String &other) const {
    if (this == &other)
        return true;
    usz n = size();
    if (n != other.size())
        return false;

    const u8 *d1 = data();
    const u8 *d2 = other.data();
    if (!d1 || !d2)
        return d1 == d2;

    for (usz i = 0; i < n; ++i)
        if (d1[i] != d2[i])
            return false;
    return true;
}

bool String::operator==(const char *other) const {
    usz oLen = str_len(other);
    usz n = size();
    if (n != oLen)
        return false;
    const u8 *d = data();
    for (usz i = 0; i < oLen; ++i)
        if (d[i] != (u8)other[i])
            return false;
    return true;
}

long long String::find(const char *needle, usz start) const {
    usz nLen = str_len(needle);
    usz hLen = size();
    if (nLen == 0 || hLen < nLen)
        return -1;
    const u8 *h = data();
    const u8 *n = (const u8 *)needle;
    for (usz i = start; i <= hLen - nLen; ++i) {
        usz j = 0;
        while (j < nLen && h[i + j] == n[j])
            j++;
        if (j == nLen)
            return (long long)i;
    }
    return -1;
}

Array<String> String::split(const char *sep) const {
    Array<String> res;
    usz sLen = str_len(sep);
    if (sLen == 0)
        return res;

    String *mut = const_cast<String *>(this);
    usz totalLen = size();
    Array<long long> indices;
    long long pos = find(sep, 0);
    while (pos != -1) {
        indices.push(pos);
        pos = find(sep, (usz)pos + sLen);
    }

    usz curr = 0;
    usz cnt = indices.size();
    for (usz i = 0; i < cnt; i++) {
        usz idx = (usz)indices[i];
        if (idx > totalLen)
            break;
        res.push(mut->begin(curr, idx));
        curr = idx + sLen;
    }
    if (curr <= totalLen)
        res.push(mut->begin(curr, totalLen));
    return res;
}

String String::replace(const char *tgt, const char *rep) const {
    Array<String> parts = split(tgt);
    String res;
    bool first = true;
    usz n = parts.size();
    for (usz i = 0; i < n; ++i) {
        if (!first)
            res += rep;
        res += parts[i];
        first = false;
    }
    return res;
}

bool String::constantTimeEquals(const Xi::String &b, int length) const {
    const u8 *ad = data();
    const u8 *bd = b.data();
    usz actualALen = this->size();
    usz actualBLen = b.size();

    usz compareLen = (length > 0)
                         ? static_cast<usz>(length)
                         : (actualALen > actualBLen ? actualALen : actualBLen);

    u8 result = 0;

    if (length > 0 && (actualALen < (usz)length || actualBLen < (usz)length)) {
        result = 1;
    } else if (length == 0 && actualALen != actualBLen) {
        result = 1;
    }

    for (usz i = 0; i < compareLen; ++i) {
        u8 aByte = (i < actualALen) ? ad[i] : 0;
        u8 bByte = (i < actualBLen) ? bd[i] : 0;
        result |= (aByte ^ bByte);
    }

    return result == 0;
}

String *String::pushVarLong(long long v) {
    unsigned long long n = (unsigned long long)v;
    do {
        u8 t = n & 0x7f;
        n >>= 7;
        if (n)
            t |= 0x80;
        push(t);
    } while (n);
    return this;
}

long long String::shiftVarLong() {
    unsigned long long r = 0;
    int s = 0;
    u8 b;
    do {
        if (s >= 70 || size() == 0)
            return 0;
        b = shift();
        r |= (unsigned long long)(b & 0x7f) << s;
        s += 7;
    } while (b & 0x80);
    return (long long)r;
}

String *String::unshiftVarLong(long long v) {
    unsigned long long n = (unsigned long long)v;
    InlineArray<u8> temp;
    do {
        u8 t = n & 0x7f;
        n >>= 7;
        if (n)
            t |= 0x80;
        temp.push(t);
    } while (n);
    for (long long i = (long long)temp.size() - 1; i >= 0; i--)
        unshift(temp[i]);
    return this;
}

void String::pushVarString(const String &s) {
    *this += s.serialize();
}

String String::shiftVarString() {
    usz at = 0;
    String res = InlineArray<u8>::deserialize(*this, at);
    for (usz i = 0; i < at; i++)
        shift();
    return res;
}

String *String::pushBool(bool v) {
    push(v ? 1 : 0);
    return this;
}
bool String::shiftBool() { return size() > 0 ? (shift() != 0) : false; }

String *String::pushI64(long long v) {
    for (int i = 0; i < 8; i++)
        push((u8)((v >> (i * 8)) & 0xff));
    return this;
}
long long String::shiftI64() {
    if (size() < 8)
        return 0;
    unsigned long long r = 0;
    for (int i = 0; i < 8; i++)
        r |= (unsigned long long)shift() << (i * 8);
    return (long long)r;
}

String *String::pushF64(f64 v) {
    union { f64 f; long long i; } u;
    u.f = v;
    return pushI64(u.i);
}
f64 String::shiftF64() {
    union { f64 f; long long i; } u;
    u.i = shiftI64();
    return u.f;
}

String *String::pushF32(f32 v) {
    union { f32 f; u32 i; } u;
    u.f = v;
    for (int i = 0; i < 4; i++)
        push((u8)((u.i >> (i * 8)) & 0xff));
    return this;
}
f32 String::shiftF32() {
    if (size() < 4)
        return 0.0f;
    u32 r = 0;
    for (int i = 0; i < 4; i++)
        r |= (u32)shift() << (i * 8);
    union { f32 f; u32 i; } u;
    u.i = r;
    return u.f;
}

VarLongResult String::peekVarLong(usz offset) const {
    unsigned long long r = 0;
    int s = 0;
    int i = 0;
    usz n = size();
    if (offset >= n)
        return {0, 0, true};
    const u8 *d = data();
    for (i = (int)offset; i < (int)n; ++i) {
        u8 b = d[i];
        r |= (unsigned long long)(b & 0x7f) << s;
        s += 7;
        if (!(b & 0x80))
            return {(long long)r, (i - (int)offset) + 1, false};
        if (s >= 70)
            break;
    }
    return {0, 0, true};
}

Xi::String String::toDeci() const {
    Xi::String result;
    usz n = size();
    for (usz i = 0; i < n; ++i) {
        u8 value = (*this)[i];
        if (value == 0)
            result.push('0');
        else {
            char buffer[4];
            int pos = 0;
            while (value > 0) {
                buffer[pos++] = (value % 10) + '0';
                value /= 10;
            }
            for (int j = pos - 1; j >= 0; --j)
                result.push(buffer[j]);
        }
        if (i < n - 1)
            result.push(' ');
    }
    return result;
}

} // namespace Xi
