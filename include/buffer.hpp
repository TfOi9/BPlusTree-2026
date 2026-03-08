#ifndef BUFFER_HPP
#define BUFFER_HPP

#include <list>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <cassert>
#include <cstring>

#include "config.hpp"
#include "disk.hpp"

namespace sjtu {

// A single frame in the buffer pool.
struct CacheEntry {
    page_id_t page_id_ = INVALID_PAGE_ID;
    char data_[PAGE_SIZE];
    bool dirty_ = false;
    int pin_count_ = 0;
    std::shared_mutex latch_;  // per-page rwlock
    std::list<page_id_t>::iterator lru_it_;

    CacheEntry() { std::memset(data_, 0, PAGE_SIZE); }
};

// Non-template BufferManager that manages raw PAGE_SIZE frames.
class BufferManager {
private:
    DiskManager disk_;
    size_t capacity_;
    std::mutex global_latch_;  // protects cache_ and lru_list_

    std::unordered_map<page_id_t, CacheEntry*> page_table_;
    std::list<page_id_t> lru_list_;
    // We pre-allocate frames
    CacheEntry* frames_;
    std::list<size_t> free_list_;

    size_t AllocateFrame() {
        if (!free_list_.empty()) {
            size_t idx = free_list_.front();
            free_list_.pop_front();
            return idx;
        }
        // Need to evict
        for (auto rit = lru_list_.rbegin(); rit != lru_list_.rend(); ++rit) {
            page_id_t cand = *rit;
            auto it = page_table_.find(cand);
            if (it != page_table_.end() && it->second->pin_count_ == 0) {
                CacheEntry* entry = it->second;
                size_t frame_idx = entry - frames_;
                if (entry->dirty_) {
                    disk_.WritePage(entry->page_id_, entry->data_);
                    entry->dirty_ = false;
                }
                lru_list_.erase(std::next(rit).base());
                page_table_.erase(it);
                entry->page_id_ = INVALID_PAGE_ID;
                entry->pin_count_ = 0;
                entry->dirty_ = false;
                std::memset(entry->data_, 0, PAGE_SIZE);
                return frame_idx;
            }
        }
        // All frames pinned — should not happen in practice
        assert(false && "All buffer frames are pinned, cannot evict");
        return 0;
    }

    void Promote(CacheEntry* entry) {
        lru_list_.erase(entry->lru_it_);
        lru_list_.push_front(entry->page_id_);
        entry->lru_it_ = lru_list_.begin();
    }

public:
    BufferManager(size_t capacity, const std::string& file_name)
        : capacity_(capacity) {
        frames_ = new CacheEntry[capacity_];
        for (size_t i = 0; i < capacity_; i++) {
            free_list_.push_back(i);
        }
        disk_.Initialize(file_name);
    }

    ~BufferManager() {
        FlushAll();
        delete[] frames_;
    }

    BufferManager(const BufferManager&) = delete;
    BufferManager& operator=(const BufferManager&) = delete;

    // Fetch a page into the buffer pool and pin it. Returns the CacheEntry*.
    // Caller must later call Unpin().
    CacheEntry* FetchPage(page_id_t page_id) {
        std::lock_guard<std::mutex> lock(global_latch_);
        auto it = page_table_.find(page_id);
        if (it != page_table_.end()) {
            CacheEntry* entry = it->second;
            entry->pin_count_++;
            Promote(entry);
            return entry;
        }
        size_t frame_idx = AllocateFrame();
        CacheEntry* entry = &frames_[frame_idx];
        entry->page_id_ = page_id;
        entry->pin_count_ = 1;
        entry->dirty_ = false;
        disk_.ReadPage(page_id, entry->data_);
        lru_list_.push_front(page_id);
        entry->lru_it_ = lru_list_.begin();
        page_table_[page_id] = entry;
        return entry;
    }

    // Create a new page on disk and bring it into the buffer.
    CacheEntry* NewPage(page_id_t page_id) {
        std::lock_guard<std::mutex> lock(global_latch_);
        disk_.AllocatePage(page_id);
        size_t frame_idx = AllocateFrame();
        CacheEntry* entry = &frames_[frame_idx];
        entry->page_id_ = page_id;
        entry->pin_count_ = 1;
        entry->dirty_ = true;
        std::memset(entry->data_, 0, PAGE_SIZE);
        lru_list_.push_front(page_id);
        entry->lru_it_ = lru_list_.begin();
        page_table_[page_id] = entry;
        return entry;
    }

    void Unpin(page_id_t page_id, bool dirty) {
        std::lock_guard<std::mutex> lock(global_latch_);
        auto it = page_table_.find(page_id);
        if (it == page_table_.end()) return;
        CacheEntry* entry = it->second;
        if (dirty) entry->dirty_ = true;
        if (entry->pin_count_ > 0) entry->pin_count_--;
    }

    void FlushAll() {
        std::lock_guard<std::mutex> lock(global_latch_);
        for (auto& [pid, entry] : page_table_) {
            if (entry->dirty_) {
                disk_.WritePage(entry->page_id_, entry->data_);
                entry->dirty_ = false;
            }
        }
    }

    // Fetch and return a pinned CacheEntry (for guard construction).
    CacheEntry* FetchPageForGuard(page_id_t page_id) {
        return FetchPage(page_id);
    }

    // Allocate new page and return a pinned CacheEntry (for guard construction).
    CacheEntry* NewPageForGuard(page_id_t page_id) {
        return NewPage(page_id);
    }
};

} // namespace sjtu

#endif // BUFFER_HPP