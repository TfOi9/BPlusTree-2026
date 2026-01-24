#include <climits>
#include <iostream>
#include <fstream>
#include <cstring>
#include <optional>
#include <vector>

using std::string;
using std::fstream;
using std::ifstream;
using std::ofstream;

template<class T, int info_len = 4>
class MemoryRiver {
private:
    fstream file;
    string file_name;
    int sizeofT = sizeof(T);
    int info_offset = info_len * sizeof(int);

    int size_;
public:
    MemoryRiver() : size_(1) {}

    MemoryRiver(const string& file_name) : file_name(file_name) {}

    ~MemoryRiver() {
        write_info(size_, 1);
        if (file.is_open()) {
            file.close();
        }
    }

    bool open_file() {
        file.open(file_name, std::ios::in | std::ios::out | std::ios::binary);
        if (!file) {
            file.open(file_name, std::ios::out | std::ios::binary);
            file.close();
            file.open(file_name, std::ios::in | std::ios::out | std::ios::binary);
            return 0;
        }
        return 1;
    }

    bool initialise(string FN = "") {
        if (FN != "") file_name = FN;
        bool f = open_file();
        if (f) {
            get_info(size_, 1);
        }
        else {
            size_ = 0;
        }
        // std::cerr << file_name << " " << size_ << std::endl;
        return f;
    }

    void get_info(int &tmp, int n) {
        if (n > info_len) return;
        if (!file.is_open()) {
            open_file();
        }
        if (!file) {
            return;
        }
        file.seekg((n - 1) * sizeof(int));
        file.read(reinterpret_cast<char *>(&tmp), sizeof(int));
    }

    void write_info(int tmp, int n) {
        if (n > info_len) return;
        if (!file.is_open()) {
            open_file();
        }
        if (!file) {
            return;
        }
        file.seekp((n - 1) * sizeof(int));
        file.write(reinterpret_cast<char *>(&tmp), sizeof(int));
    }

    int write(T &t) {
        int siz = size_;
        size_++;
        file.seekp(info_offset + siz * sizeofT);
        file.write(reinterpret_cast<char *>(&t), sizeofT);
        return siz;
    }

    void update(T &t, const int pos) {
        file.seekp(info_offset + pos * sizeofT);
        file.write(reinterpret_cast<char *>(&t), sizeofT);
    }

    void read(T &t, const int pos) {
        file.seekg(info_offset + pos * sizeofT);
        file.read(reinterpret_cast<char *>(&t), sizeofT);
    }

    int size() const {
        return size_;
    }
};

struct KeyPair {
    char key_[65];
    int val_;

    KeyPair() : val_(0) {
        memset(key_, 0, sizeof(key_));
    }

    KeyPair(char key[65], int val) : val_(val) {
        memcpy(key_, key, sizeof(char) * 65);
    }

    KeyPair(const KeyPair& oth) : val_(oth.val_) {
        memcpy(key_, oth.key_, sizeof(char) * 65);
    }

    KeyPair& operator=(const KeyPair& oth) {
        if (this == &oth) {
            return *this;
        }
        memcpy(key_, oth.key_, sizeof(char) * 65);
        val_ = oth.val_;
        return *this;
    }
};

bool operator==(const KeyPair& a, const KeyPair& b) {
    return strcmp(a.key_, b.key_) == 0 && a.val_ == b.val_;
}

bool operator<(const KeyPair& a, const KeyPair& b) {
    int cp = strcmp(a.key_, b.key_);
    if (!cp) {
        return a.val_ < b.val_;
    }
    return cp < 0;
}

bool operator>(const KeyPair& a, const KeyPair& b) {
    int cp = strcmp(a.key_, b.key_);
    if (!cp) {
        return a.val_ > b.val_;
    }
    return cp > 0;
}

bool operator<=(const KeyPair& a, const KeyPair& b) {
    return a < b || a == b;
}

bool operator>=(const KeyPair& a, const KeyPair& b) {
    return a > b || a == b;
}

bool operator!=(const KeyPair& a, const KeyPair& b) {
    return !(a == b);
}

enum class PageType {
    Invalid = 0,
    Leaf,
    Internal
};

constexpr int SLOTS = 200;

