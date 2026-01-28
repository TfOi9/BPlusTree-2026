#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <cstddef>
#include <cstdint>

namespace sjtu {

typedef int64_t diskpos_t;

constexpr size_t PAGE_SLOT_COUNT = 200;
static_assert(PAGE_SLOT_COUNT % 2 == 0, "Slot count must be even!");

constexpr size_t CACHE_CAPACITY = 500;

} // namespace sjtu

#endif // CONFIG_HPP