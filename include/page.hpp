#ifndef PAGE_HPP
#define PAGE_HPP

#include <cstring>
#include <algorithm>

#include "config.hpp"
#include "comparator.hpp"
#include "type_helper.hpp"

namespace sjtu {

// ======================== KeyPair ========================

template<typename KeyType, typename ValueType>
struct KeyPair {
    KeyType key_;
    ValueType val_;

    KeyPair() = default;
    KeyPair(const KeyPair&) = default;
    KeyPair(const KeyType& key, const ValueType& val) : key_(key), val_(val) {}
    ~KeyPair() = default;
    KeyPair& operator=(const KeyPair&) = default;
};

template<typename KeyType, typename ValueType>
bool operator==(const KeyPair<KeyType, ValueType>& a, const KeyPair<KeyType, ValueType>& b) {
    if constexpr (has_operator_equal_v<KeyType> && has_operator_equal_v<ValueType>) {
        return a.key_ == b.key_ && a.val_ == b.val_;
    } else {
        Comparator<KeyType> kc; Comparator<ValueType> vc;
        return kc(a.key_, b.key_) == 0 && vc(a.val_, b.val_) == 0;
    }
}
template<typename KeyType, typename ValueType>
bool operator!=(const KeyPair<KeyType, ValueType>& a, const KeyPair<KeyType, ValueType>& b) { return !(a == b); }

template<typename KeyType, typename ValueType>
bool operator<(const KeyPair<KeyType, ValueType>& a, const KeyPair<KeyType, ValueType>& b) {
    if constexpr (has_operator_less_v<KeyType> && has_operator_less_v<ValueType>) {
        if (a.key_ == b.key_) return a.val_ < b.val_;
        return a.key_ < b.key_;
    } else {
        Comparator<KeyType> kc; Comparator<ValueType> vc;
        int k = kc(a.key_, b.key_);
        if (k == 0) return vc(a.val_, b.val_) < 0;
        return k < 0;
    }
}
template<typename KeyType, typename ValueType>
bool operator>(const KeyPair<KeyType, ValueType>& a, const KeyPair<KeyType, ValueType>& b) {
    if constexpr (has_operator_greater_v<KeyType> && has_operator_greater_v<ValueType>) {
        if (a.key_ == b.key_) return a.val_ > b.val_;
        return a.key_ > b.key_;
    } else {
        Comparator<KeyType> kc; Comparator<ValueType> vc;
        int k = kc(a.key_, b.key_);
        if (k == 0) return vc(a.val_, b.val_) > 0;
        return k > 0;
    }
}
template<typename K, typename V>
bool operator>=(const KeyPair<K, V>& a, const KeyPair<K, V>& b) { return !(a < b); }
template<typename K, typename V>
bool operator<=(const KeyPair<K, V>& a, const KeyPair<K, V>& b) { return !(a > b); }

template<typename KeyType, typename ValueType>
bool key_less(const KeyPair<KeyType, ValueType>& a, const KeyType& key) {
    if constexpr (has_operator_less_v<KeyType>) {
        return a.key_ < key;
    } else {
        Comparator<KeyType> comp;
        return comp(a.key_, key) < 0;
    }
}

template<typename KeyType>
bool key_equal(const KeyType& a, const KeyType& b) {
    if constexpr (has_operator_equal_v<KeyType>) {
        return a == b;
    } else {
        Comparator<KeyType> comp;
        return comp(a, b) == 0;
    }
}

// ======================== Page Types ========================

enum class PageType : int32_t {
    Invalid = 0,
    Header,
    Internal,
    Leaf
};

// ======================== HeaderPage ========================

struct HeaderPage {
    PageType type_ = PageType::Header;
    page_id_t root_page_id_ = INVALID_PAGE_ID;
    page_id_t next_page_id_ = 1;
    int32_t size_ = 0;