struct Page {
    KeyPair data_[SLOTS + 2];
    PageType type_ = PageType::Invalid;
    int fa_ = -1;
    int ch_[SLOTS + 2];
    int left_ = -1;
    int right_ = -1;
    int size_ = 0;

    Page() {
        memset(ch_, 0, sizeof(ch_));
    }

    int lower_bound(const KeyPair& key) {
        int l = 0, r = size_ - 1, mid = -1, ans = r;
        while (l <= r) {
            mid = (l + r) / 2;
            if (data_[mid] < key) {
                l = mid + 1;
            }
            else {
                ans = mid;
                r = mid - 1;
            }
        }
        return ans;
    }

    KeyPair back() const {
        if (!size_) return KeyPair();
        else return data_[size_ - 1];
    }
};

class BPlusTree {
public:
    BPlusTree(const std::string file_name = "bpt.dat") {
        disk_.initialise(file_name);
        disk_.get_info(root_, 2);
    }
    
    ~BPlusTree() {
        disk_.write_info(root_, 2);
    }

    std::optional<int> find(char key[65]) {
        KeyPair kp(key, INT_MIN);
        if (root_ == 0) {
            return std::nullopt;
        }
        pos_ = root_;
        disk_.read(cur_, root_);
        while (cur_.type_ != PageType::Leaf) {
            int k = cur_.lower_bound(kp);
            pos_ = cur_.ch_[k];
            disk_.read(cur_, pos_);
        }
        int k = cur_.lower_bound(kp);
        if (strcmp(cur_.data_[k].key_, key)) {
            return std::nullopt;
        }
        return cur_.data_[k].val_;
    }

    void find_all(char key[65], std::vector<int>& vec) {
        vec.clear();
        KeyPair kp(key, INT_MIN);
        if (root_ == 0) {
            return;
        }
        pos_ = root_;
        disk_.read(cur_, root_);
        while (cur_.type_ != PageType::Leaf) {
            int k = cur_.lower_bound(kp);
            pos_ = cur_.ch_[k];
            disk_.read(cur_, pos_);
        }
        int k = cur_.lower_bound(kp);
        if (strcmp(cur_.data_[k].key_, key)) {
            return;
        }
        int curk = k;
        while (strcmp(cur_.data_[curk].key_, key) == 0) {
            vec.push_back(cur_.data_[curk].val_);
            if (curk < cur_.size_ - 1) {
                curk++;
            }
            else {
                if (cur_.right_ == -1) {
                    break;
                }
                else {
                    pos_ = cur_.right_;
                    disk_.read(cur_, pos_);
                    curk = 0;
                }
            }
        }
    }

    void insert(const KeyPair& kp) {
        if (root_ == 0) {
            Page newr;
            newr.size_ = 1;
            newr.type_ = PageType::Leaf;
            newr.data_[0] = kp;
            root_ = disk_.write(newr);
            return;
        }
        pos_ = root_;
        disk_.read(cur_, root_);
        while (cur_.type_ != PageType::Leaf) {
            int k = cur_.lower_bound(kp);
            if (cur_.data_[k] < kp) {
                cur_.data_[k] = kp;
                disk_.update(cur_, pos_);
            }
            pos_ = cur_.ch_[k];
            disk_.read(cur_, pos_);
        }
        int k = cur_.lower_bound(kp);
        if (cur_.data_[k] == kp) {
            return;
        }
        if (k == cur_.size_ - 1) {
            cur_.size_++;
            cur_.data_[cur_.size_ - 1] = kp;
        }
        else {
            for (int i = cur_.size_ - 1; i >= k; i--) {
                cur_.data_[i + 1] = cur_.data_[i];
            }
            cur_.data_[k] = kp;
            cur_.size_++;
        }
        disk_.update(cur_, pos_);
        if (cur_.size_ == SLOTS) {
            split();
        }
    }

private:
    MemoryRiver<Page> disk_;
    Page cur_;
    int pos_;
    int root_ = 0;

