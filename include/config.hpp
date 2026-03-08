#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstddef>
#include <cstdint>

namespace sjtu {

typedef int64_t diskpos_t;
typedef int32_t page_id_t;

constexpr page_id_t INVALID_PAGE_ID = -1;

// Each page is a fixed-size 4KB block on disk and in memory.
constexpr size_t PAGE_SIZE = 4096;

constexpr size_t CACHE_CAPACITY = 4096;

typedef int64_t hash_t;

constexpr hash_t HASH_MOD1 = 998244353;
constexpr hash_t HASH_MOD2 = 1000000007;
constexpr hash_t HASH_BASE1 = 10007;
constexpr hash_t HASH_BASE2 = 9973;

constexpr page_id_t HEADER_PAGE_ID = 0;

} // namespace sjtu

#endif // CONFIG_HPP