#ifndef XI_COLLECTION_INTERVAL_HPP
#define XI_COLLECTION_INTERVAL_HPP

#include "../Xi/Primitives.hpp"
#include <cmath>
#include <limits>

namespace Collection {

template <typename T = f32>
class Interval {
public:
    T left = 0;
    T right = 0;
    T step = 0;
    u8 flags = 0; // bit 0: lopen, bit 1: ropen, bit 2: linfinity, bit 3: rinfinity, bit 4: isInversed

    Interval() : left(0), right(0), step(0), flags(0) {}

    Interval(T l, T r, u8 f, T st = 0) : left(l), right(r), step(st), flags(f) {}

    bool lopen() const { return (flags & 1) != 0; }
    bool ropen() const { return (flags & 2) != 0; }
    bool linfinity() const { return (flags & 4) != 0; }
    bool rinfinity() const { return (flags & 8) != 0; }
    bool isInversed() const { return (flags & 16) != 0; }

    T anchor() const {
        if (linfinity() && rinfinity() && step != 0) {
            return right;
        }
        return 0;
    }

    bool includes(T val) const {
        bool inBase = true;
        if (!linfinity()) {
            if (lopen()) {
                if (val <= left) inBase = false;
            } else {
                if (val < left) inBase = false;
            }
        }
        if (!rinfinity()) {
            if (ropen()) {
                if (val >= right) inBase = false;
            } else {
                if (val > right) inBase = false;
            }
        }

        if (inBase && step != 0) {
            T start = 0;
            if (!linfinity()) {
                start = left;
            } else if (!rinfinity()) {
                start = right;
            } else {
                start = anchor();
            }
            T diff = val - start;
            T ratio = diff / step;
            T rounded = std::round(ratio);
            if (std::abs(ratio - rounded) > 1e-5) {
                inBase = false;
            }
        }

        return isInversed() ? !inBase : inBase;
    }

    bool operator()(T val) const {
        return includes(val);
    }

    Interval<T> bar() const {
        Interval<T> res = *this;
        res.flags ^= 16; // Toggle isInversed
        return res;
    }

    Interval<T> operator~() const {
        return bar();
    }

    T next() const {
        if (!linfinity()) {
            if (lopen() && step == 0) return left + 1e-5;
            return left;
        }
        if (step != 0) {
            return anchor();
        }
        return std::numeric_limits<T>::lowest();
    }

    T next(T current) const {
        if (step == 0) return current;
        return current + step;
    }

    // Set operations returning Intervals
    Interval<T> inter(const Interval<T>& o) const {
        T newLeft = (!linfinity() && !o.linfinity()) ? std::max(left, o.left) : (linfinity() ? o.left : left);
        T newRight = (!rinfinity() && !o.rinfinity()) ? std::min(right, o.right) : (rinfinity() ? o.right : right);
        u8 newFlags = 0;
        if (linfinity() && o.linfinity()) newFlags |= 4;
        if (rinfinity() && o.rinfinity()) newFlags |= 8;
        return Interval<T>(newLeft, newRight, newFlags, std::max(step, o.step));
    }

    Interval<T> operator&(const Interval<T>& o) const { return inter(o); }
    Interval<T> operator|(const Interval<T>& o) const {
        T newLeft = (!linfinity() && !o.linfinity()) ? std::min(left, o.left) : (linfinity() || o.linfinity() ? 0 : left);
        T newRight = (!rinfinity() && !o.rinfinity()) ? std::max(right, o.right) : (rinfinity() || o.rinfinity() ? 0 : right);
        u8 newFlags = 0;
        if (linfinity() || o.linfinity()) newFlags |= 4;
        if (rinfinity() || o.rinfinity()) newFlags |= 8;
        return Interval<T>(newLeft, newRight, newFlags, std::max(step, o.step));
    }

    // Element-wise arithmetic operators
    Interval<T> operator+(T val) const {
        Interval<T> res = *this;
        if (!res.linfinity()) res.left += val;
        if (!res.rinfinity()) res.right += val;
        else if (res.step != 0) res.right += val; // Update anchor stored in right
        return res;
    }

    Interval<T> operator-(T val) const {
        return *this + (-val);
    }

    Interval<T> operator*(T val) const {
        Interval<T> res = *this;
        if (!res.linfinity()) res.left *= val;
        if (!res.rinfinity()) res.right *= val;
        res.step *= val;
        return res;
    }

    Interval<T> operator/(T val) const {
        return *this * (1.0f / val);
    }

    // Iterator support for range-based loops
    class Iterator {
    public:
        const Interval<T>* parent;
        T current;
        usz count;

        Iterator(const Interval<T>* p, T start) : parent(p), current(start), count(0) {}

        T operator*() const { return current; }

        Iterator& operator++() {
            current = parent->next(current);
            count++;
            return *this;
        }

        bool operator!=(const Iterator& o) const {
            if (parent->rinfinity() && parent->step == 0) {
                return count < 100;
            }
            if (parent->rinfinity() && parent->step != 0) {
                return count < 100;
            }
            if (!parent->rinfinity() && current >= parent->right) {
                return false;
            }
            return count < 10000;
        }
    };

    Iterator begin() const {
        return Iterator(this, next());
    }

    Iterator end() const {
        return Iterator(this, rinfinity() ? 0 : right);
    }
};

// Constructor Helpers
template <typename T = f32>
Interval<T> i00(T left, T right, T step = 0, T fix = 0) {
    return Interval<T>(left, right, 1 | 2, step);
}

template <typename T = f32>
Interval<T> i10(T left, T right, T step = 0, T fix = 0) {
    return Interval<T>(left, right, 2, step);
}

template <typename T = f32>
Interval<T> i11(T left, T right, T step = 0, T fix = 0) {
    return Interval<T>(left, right, 0, step);
}

template <typename T = f32>
Interval<T> i01(T left, T right, T step = 0, T fix = 0) {
    return Interval<T>(left, right, 1, step);
}

template <typename T = f32>
Interval<T> iii(T step = 0, T fix = 0) {
    return Interval<T>(0, fix, 1 | 2 | 4 | 8, step);
}

template <typename T = f32>
Interval<T> i1i(T left, T step = 0, T fix = 0) {
    return Interval<T>(left, fix, 2 | 8, step);
}

template <typename T = f32>
Interval<T> i0i(T left, T step = 0, T fix = 0) {
    return Interval<T>(left, fix, 1 | 2 | 8, step);
}

template <typename T = f32>
Interval<T> ii1(T right, T step = 0, T fix = 0) {
    return Interval<T>(fix, right, 1 | 4, step);
}

template <typename T = f32>
Interval<T> ii0(T right, T step = 0, T fix = 0) {
    return Interval<T>(fix, right, 1 | 2 | 4, step);
}

// Global math constants
static const Interval<f32> R = iii<f32>(0, 0);
static const Interval<f32> Z = iii<f32>(1.0f, 0.0f);

} // namespace Collection

#endif // XI_COLLECTION_INTERVAL_HPP
