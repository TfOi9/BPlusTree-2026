#ifndef PAGE_GUARD_HPP
#define PAGE_GUARD_HPP

#include <shared_mutex>
#include <utility>

#include "config.hpp"
#include "buffer.hpp"

namespace sjtu {

// Basic guard holding a pinned CacheEntry. Not directly used; use Read/WritePageGuard.
class BasicPageGuard {
    friend class ReadPageGuard;
    friend class WritePageGuard;

protected:
    BufferManager* bpm_ = nullptr;
    CacheEntry* entry_ = nullptr;
    bool dirty_ = false;

public:
    BasicPageGuard() = default;

    BasicPageGuard(BufferManager* bpm, CacheEntry* entry)
        : bpm_(bpm), entry_(entry) {}

    BasicPageGuard(BasicPageGuard&& other) noexcept
        : bpm_(other.bpm_), entry_(other.entry_), dirty_(other.dirty_) {
        other.bpm_ = nullptr;
        other.entry_ = nullptr;
    }

    BasicPageGuard& operator=(BasicPageGuard&& other) noexcept {
        if (this != &other) {
            Drop();
            bpm_ = other.bpm_;
            entry_ = other.entry_;
            dirty_ = other.dirty_;
            other.bpm_ = nullptr;
            other.entry_ = nullptr;
        }
        return *this;
    }

    BasicPageGuard(const BasicPageGuard&) = delete;
    BasicPageGuard& operator=(const BasicPageGuard&) = delete;

    ~BasicPageGuard() { Drop(); }

    void Drop() {
        if (bpm_ && entry_) {
            bpm_->Unpin(entry_->page_id_, dirty_);
            bpm_ = nullptr;
            entry_ = nullptr;
        }
    }

    page_id_t PageId() const { return entry_ ? entry_->page_id_ : INVALID_PAGE_ID; }
    bool IsValid() const { return entry_ != nullptr; }
};

// ReadPageGuard: holds a shared (read) lock on the page latch.
class ReadPageGuard {
private:
    BufferManager* bpm_ = nullptr;
    CacheEntry* entry_ = nullptr;

public:
    ReadPageGuard() = default;

    ReadPageGuard(BufferManager* bpm, CacheEntry* entry)
        : bpm_(bpm), entry_(entry) {}

    ReadPageGuard(ReadPageGuard&& other) noexcept
        : bpm_(other.bpm_), entry_(other.entry_) {
        other.bpm_ = nullptr;
        other.entry_ = nullptr;
    }

    ReadPageGuard& operator=(ReadPageGuard&& other) noexcept {
        if (this != &other) {
            Drop();
            bpm_ = other.bpm_;
            entry_ = other.entry_;
            other.bpm_ = nullptr;
            other.entry_ = nullptr;
        }
        return *this;
    }

    ReadPageGuard(const ReadPageGuard&) = delete;
    ReadPageGuard& operator=(const ReadPageGuard&) = delete;

    ~ReadPageGuard() { Drop(); }

    template<typename T>
    const T* As() const {
        return reinterpret_cast<const T*>(entry_->data_);
    }

    void Drop() {
        if (bpm_ && entry_) {
            bpm_->Unpin(entry_->page_id_, false);
            bpm_ = nullptr;
            entry_ = nullptr;
        }
    }

    page_id_t PageId() const { return entry_ ? entry_->page_id_ : INVALID_PAGE_ID; }
    bool IsValid() const { return entry_ != nullptr; }
};

// WritePageGuard: holds an exclusive (write) lock on the page latch.
class WritePageGuard {
private:
    BufferManager* bpm_ = nullptr;
    CacheEntry* entry_ = nullptr;

public:
    WritePageGuard() = default;

    WritePageGuard(BufferManager* bpm, CacheEntry* entry)
        : bpm_(bpm), entry_(entry) {}

    WritePageGuard(WritePageGuard&& other) noexcept
        : bpm_(other.bpm_), entry_(other.entry_) {
        other.bpm_ = nullptr;
        other.entry_ = nullptr;
    }

    WritePageGuard& operator=(WritePageGuard&& other) noexcept {
        if (this != &other) {
            Drop();
            bpm_ = other.bpm_;
            entry_ = other.entry_;
            other.bpm_ = nullptr;
            other.entry_ = nullptr;
        }
        return *this;
    }

    WritePageGuard(const WritePageGuard&) = delete;
    WritePageGuard& operator=(const WritePageGuard&) = delete;

    ~WritePageGuard() { Drop(); }

    template<typename T>
    const T* As() const {
        return reinterpret_cast<const T*>(entry_->data_);
    }

    template<typename T>
    T* AsMut() {
        return reinterpret_cast<T*>(entry_->data_);
    }

    void Drop() {
        if (bpm_ && entry_) {
            bpm_->Unpin(entry_->page_id_, true);  // write guards always mark dirty
            bpm_ = nullptr;
            entry_ = nullptr;
        }
    }

    page_id_t PageId() const { return entry_ ? entry_->page_id_ : INVALID_PAGE_ID; }
    bool IsValid() const { return entry_ != nullptr; }
};

} // namespace sjtu

#endif // PAGE_GUARD_HPP