    void split() {
        Page newp;
        newp.size_ = SLOTS / 2;
        cur_.size_ = SLOTS / 2;
        if (cur_.type_ == PageType::Leaf) {
            newp.type_ = PageType::Leaf;
        }
        else {
            newp.type_ = PageType::Internal;
        }
        newp.fa_ = cur_.fa_;
        newp.left_ = pos_;
        newp.right_ = cur_.right_;
        if (cur_.type_ == PageType::Leaf) {
            // spliting leaf page
            for (int i = 0; i < newp.size_; i++) {
                newp.data_[i] = cur_.data_[i + newp.size_];
            }
            KeyPair split_at = cur_.back(), max_pair = newp.back();
            if (cur_.fa_ != -1) {
                // exists father
                Page f;
                disk_.read(f, cur_.fa_);
                int fa_pos = f.lower_bound(max_pair);
                for (int i = f.size_ - 1; i >= fa_pos; i--) {
                    f.data_[i + 1] = f.data_[i];
                    f.ch_[i + 1] = f.ch_[i];
                }
                int newp_pos = disk_.write(newp);
                f.data_[fa_pos] = split_at;
                f.data_[fa_pos + 1] = max_pair;
                f.ch_[fa_pos] = pos_;
                f.ch_[fa_pos + 1] = newp_pos;
                f.size_++;
                cur_.right_ = newp_pos;
                if (newp.right_ != -1) {
                    Page rp;
                    disk_.read(rp, newp.right_);
                    rp.left_ = newp_pos;
                    disk_.update(rp, newp.right_);
                }
                disk_.update(f, cur_.fa_);
                disk_.update(cur_, pos_);
                if (f.size_ == SLOTS) {
                    pos_ = cur_.fa_;
                    cur_ = f;
                    split();
                }
            }
            else {
                // at root
                Page newr;
                newr.type_ = PageType::Internal;
                newr.size_ = 2;
                root_ = disk_.write(newr);
                cur_.fa_ = root_;
                newp.fa_ = root_;
                newr.data_[0] = cur_.back();
                newr.data_[1] = newp.back();
                newr.ch_[0] = pos_;
                int newp_pos = disk_.write(newp);
                newr.ch_[1] = newp_pos;
                cur_.right_ = newp_pos;
                newp.left_ = pos_;
                disk_.update(newr, root_);
                disk_.update(cur_, pos_);
            }
        }
        else {
            // spliting internal page
            int newp_pos = disk_.write(newp);
            for (int i = 0; i < newp.size_; i++) {
                newp.data_[i] = cur_.data_[i + newp.size_];
                newp.ch_[i] = cur_.ch_[i + newp.size_];
            }
            Page ch;
            for (int i = 0; i < newp.size_; i++) {
                disk_.read(ch, newp.ch_[i]);
                ch.fa_ = newp_pos;
                disk_.update(ch, newp.ch_[i]);
            }
            KeyPair split_at = cur_.back(), max_pair = newp.back();
            if (cur_.fa_ != -1) {
                // exists father
                Page f;
                disk_.read(f, cur_.fa_);
                int fa_pos = f.lower_bound(max_pair);
                for (int i = f.size_ - 1; i >= fa_pos; i--) {
                    f.data_[i + 1] = f.data_[i];
                    f.ch_[i + 1] = f.ch_[i];
                }
                f.data_[fa_pos] = split_at;
                f.data_[fa_pos + 1] = max_pair;
                f.ch_[fa_pos] = pos_;
                f.ch_[fa_pos + 1] = newp_pos;
                f.size_++;
                cur_.right_ = newp_pos;
                if (newp.right_ != -1) {
                    Page rp;
                    disk_.read(rp, newp.right_);
                    rp.left_ = newp_pos;
                    disk_.update(rp, newp.right_);
                }
                disk_.update(f, cur_.fa_);
                disk_.update(cur_, pos_);
                disk_.update(newp, newp_pos);
                if (f.size_ == SLOTS) {
                    pos_ = cur_.fa_;
                    cur_ = f;
                    split();
                }
            }
            else {
                // at root
                Page newr;
                newr.type_ = PageType::Internal;
                newr.size_ = 2;
                root_ = disk_.write(newr);
                cur_.fa_ = root_;
                newp.fa_ = root_;
                newr.data_[0] = cur_.back();
                newr.data_[1] = newp.back();
                newr.ch_[0] = pos_;
                newr.ch_[1] = newp_pos;
                disk_.update(newr, root_);
                disk_.update(cur_, pos_);
                disk_.update(newp, newp_pos);
            }
        }
    }

    void merge();
};