#ifndef XI_CORE_ROUTER_HPP
#define XI_CORE_ROUTER_HPP

#include "Path.hpp"
#include "../Encoding/Regex.hpp"
#include "../Xi/Func.hpp"
#include "../Collection/Array.hpp"
#include "../Collection/Map.hpp"
#include <type_traits>
#include <utility>

namespace Resource {

using namespace Xi;
using namespace Collection;
using namespace Encoding;

class Router;

// Route callback signatures
using RouteCallback = Func<Path(Router&, Path&, void*)>;
using RegexRouteCallback = Func<Path(const RegexMatch&, Router&, Path&, void*)>;

// ---------------------------------------------------------------------------
// Type traits for compile-time signature resolution
// ---------------------------------------------------------------------------
template <typename T, typename = void>
struct has_station_type : std::false_type {};

template <typename T>
struct has_station_type<T, std::void_t<typename T::StationType>> : std::true_type {};

template <typename T, typename = void>
struct has_source_field : std::false_type {};

template <typename T>
struct has_source_field<T, std::void_t<decltype(std::declval<T>().source)>> : std::true_type {};

// ---------------------------------------------------------------------------
// RoutePhase and MatchResult structs
// ---------------------------------------------------------------------------
enum class RoutePhase {
    Before,
    Main,
    After,
    Ready
};

struct MatchResult {
    enum class Type { Static, Regex };
    Type type;
    RouteCallback staticCb;
    RegexRouteCallback regexCb;
    RegexMatch regexMatch;
    String name;
    i32 weight = 0;
    usz depth = 0;
};

// ---------------------------------------------------------------------------
// RouterImpl — Intrusive reference-counted implementation details
// ---------------------------------------------------------------------------
class RouterImpl {
public:
    usz refCount = 1;

    void addRef() { refCount++; }
    void release() {
        if (--refCount == 0) {
            delete this;
        }
    }

    // --- Numeric Trie Node ---
    struct NumericTrieNode {
        void* station = nullptr;
        u32 part = 0;
        i32 weight = 0;
        Array<NumericTrieNode> children;
        NumericTrieNode* parent = nullptr;

        NumericTrieNode() {}
        NumericTrieNode(u32 p, void* st, NumericTrieNode* prnt, i32 w)
            : station(st), part(p), parent(prnt), weight(w) {}
    };
    Array<NumericTrieNode> numericEntries;

    // --- String Trie Node ---
    struct StringTrieNode {
        String segment;
        Map<String, StringTrieNode*> children;
        StringTrieNode* wildcardChild = nullptr;

        struct Handler {
            String method;
            String name;
            i32 weight = 0;
            RoutePhase phase = RoutePhase::Main;
            RouteCallback staticCallback;
        };
        Array<Handler> handlers;

        ~StringTrieNode() {
            for (auto it = children.begin(); it != children.end(); ++it) {
                delete it->value;
            }
            delete wildcardChild;
        }
    };
    StringTrieNode stringRoot;

    // --- Regex Route Node ---
    struct RegexRoute {
        String pattern;
        Regex* regex = nullptr;
        String method;
        String name;
        i32 weight = 0;
        RoutePhase phase = RoutePhase::Main;
        RegexRouteCallback callback;

        ~RegexRoute() {
            delete regex;
        }
    };
    Array<RegexRoute*> regexRoutes;

    ~RouterImpl() {
        for (usz i = 0; i < regexRoutes.size(); ++i) {
            delete regexRoutes[i];
        }
    }

    // --- Numeric Trie Traversal Helpers ---
    NumericTrieNode* _findExactNumeric(const NumericalPath& addr) {
        if (addr.size() == 0) return nullptr;
        Array<NumericTrieNode>* level = &numericEntries;
        NumericTrieNode* current = nullptr;
        for (usz depth = 0; depth < addr.size(); ++depth) {
            u32 part = addr[depth];
            bool found = false;
            for (usz i = 0; i < level->size(); ++i) {
                if ((*level)[i].part == part) {
                    current = &(*level)[i];
                    level = &current->children;
                    found = true;
                    break;
                }
            }
            if (!found) return nullptr;
        }
        return current;
    }