    void Init() {
        type_ = PageType::Header;
        root_page_id_ = INVALID_PAGE_ID;
        next_page_id_ = 1;
        size_ = 0;
    }
};

static_assert(sizeof(HeaderPage) <= PAGE_SIZE);

// ======================== InternalPage ========================
// Stores KeyPair as routing keys (max of each subtree) + child page_ids.
// data_[i] = max KeyPair of the subtree rooted at children_[i].

template<typename KeyType, typename ValueType>
struct InternalPage {
    PageType type_ = PageType::Internal;
    int32_t size_ = 0;
    page_id_t parent_ = INVALID_PAGE_ID;
    page_id_t left_sibling_ = INVALID_PAGE_ID;
    page_id_t right_sibling_ = INVALID_PAGE_ID;

    static constexpr size_t HEADER_SIZE = sizeof(PageType) + sizeof(int32_t) * 4;
    static constexpr size_t MAX_SIZE =
        (PAGE_SIZE - HEADER_SIZE) / (sizeof(KeyPair<KeyType, ValueType>) + sizeof(page_id_t)) - 1;
    static constexpr size_t HALF_SIZE = MAX_SIZE / 2;

    KeyPair<KeyType, ValueType> data_[MAX_SIZE + 1];
    page_id_t children_[MAX_SIZE + 1];

    void Init() {
        type_ = PageType::Internal;
        size_ = 0;
        parent_ = INVALID_PAGE_ID;
        left_sibling_ = INVALID_PAGE_ID;
        right_sibling_ = INVALID_PAGE_ID;
    }

    int LowerBound(const KeyPair<KeyType, ValueType>& kp) const {
        if (size_ <= 0) {
            return 0;
        }
        int lo = 0, hi = size_ - 1, ans = hi;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (data_[mid] < kp) lo = mid + 1;
            else { ans = mid; hi = mid - 1; }
        }
        return ans;
    }

    int LowerBoundByKey(const KeyType& key) const {
        if (size_ <= 0) {
            return 0;
        }
        int lo = 0, hi = size_ - 1, ans = hi;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (key_less(data_[mid], key)) lo = mid + 1;
            else { ans = mid; hi = mid - 1; }
        }
        return ans;
    }

    KeyPair<KeyType, ValueType> Back() const {
        return size_ ? data_[size_ - 1] : KeyPair<KeyType, ValueType>();
    }
};

// ======================== LeafPage ========================
// Stores KeyPair data only, plus sibling links for range scan.

template<typename KeyType, typename ValueType>
struct LeafPage {
    PageType type_ = PageType::Leaf;
    int32_t size_ = 0;
    page_id_t parent_ = INVALID_PAGE_ID;
    page_id_t left_sibling_ = INVALID_PAGE_ID;
    page_id_t right_sibling_ = INVALID_PAGE_ID;

    static constexpr size_t HEADER_SIZE = sizeof(PageType) + sizeof(int32_t) * 4;
    static constexpr size_t MAX_SIZE =
        (PAGE_SIZE - HEADER_SIZE) / sizeof(KeyPair<KeyType, ValueType>) - 1;
    static constexpr size_t HALF_SIZE = MAX_SIZE / 2;

    KeyPair<KeyType, ValueType> data_[MAX_SIZE + 1];

    void Init() {
        type_ = PageType::Leaf;
        size_ = 0;
        parent_ = INVALID_PAGE_ID;
        left_sibling_ = INVALID_PAGE_ID;
        right_sibling_ = INVALID_PAGE_ID;
    }

    int LowerBound(const KeyPair<KeyType, ValueType>& kp) const {
        if (size_ <= 0) {
            return 0;
        }
        int lo = 0, hi = size_ - 1, ans = hi;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (data_[mid] < kp) lo = mid + 1;
            else { ans = mid; hi = mid - 1; }
        }
        return ans;
    }

    int LowerBoundByKey(const KeyType& key) const {
        if (size_ <= 0) {
            return 0;
        }
        int lo = 0, hi = size_ - 1, ans = hi;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if (key_less(data_[mid], key)) lo = mid + 1;
            else { ans = mid; hi = mid - 1; }
        }
        return ans;
    }

    KeyPair<KeyType, ValueType> Front() const {
        return size_ ? data_[0] : KeyPair<KeyType, ValueType>();
    }
    KeyPair<KeyType, ValueType> Back() const {
        return size_ ? data_[size_ - 1] : KeyPair<KeyType, ValueType>();
    }
};

} // namespace sjtu

#endif // PAGE_HPP