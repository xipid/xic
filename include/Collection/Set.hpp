#ifndef XI_COLLECTION_SET_HPP
#define XI_COLLECTION_SET_HPP

#include "Array.hpp"
#include "Interval.hpp"

namespace Collection {

template <typename T>
class Set {
public:
    Array<T> elements;
    bool isInversed = false;

    Set() : isInversed(false) {}

    bool includes(const T& val) const {
        bool found = false;
        for (usz i = 0; i < elements.size(); ++i) {
            if (elements[i] == val) {
                found = true;
                break;
            }
        }
        return isInversed ? !found : found;
    }

    bool operator()(const T& val) const {
        return includes(val);
    }

    void add(const T& val) {
        if (!includes(val)) {
            elements.push(val);
        }
    }

    void del(const T& val) {
        for (usz i = 0; i < elements.size(); ++i) {
            if (elements[i] == val) {
                elements.remove(i);
                break;
            }
        }
    }

    Set<T> bar() const {
        Set<T> res = *this;
        res.isInversed = !res.isInversed;
        return res;
    }

    Set<T> operator~() const {
        return bar();
    }

    Set<T> operator&(const Set<T>& o) const {
        Set<T> res;
        for (usz i = 0; i < elements.size(); ++i) {
            if (o.includes(elements[i])) {
                res.add(elements[i]);
            }
        }
        return res;
    }

    Set<T> operator|(const Set<T>& o) const {
        Set<T> res = *this;
        for (usz i = 0; i < o.elements.size(); ++i) {
            res.add(o.elements[i]);
        }
        return res;
    }

    Set<T> operator-(const Set<T>& o) const {
        Set<T> res;
        for (usz i = 0; i < elements.size(); ++i) {
            if (!o.includes(elements[i])) {
                res.add(elements[i]);
            }
        }
        return res;
    }

    // Iterator support
    typename Array<T>::Iterator begin() const {
        return elements.begin();
    }

    typename Array<T>::Iterator end() const {
        return elements.end();
    }
};

} // namespace Collection

#endif // XI_COLLECTION_SET_HPP