    Array<NumericTrieNode*> _buildSourcePath(const NumericalPath& source) {
        Array<NumericTrieNode*> path;
        Array<NumericTrieNode>* level = &numericEntries;
        for (usz d = 0; d < source.size(); ++d) {
            u32 part = source[d];
            bool found = false;
            for (usz i = 0; i < level->size(); ++i) {
                if ((*level)[i].part == part) {
                    path.push(&(*level)[i]);
                    level = &(*level)[i].children;
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
        return path;
    }

    void _findBestMatch(Array<NumericTrieNode>& level,
                        const NumericalPath& target,
                        usz depth, i32 accumWeight,
                        NumericTrieNode*& best, i32& bestScore, usz& bestDepth) {
        if (depth >= target.size()) return;

        u32 part = target[depth];
        for (usz i = 0; i < level.size(); ++i) {
            if (level[i].part == part) {
                i32 w = accumWeight + level[i].weight;
                if (level[i].station) {
                    if (depth + 1 > bestDepth || (depth + 1 == bestDepth && w > bestScore)) {
                        best = &level[i];
                        bestScore = w;
                        bestDepth = depth + 1;
                    }
                }
                _findBestMatch(level[i].children, target, depth + 1, w, best, bestScore, bestDepth);
                return;
            }
        }
    }

    void _findBestViaSource(const NumericalPath& source, const NumericalPath& target,
                            NumericTrieNode*& best, i32& bestScore, usz& bestDepth) {
        Array<NumericTrieNode*> sourcePath = _buildSourcePath(source);

        for (long long si = (long long)sourcePath.size() - 1; si >= 0; --si) {
            NumericTrieNode* ancestor = sourcePath[(usz)si];
            Array<NumericTrieNode>* siblings;
            if (ancestor->parent) {
                siblings = &ancestor->parent->children;
            } else {
                siblings = &numericEntries;
            }
            
            i32 baseWeight = 0;
            for (usz k = 0; k <= (usz)si; ++k) {
                baseWeight += sourcePath[k]->weight;
            }

            for (usz j = 0; j < siblings->size(); ++j) {
                if (&(*siblings)[j] == ancestor) continue;
                
                if (target.size() > 0 && (*siblings)[j].part == target[(usz)si > 0 ? (usz)si : 0]) {
                    i32 w = baseWeight + (*siblings)[j].weight;
                    if ((*siblings)[j].station) {
                        usz matchDepth = (usz)si + 1;
                        if (matchDepth > bestDepth || (matchDepth == bestDepth && w > bestScore)) {
                            best = &(*siblings)[j];
                            bestScore = w;
                            bestDepth = matchDepth;
                        }
                    }
                    usz nextTargetIdx = (usz)si + 1;
                    if (nextTargetIdx < target.size()) {
                        _findBestMatch((*siblings)[j].children, target, nextTargetIdx, w,
                                      best, bestScore, bestDepth);
                    }
                }
            }
        }

        // Sibling fallback failed, fallback to parent gateway
        if (!best) {
            for (usz i = 0; i < sourcePath.size(); ++i) {
                if (sourcePath[i]->station) {
                    best = sourcePath[i];
                    bestScore = sourcePath[i]->weight;
                    bestDepth = 0;
                    break;
                }
            }
        }
    }

    template <typename First, typename... Rest>
    void _findBestViaSourceInArgs(const NumericalPath& target, NumericTrieNode*& best, i32& bestScore, usz& bestDepth, First& first, Rest&... rest) {
        if constexpr (has_source_field<First>::value) {
            if (!best && first.source.size() > 0) {
                _findBestViaSource(first.source, target, best, bestScore, bestDepth);
            }
        } else {
            if constexpr (sizeof...(Rest) > 0) {
                _findBestViaSourceInArgs(target, best, bestScore, bestDepth, rest...);
            }
        }
    }

    template <typename... Args>
    void routeNumeric(const NumericalPath& target, NumericTrieNode*& best, Args&&... args) {
        i32 bestScore = 0;
        usz bestDepth = 0;
        best = nullptr;

        // 1. Forward match
        _findBestMatch(numericEntries, target, 0, 0, best, bestScore, bestDepth);

        // 2. Source-aware routing
        if constexpr (sizeof...(Args) > 0) {
            _findBestViaSourceInArgs(target, best, bestScore, bestDepth, args...);
        }
    }

    // --- Station Hook API ---
    bool hook(void* station, const NumericalPath& address, i32 weight) {
        if (address.size() == 0) return false;
        
        NumericTrieNode* existing = _findExactNumeric(address);
        if (existing && existing->station) return false;

        Array<NumericTrieNode>* level = &numericEntries;
        NumericTrieNode* current = nullptr;

        for (usz depth = 0; depth < address.size(); ++depth) {
            u32 part = address[depth];
            bool found = false;
            for (usz i = 0; i < level->size(); ++i) {
                if ((*level)[i].part == part) {
                    current = &(*level)[i];
                    level = &current->children;
                    found = true;
                    break;
                }
            }
            if (!found) {
                NumericTrieNode newNode(part, nullptr, current, weight);
                level->push(Xi::Move(newNode));
                current = &(*level)[level->size() - 1];
                level = &current->children;
            }
        }
        if (current) {
            current->station = station;
            current->weight = weight;
        }
        return true;
    }

    NumericalPath generate(const NumericalPath& parent) {
        NumericTrieNode* node = nullptr;
        if (parent.size() > 0) {
            node = _findExactNumeric(parent);
        }

        u32 candidate = 1;
        if (node) {
            while (true) {
                bool found = false;
                for (usz i = 0; i < node->children.size(); ++i) {
                    if (node->children[i].part == candidate) {
                        candidate++;
                        found = true;
                        break;
                    }
                }
                if (!found) break;
            }
        } else if (parent.size() == 0) {
            while (true) {
                bool found = false;
                for (usz i = 0; i < numericEntries.size(); ++i) {
                    if (numericEntries[i].part == candidate) {
                        candidate++;
                        found = true;
                        break;
                    }
                }
                if (!found) break;
            }
        }

        NumericalPath result = parent;
        result.push(candidate);
        return result;
    }

    void unhook(void* station) {
        if (!station) return;
        _unhookStation(numericEntries, station);
    }

    void unhook(const NumericalPath& address) {
        if (address.size() == 0) return;
        NumericTrieNode* node = _findExactNumeric(address);
        if (node) {
            node->station = nullptr;
            _pruneEmpty(node);
        }
    }

    void unhookAll() {
        numericEntries.clear();
    }

    void unhookAll(const NumericalPath& address) {
        if (address.size() == 0) {
            unhookAll();
            return;
        }
        
        if (address.size() == 1) {
            for (usz i = 0; i < numericEntries.size(); ++i) {
                if (numericEntries[i].part == (u32)address[0]) {
                    numericEntries.splice(i, 1);
                    return;
                }
            }
            return;
        }
        
        NumericalPath parentAddr;
        for (usz i = 0; i < address.size() - 1; ++i) parentAddr.push(address[i]);
        NumericTrieNode* parent = _findExactNumeric(parentAddr);
        if (parent) {
            u32 lastPart = address[address.size() - 1];
            for (usz i = 0; i < parent->children.size(); ++i) {
                if (parent->children[i].part == lastPart) {
                    parent->children.splice(i, 1);
                    return;
                }
            }
        }
    }

    bool _findAddress(Array<NumericTrieNode>& level, void* station, NumericalPath& result) {
        for (usz i = 0; i < level.size(); ++i) {
            result.push(level[i].part);
            if (level[i].station == station) return true;
            if (_findAddress(level[i].children, station, result)) return true;
            result.pop();
        }
        return false;
    }

    void _unhookStation(Array<NumericTrieNode>& level, void* station) {
        for (usz i = 0; i < level.size(); ++i) {
            if (level[i].station == station) {
                level[i].station = nullptr;
                if (level[i].children.size() == 0) {
                    level.splice(i, 1);
                    --i;
                    continue;
                }
            }
            _unhookStation(level[i].children, station);
            if (!level[i].station && level[i].children.size() == 0) {
                level.splice(i, 1);
                --i;
            }
        }
    }

    void _pruneEmpty(NumericTrieNode* node) {
        while (node && !node->station && node->children.size() == 0) {
            NumericTrieNode* parent = node->parent;
            if (parent) {
                for (usz i = 0; i < parent->children.size(); ++i) {
                    if (&parent->children[i] == node) {
                        parent->children.splice(i, 1);
                        break;
                    }
                }
            } else {
                for (usz i = 0; i < numericEntries.size(); ++i) {
                    if (&numericEntries[i] == node) {
                        numericEntries.splice(i, 1);
                        break;
                    }
                }
            }
            node = parent;
        }
    }

    // --- String Routing Registration ---
    void registerStaticRoute(const String& method, const String& pattern, RouteCallback cb, const String& name, i32 weight, RoutePhase phase) {
        Array<String> segments;
        Array<String> parts = pattern.split("/");
        for (usz i = 0; i < parts.size(); ++i) {
            if (!parts[i].isEmpty()) {
                segments.push(parts[i]);
            }
        }

        StringTrieNode* current = &stringRoot;
        for (usz i = 0; i < segments.size(); ++i) {
            const String& seg = segments[i];
            if (seg == "") {
                if (!current->wildcardChild) {
                    current->wildcardChild = new StringTrieNode();
                    current->wildcardChild->segment = "";
                }
                current = current->wildcardChild;
            } else {
                auto** pChild = current->children.get(seg);
                if (!pChild) {
                    StringTrieNode* child = new StringTrieNode();
                    child->segment = seg;
                    current->children.put(seg, child);
                    current = child;
                } else {
                    current = *pChild;
                }
            }
        }

        StringTrieNode::Handler h;
        h.method = method;
        h.name = name;
        h.weight = weight;
        h.phase = phase;
        h.staticCallback = cb;
        current->handlers.push(h);
    }

    void registerRegexRoute(const String& method, const String& pattern, RegexRouteCallback cb, const String& name, i32 weight, RoutePhase phase) {
        RegexRoute* r = new RegexRoute();
        r->pattern = pattern;
        r->regex = new Regex(pattern);
        r->method = method;
        r->name = name;
        r->weight = weight;
        r->phase = phase;
        r->callback = cb;
        regexRoutes.push(r);
    }

    // --- Route Removal APIs ---
    void removeStaticRoute(RoutePhase phase, const String& method, const String& pattern) {
        Array<String> segments;
        Array<String> parts = pattern.split("/");
        for (usz i = 0; i < parts.size(); ++i) {
            if (!parts[i].isEmpty()) {
                segments.push(parts[i]);
            }
        }

        StringTrieNode* current = &stringRoot;
        for (usz i = 0; i < segments.size(); ++i) {
            const String& seg = segments[i];
            if (seg == "") {
                if (!current->wildcardChild) return;
                current = current->wildcardChild;
            } else {
                auto** pChild = current->children.get(seg);
                if (!pChild) return;
                current = *pChild;
            }
        }

        for (usz i = 0; i < current->handlers.size(); ++i) {
            auto& h = current->handlers[i];
            if (h.phase == phase && (method.isEmpty() || h.method == method)) {
                current->handlers.splice(i, 1);
                --i;
            }
        }
    }

    void removeRegexRoute(RoutePhase phase, const String& method, const String& pattern) {
        for (usz i = 0; i < regexRoutes.size(); ++i) {
            RegexRoute* r = regexRoutes[i];
            if (r->phase == phase && (method.isEmpty() || r->method == method) && r->pattern == pattern) {
                delete r;
                regexRoutes.splice(i, 1);
                --i;
            }
        }
    }

    void removeRoute(RoutePhase phase, const String& method, const String& pattern) {
        removeStaticRoute(phase, method, pattern);
        removeRegexRoute(phase, method, pattern);
    }

    // --- Match Collection helpers ---
    void _collectStaticMatches(StringTrieNode* current, const Array<String>& segments, usz depth, RoutePhase phase, const String& method, Array<MatchResult>& outMatches) {
        bool isLeaf = (depth == segments.size());
        if (isLeaf || phase != RoutePhase::Main) {
            for (usz i = 0; i < current->handlers.size(); ++i) {
                const auto& h = current->handlers[i];
                if (h.phase == phase && (h.method.isEmpty() || h.method == method)) {
                    MatchResult res;
                    res.type = MatchResult::Type::Static;
                    res.staticCb = h.staticCallback;
                    res.name = h.name;
                    res.weight = h.weight;
                    res.depth = depth;
                    outMatches.push(res);
                }
            }
        }

        if (depth < segments.size()) {
            const String& seg = segments[depth];
            auto** pChild = current->children.get(seg);
            if (pChild) {
                _collectStaticMatches(*pChild, segments, depth + 1, phase, method, outMatches);
            }
            if (current->wildcardChild) {
                _collectStaticMatches(current->wildcardChild, segments, depth + 1, phase, method, outMatches);
            }
        }
    }

    void _collectRegexMatches(RoutePhase phase, const String& method, const Path& path, Array<MatchResult>& outMatches) {
        String pathStr = path.toString();
        for (usz i = 0; i < regexRoutes.size(); ++i) {
            RegexRoute* r = regexRoutes[i];
            if (r->phase == phase && (r->method.isEmpty() || r->method == method)) {
                auto matches = r->regex->matchAll(pathStr, 1);
                if (matches.size() > 0) {
                    MatchResult res;
                    res.type = MatchResult::Type::Regex;
                    res.regexCb = r->callback;
                    res.regexMatch = matches[0];
                    res.name = r->name;
                    res.weight = r->weight;
                    res.depth = r->pattern.size();
                    outMatches.push(res);
                }
            }
        }
    }
};

// ---------------------------------------------------------------------------
// Router — lightweight sub-router wrapper supporting shared state
// ---------------------------------------------------------------------------
class Router {
private:
    RouterImpl* _impl;
    String _prefix;
    String _namePrefix;
    i32 _weight = 0;
    RoutePhase _phase = RoutePhase::Main;

    // Intrusive copy helper
    Router(RouterImpl* impl, const String& prefix, const String& namePrefix, i32 weight, RoutePhase phase = RoutePhase::Main)
        : _impl(impl), _prefix(prefix), _namePrefix(namePrefix), _weight(weight), _phase(phase) {
        if (_impl) _impl->addRef();
    }

    static bool _isRegexPattern(const String& s) {
        for (usz i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '*' || c == '+' || c == '?' || c == '(' || c == ')' || 
                c == '[' || c == ']' || c == '|' || c == '^' || c == '$') {
                return true;
            }
        }
        return false;
    }

    static String _combinePaths(const String& parent, const String& child) {
        if (parent.isEmpty()) return child;
        if (child.isEmpty()) return parent;
        String p = parent;
        String c = child;
        if (p[p.size() - 1] == '/' && p.size() > 1) {
            p.pop();
        }
        if (c[0] != '/') {
            c = String("/") + c;
        }
        return p + c;
    }

    static String _combineNames(const String& parent, const String& child) {
        if (parent.isEmpty()) return child;
        if (child.isEmpty()) return parent;
        String res = parent;
        if (res[res.size() - 1] != '.' && child[0] != '.') {
            res += ".";
        }
        res += child;
        return res;
    }

    static Array<String> _getPathSegments(const Path& path) {
        Array<String> segments;
        if (path.size() > 0 && path[0] == "5") {
            usz sc = path._softCount();
            usz ss = path._softStartOffset();
            for (usz i = 0; i < sc; ++i) {
                segments.push(path[ss + i]);
            }
        } else {
            for (usz i = 0; i < path.size(); ++i) {
                segments.push(path[i]);
            }
        }
        return segments;
    }

    static void _sortMatches(Array<MatchResult>& matches, RoutePhase phase) {
        if (phase == RoutePhase::Main) {
            for (usz i = 1; i < matches.size(); ++i) {
                MatchResult key = matches[i];
                long long j = (long long)i - 1;
                while (j >= 0) {
                    bool swapNeeded = false;
                    if (matches[(usz)j].weight < key.weight) {
                        swapNeeded = true;
                    } else if (matches[(usz)j].weight == key.weight) {
                        if (matches[(usz)j].depth < key.depth) {
                            swapNeeded = true;
                        }
                    }
                    if (swapNeeded) {
                        matches[(usz)j + 1] = matches[(usz)j];
                        j--;
                    } else {
                        break;
                    }
                }
                matches[(usz)j + 1] = key;
            }
        } else {
            for (usz i = 1; i < matches.size(); ++i) {
                MatchResult key = matches[i];
                long long j = (long long)i - 1;
                while (j >= 0) {
                    bool swapNeeded = false;
                    if (matches[(usz)j].weight < key.weight) {
                        swapNeeded = true;
                    } else if (matches[(usz)j].weight == key.weight) {
                        if (matches[(usz)j].depth > key.depth) {
                            swapNeeded = true;
                        }
                    }
                    if (swapNeeded) {
                        matches[(usz)j + 1] = matches[(usz)j];
                        j--;
                    } else {
                        break;
                    }
                }
                matches[(usz)j + 1] = key;
            }
        }
    }

public:
    Router() : _impl(new RouterImpl()), _weight(0), _phase(RoutePhase::Main) {}

    Router(const Router& o) : _impl(o._impl), _prefix(o._prefix), _namePrefix(o._namePrefix), _weight(o._weight), _phase(o._phase) {
        if (_impl) _impl->addRef();
    }

    Router& operator=(const Router& o) {
        if (this != &o) {
            if (_impl) _impl->release();
            _impl = o._impl;
            _prefix = o._prefix;
            _namePrefix = o._namePrefix;
            _weight = o._weight;
            _phase = o._phase;
            if (_impl) _impl->addRef();
        }
        return *this;
    }

    ~Router() {
        if (_impl) _impl->release();
    }

    void destroy() {
        if (_impl) {
            _impl->numericEntries.clear();
            _impl->stringRoot.children.clear();
            delete _impl->stringRoot.wildcardChild;
            _impl->stringRoot.wildcardChild = nullptr;
            for (usz i = 0; i < _impl->regexRoutes.size(); ++i) {
                delete _impl->regexRoutes[i];
            }
            _impl->regexRoutes.clear();
        }
    }

    // --- Sub-Router Wrapper Builders ---
    Router prefix(const String& p) const {
        return Router(_impl, _combinePaths(_prefix, p), _namePrefix, _weight, _phase);
    }

    Router name(const String& n) const {
        return Router(_impl, _prefix, _combineNames(_namePrefix, n), _weight, _phase);
    }

    Router weight(i32 w) const {
        return Router(_impl, _prefix, _namePrefix, w, _phase);
    }

    Router before() const {
        return Router(_impl, _prefix, _namePrefix, _weight, RoutePhase::Before);
    }

    Router after() const {
        return Router(_impl, _prefix, _namePrefix, _weight, RoutePhase::After);
    }

    Router ready() const {
        return Router(_impl, _prefix, _namePrefix, _weight, RoutePhase::Ready);
    }

    // --- Static Hook Routing Compatibility API ---
    bool hook(void* station, const NumericalPath& address) {
        return _impl->hook(station, address, _weight);
    }

    void hookUnder(void* station, const NumericalPath& parent) {
        NumericalPath addr = generate(parent);
        hook(station, addr);
    }

    void unhook(void* station) {
        _impl->unhook(station);
    }

    void unhook(const NumericalPath& address) {
        _impl->unhook(address);
    }

    void unhookAll(const NumericalPath& address) {
        _impl->unhookAll(address);
    }

    void unhookAll() {
        _impl->unhookAll();
    }

    NumericalPath generate(const NumericalPath& parent) {
        return _impl->generate(parent);
    }

    NumericalPath address(void* station) {
        NumericalPath result;
        _impl->_findAddress(_impl->numericEntries, station, result);
        return result;
    }

    Array<RouterImpl::NumericTrieNode*> list(const NumericalPath& addr) {
        Array<RouterImpl::NumericTrieNode*> result;
        if (addr.size() == 0) {
            for (usz i = 0; i < _impl->numericEntries.size(); ++i) {
                result.push(&_impl->numericEntries[i]);
            }
            return result;
        }
        RouterImpl::NumericTrieNode* node = _impl->_findExactNumeric(addr);
        if (node) {
            for (usz i = 0; i < node->children.size(); ++i) {
                result.push(&node->children[i]);
            }
        }
        return result;
    }

    // --- Route Registration APIs ---
    template <typename F>
    Router on(const String& pattern, F&& cb) {
        if constexpr (std::is_invocable_v<F, Router&, Path&, void*>) {
            RouteCallback wrapped(std::forward<F>(cb));
            String fullPath = _combinePaths(_prefix, pattern);
            if (_isRegexPattern(fullPath)) {
                RegexRouteCallback regexWrapped = [wrapped](const RegexMatch&, Router& rtr, Path& path, void* ctx) -> Path {
                    return wrapped(rtr, path, ctx);
                };
                _impl->registerRegexRoute("", fullPath, regexWrapped, _namePrefix, _weight, _phase);
            } else {
                _impl->registerStaticRoute("", fullPath, wrapped, _namePrefix, _weight, _phase);
            }
        } else if constexpr (std::is_invocable_v<F, const RegexMatch&, Router&, Path&, void*>) {
            RegexRouteCallback wrapped(std::forward<F>(cb));
            String fullPath = _combinePaths(_prefix, pattern);
            if (_isRegexPattern(fullPath)) {
                _impl->registerRegexRoute("", fullPath, wrapped, _namePrefix, _weight, _phase);
            } else {
                RouteCallback staticWrapped = [wrapped](Router& rtr, Path& path, void* ctx) -> Path {
                    RegexMatch fake;
                    return wrapped(fake, rtr, path, ctx);
                };
                _impl->registerStaticRoute("", fullPath, staticWrapped, _namePrefix, _weight, _phase);
            }
        }
        return *this;
    }

    template <typename F>
    Router on(F&& cb) {
        return on("", std::forward<F>(cb));
    }

    template <typename F>
    Router get(const String& pattern, F&& cb) {
        _registerHTTPHelper("GET", pattern, std::forward<F>(cb));
        return *this;
    }

    template <typename F>
    Router post(const String& pattern, F&& cb) {
        _registerHTTPHelper("POST", pattern, std::forward<F>(cb));
        return *this;
    }

    template <typename F>
    Router put(const String& pattern, F&& cb) {
        _registerHTTPHelper("PUT", pattern, std::forward<F>(cb));
        return *this;
    }

    template <typename F>
    Router del(const String& pattern, F&& cb) {
        _registerHTTPHelper("DELETE", pattern, std::forward<F>(cb));
        return *this;
    }

    template <typename F>
    Router delete_(const String& pattern, F&& cb) {
        return del(pattern, std::forward<F>(cb));
    }

    template <typename F>
    Router patch(const String& pattern, F&& cb) {
        _registerHTTPHelper("PATCH", pattern, std::forward<F>(cb));
        return *this;
    }

    // --- Route Removal APIs ---
    Router off(const String& pattern) {
        String fullPath = _combinePaths(_prefix, pattern);
        _impl->removeRoute(_phase, "", fullPath);
        return *this;
    }

    Router off(const String& method, const String& pattern) {
        String fullPath = _combinePaths(_prefix, pattern);
        _impl->removeRoute(_phase, method, fullPath);
        return *this;
    }

    // --- Resolution Entry API ---
    template <typename... Args>
    Path resolve(const Path& path, Args&&... args) {
        if (path.isHalt()) return path;
        if (path.isNumerical()) {
            NumericalPath numPath = path.toNumerical();
            return _resolveNumeric(numPath, std::forward<Args>(args)...);
        } else {
            return _resolveString(path, std::forward<Args>(args)...);
        }
    }

    template <typename... Args>
    void route(Args&&... args) {
        if constexpr (sizeof...(Args) > 0) {
            _routeNumericCart(std::forward<Args>(args)...);
        }
    }

private:
    template <typename F>
    void _registerHTTPHelper(const String& method, const String& pattern, F&& cb) {
        if constexpr (std::is_invocable_v<F, Router&, Path&, void*>) {
            _registerHTTP(method, pattern, RouteCallback(std::forward<F>(cb)));
        } else if constexpr (std::is_invocable_v<F, const RegexMatch&, Router&, Path&, void*>) {
            _registerHTTPRegex(method, pattern, RegexRouteCallback(std::forward<F>(cb)));
        } else {
            if constexpr (std::is_pointer_v<std::decay_t<F>>) {
                _registerHTTPStation(method, pattern, cb);
            } else {
                _registerHTTPStation(method, pattern, &cb);
            }
        }
    }

    template <typename StationType>
    void _registerHTTPStation(const String& method, const String& pattern, StationType* station) {
        RouteCallback cb = [station](Router& rtr, Path& path, void* ctx) -> Path {
            if (ctx) {
                using CartType = typename StationType::CartType;
                (*station)(*static_cast<CartType*>(ctx));
            }
            return path;
        };
        _registerHTTP(method, pattern, cb);
    }

    void _registerHTTP(const String& method, const String& pattern, RouteCallback cb) {
        String fullPath = _combinePaths(_prefix, pattern);
        if (_isRegexPattern(fullPath)) {
            RegexRouteCallback wrapped = [cb](const RegexMatch&, Router& rtr, Path& path, void* ctx) -> Path {
                return cb(rtr, path, ctx);
            };
            _impl->registerRegexRoute(method, fullPath, wrapped, _namePrefix, _weight, _phase);
        } else {
            _impl->registerStaticRoute(method, fullPath, cb, _namePrefix, _weight, _phase);
        }
    }

    void _registerHTTPRegex(const String& method, const String& pattern, RegexRouteCallback cb) {
        String fullPath = _combinePaths(_prefix, pattern);
        if (_isRegexPattern(fullPath)) {
            _impl->registerRegexRoute(method, fullPath, cb, _namePrefix, _weight, _phase);
        } else {
            RouteCallback wrapped = [cb](Router& rtr, Path& path, void* ctx) -> Path {
                RegexMatch fake;
                return cb(fake, rtr, path, ctx);
            };
            _impl->registerStaticRoute(method, fullPath, wrapped, _namePrefix, _weight, _phase);
        }
    }

    template <typename... Args>
    Path _resolveNumeric(const NumericalPath& target, Args&&... args) {
        if (target.isHalt()) return Path(target.toString());
        RouterImpl::NumericTrieNode* best = nullptr;
        if constexpr (sizeof...(Args) > 0) {
            _impl->routeNumeric(target, best, std::forward<Args>(args)...);
        } else {
            _impl->routeNumeric(target, best);
        }
        if (best && best->station) {
            _invokeStation(best->station, std::forward<Args>(args)...);
        }
        return Path(target.toString());
    }

    template <typename... Args>
    void _routeNumericCart(Args&&... args) {
        if constexpr (sizeof...(Args) > 0) {
            _routeNumericCartHelper(std::forward<Args>(args)...);
        }
    }

    template <typename First, typename... Rest>
    void _routeNumericCartHelper(First& first, Rest&... rest) {
        if constexpr (has_source_field<First>::value) {
            NumericalPath numPath = first.target;
            if (numPath.isHalt()) return;
            RouterImpl::NumericTrieNode* best = nullptr;
            _impl->routeNumeric(numPath, best, first);
            if (best && best->station) {
                _invokeStation(best->station, first);
            }
        } else {
            if constexpr (sizeof...(Rest) > 0) {
                _routeNumericCartHelper(std::forward<Rest>(rest)...);
            }
        }
    }

    template <typename... Args>
    void _invokeStation(void* station, Args&&... args) {
        if constexpr (sizeof...(Args) > 0) {
            _invokeStationHelper(station, std::forward<Args>(args)...);
        }
    }

    template <typename First, typename... Rest>
    void _invokeStationHelper(void* station, First& first, Rest&... rest) {
        if constexpr (has_station_type<First>::value) {
            using StationType = typename First::StationType;
            if (station) {
                static_cast<StationType*>(station)->operator()(first);
            }
        } else {
            if constexpr (sizeof...(Rest) > 0) {
                _invokeStationHelper(station, std::forward<Rest>(rest)...);
            }
        }
    }

    template <typename... Args>
    void* _packContext(Args&&... args) {
        if constexpr (sizeof...(Args) > 0) {
            return _packContextHelper(std::forward<Args>(args)...);
        }
        return nullptr;
    }
    
    template <typename First, typename... Rest>
    void* _packContextHelper(First& first, Rest&... rest) {
        return (void*)&first;
    }

    template <typename... Args>
    Path _resolveString(const Path& path, Args&&... args) {
        String methodVal = path.method();
        if (methodVal.isEmpty()) {
            methodVal = "GET";
        } else {
            for (usz i = 0; i < methodVal.size(); ++i) {
                if (methodVal[i] >= 'a' && methodVal[i] <= 'z') {
                    methodVal[i] = methodVal[i] - 'a' + 'A';
                }
            }
        }
        void* ctx = _packContext(std::forward<Args>(args)...);
        Path currentPath = path;

        // --- 1. BEFORE PHASE ---
        {
            Array<MatchResult> beforeMatches;
            _impl->_collectStaticMatches(&_impl->stringRoot, _getPathSegments(currentPath), 0, RoutePhase::Before, methodVal, beforeMatches);
            _impl->_collectRegexMatches(RoutePhase::Before, methodVal, currentPath, beforeMatches);
            
            _sortMatches(beforeMatches, RoutePhase::Before);
            
            for (usz i = 0; i < beforeMatches.size(); ++i) {
                if (currentPath.isHalt()) return currentPath;
                if (beforeMatches[i].type == MatchResult::Type::Static) {
                    currentPath = beforeMatches[i].staticCb(*this, currentPath, ctx);
                } else {
                    currentPath = beforeMatches[i].regexCb(beforeMatches[i].regexMatch, *this, currentPath, ctx);
                }
            }
            if (currentPath.isHalt()) return currentPath;
        }

        // --- 2. MAIN PHASE ---
        {
            Array<MatchResult> mainMatches;
            _impl->_collectStaticMatches(&_impl->stringRoot, _getPathSegments(currentPath), 0, RoutePhase::Main, methodVal, mainMatches);
            _impl->_collectRegexMatches(RoutePhase::Main, methodVal, currentPath, mainMatches);
            
            if (mainMatches.size() > 0) {
                _sortMatches(mainMatches, RoutePhase::Main);
                
                if (mainMatches[0].type == MatchResult::Type::Static) {
                    currentPath = mainMatches[0].staticCb(*this, currentPath, ctx);
                } else {
                    currentPath = mainMatches[0].regexCb(mainMatches[0].regexMatch, *this, currentPath, ctx);
                }
            }
            if (currentPath.isHalt()) return currentPath;
        }

        // --- 3. AFTER PHASE ---
        {
            Array<MatchResult> afterMatches;
            _impl->_collectStaticMatches(&_impl->stringRoot, _getPathSegments(currentPath), 0, RoutePhase::After, methodVal, afterMatches);
            _impl->_collectRegexMatches(RoutePhase::After, methodVal, currentPath, afterMatches);
            
            _sortMatches(afterMatches, RoutePhase::After);
            
            for (usz i = 0; i < afterMatches.size(); ++i) {
                if (currentPath.isHalt()) return currentPath;
                if (afterMatches[i].type == MatchResult::Type::Static) {
                    currentPath = afterMatches[i].staticCb(*this, currentPath, ctx);
                } else {
                    currentPath = afterMatches[i].regexCb(afterMatches[i].regexMatch, *this, currentPath, ctx);
                }
            }
            if (currentPath.isHalt()) return currentPath;
        }

        // --- 4. READY PHASE ---
        {
            Array<MatchResult> readyMatches;
            _impl->_collectStaticMatches(&_impl->stringRoot, _getPathSegments(currentPath), 0, RoutePhase::Ready, methodVal, readyMatches);
            _impl->_collectRegexMatches(RoutePhase::Ready, methodVal, currentPath, readyMatches);
            
            _sortMatches(readyMatches, RoutePhase::Ready);
            
            for (usz i = 0; i < readyMatches.size(); ++i) {
                if (currentPath.isHalt()) return currentPath;
                if (readyMatches[i].type == MatchResult::Type::Static) {
                    currentPath = readyMatches[i].staticCb(*this, currentPath, ctx);
                } else {
                    currentPath = readyMatches[i].regexCb(readyMatches[i].regexMatch, *this, currentPath, ctx);
                }
            }
        }

        return currentPath;
    }
};

} // namespace Resource

#endif // XI_CORE_ROUTER_HPP
