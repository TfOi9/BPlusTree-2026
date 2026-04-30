#ifndef CACHE_HASH_HPP
#define CACHE_HASH_HPP

#include <cstdint>
#include <cstddef>

namespace sjtu {
namespace anon {

constexpr int MAX_SIZE = 8192;
constexpr int PROBE_LIMIT = 32;
constexpr uint64_t EMPTY_SLOT = UINT64_MAX;
constexpr uint64_t DELETED_SLOT = UINT64_MAX - 1;

inline uint64_t hash_key(uint64_t key) noexcept {
    key ^= key >> 33;
    key *= 0xff51afd7ed558ccdull;
    key ^= key >> 33;
    key *= 0xc4ceb9fe1a85ec53ull;
    key ^= key >> 33;
    if (key == EMPTY_SLOT || key == DELETED_SLOT) {
        key ^= 0x9e3779b97f4a7c15ull;
        if (key == EMPTY_SLOT || key == DELETED_SLOT) {
            key ^= 0x7f4a7c159e3779b9ull;
        }
    }
    return key;
}

template<typename ValType>
struct cache_hash {
    struct line {
        uint64_t key;
        ValType val;
    };

    line table_[MAX_SIZE];
    size_t size_ = 0;

    static size_t index(uint64_t hash_val) noexcept {
        return hash_val & (MAX_SIZE - 1);
    }

    struct probe_seq {
        size_t cur;
        size_t step;

        probe_seq(uint64_t hash_val): cur(index(hash_val)), step((hash_val >> 32 | 1) & (MAX_SIZE - 1)) {}
        size_t next() noexcept {
            size_t res = cur;
            cur = (cur + step) & (MAX_SIZE - 1);
            return res;
        }
    };

    cache_hash() {
        clear();
    }

    ~cache_hash() = default;

    void clear() {
        for (size_t i = 0; i < MAX_SIZE; i++) {
            table_[i].key = EMPTY_SLOT;
        }
        size_ = 0;
    }

    ValType* find(uint64_t k) {
        uint64_t key = hash_key(k);
        probe_seq seq(key);
        for (int i = 0; i < PROBE_LIMIT; i++) {
            size_t idx = seq.next();
            uint64_t stored_key = table_[idx].key;
            if (stored_key == key) {
                return &table_[idx].val;
            }
            if (stored_key == EMPTY_SLOT) {
                return nullptr;
            }
        }
        return nullptr;
    }

    bool empty() const noexcept {
        return size_ == 0;
    }

    size_t size() const noexcept {
        return size_;
    }

    bool insert(uint64_t k, const ValType &v) {
        uint64_t key = hash_key(k);
        probe_seq seq(key);
        size_t first_deleted = MAX_SIZE;
        for (int i = 0; i < PROBE_LIMIT; i++) {
            size_t idx = seq.next();
            uint64_t stored_key = table_[idx].key;
            if (stored_key == key) {
                return false;
            }
            if (stored_key == EMPTY_SLOT) {
                size_t target = (first_deleted == MAX_SIZE) ? idx : first_deleted;
                table_[target].key = key;
                table_[target].val = v;
                size_++;
                return true;
            }
            if (stored_key == DELETED_SLOT && first_deleted == MAX_SIZE) {
                first_deleted = idx;
            }
        }
        if (first_deleted != MAX_SIZE) {
            table_[first_deleted].key = key;
            table_[first_deleted].val = v;
            size_++;
            return true;
        }
        return false;
    }

    bool erase(uint64_t k) {
        uint64_t key = hash_key(k);
        probe_seq seq(key);
        for (int i = 0; i < PROBE_LIMIT; i++) {
            size_t idx = seq.next();
            uint64_t stored_key = table_[idx].key;
            if (stored_key == key) {
                table_[idx].key = DELETED_SLOT;
                table_[idx].val = ValType();
                size_--;
                return true;
            }
            if (stored_key == EMPTY_SLOT) {
                return false;
            }
        }
        return false;
    }

    ValType& operator[](uint64_t k) {
        uint64_t key = hash_key(k);
        probe_seq seq(key);
        size_t first_deleted = MAX_SIZE;
        size_t last_idx = 0;
        for (int i = 0; i < PROBE_LIMIT; i++) {
            size_t idx = seq.next();
            last_idx = idx;
            uint64_t stored_key = table_[idx].key;
            if (stored_key == key) {
                return table_[idx].val;
            }
            if (stored_key == EMPTY_SLOT) {
                size_t target = (first_deleted == MAX_SIZE) ? idx : first_deleted;
                table_[target].key = key;
                table_[target].val = ValType();
                size_++;
                return table_[target].val;
            }
            if (stored_key == DELETED_SLOT && first_deleted == MAX_SIZE) {
                first_deleted = idx;
            }
        }
        if (first_deleted != MAX_SIZE) {
            table_[first_deleted].key = key;
            table_[first_deleted].val = ValType();
            size_++;
            return table_[first_deleted].val;
        }
        table_[last_idx].key = key;
        table_[last_idx].val = ValType();
        return table_[last_idx].val;
    }

    void* end() { return nullptr; }
};

} // namespace anon

} // namespace sjtu

#endif // CACHE_HASH_HPP