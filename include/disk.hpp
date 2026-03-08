#ifndef DISK_HPP
#define DISK_HPP

#include <string>
#include <fstream>
#include <mutex>
#include <cstring>

#include "config.hpp"

namespace sjtu {

// Non-template DiskManager that reads/writes fixed-size PAGE_SIZE blocks.
// Page 0 is at file offset 0, page 1 at PAGE_SIZE, etc.
class DiskManager {
private:
    std::fstream file_;
    std::string file_name_;
    std::mutex io_mutex_;

    bool OpenFile() {
        file_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
        if (!file_) {
            // Create new file
            file_.open(file_name_, std::ios::out | std::ios::binary);
            file_.close();
            file_.open(file_name_, std::ios::in | std::ios::out | std::ios::binary);
            // Write an empty header page (page 0)
            char buf[PAGE_SIZE];
            std::memset(buf, 0, PAGE_SIZE);
            file_.write(buf, PAGE_SIZE);
            file_.flush();
            return false;
        }
        return true;
    }

public:
    DiskManager() = default;

    ~DiskManager() {
        if (file_.is_open()) file_.close();
    }

    bool Initialize(const std::string& file_name) {
        file_name_ = file_name;
        return OpenFile();
    }

    void ReadPage(page_id_t page_id, char* data) {
        std::lock_guard<std::mutex> lock(io_mutex_);
        file_.seekg(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
        file_.read(data, PAGE_SIZE);
    }

    void WritePage(page_id_t page_id, const char* data) {
        std::lock_guard<std::mutex> lock(io_mutex_);
        file_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
        file_.write(data, PAGE_SIZE);
        file_.flush();
    }

    // Allocate a new page by extending the file. Returns the new page_id.
    page_id_t AllocatePage(page_id_t page_id) {
        std::lock_guard<std::mutex> lock(io_mutex_);
        char buf[PAGE_SIZE];
        std::memset(buf, 0, PAGE_SIZE);
        file_.seekp(static_cast<std::streamoff>(page_id) * PAGE_SIZE);
        file_.write(buf, PAGE_SIZE);
        file_.flush();
        return page_id;
    }
};

} // namespace sjtu

#endif // DISK_HPP