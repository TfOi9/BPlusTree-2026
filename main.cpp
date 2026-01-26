#include <algorithm>
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

public:
    MemoryRiver() {}

    MemoryRiver(const string& file_name) : file_name(file_name) {}

    ~MemoryRiver() {
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
            int temp = 0;
            for (int i = 0; i < info_len; i++) {
                file.write(reinterpret_cast<char *>(&temp), sizeof(int));
            }
            return 0;
        }
        return 1;
    }

    bool initialise(string FN = "") {
        if (FN != "") file_name = FN;
        bool f = open_file();
        std::cerr << "memory river opened file " << file_name << std::endl;
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
        file.seekp(0, std::ios::end);
        int pos = file.tellp();
        std::cerr << pos << std::endl;
        file.write(reinterpret_cast<char *>(&t), sizeofT);
        return pos;
    }

    void update(T &t, const int pos) {
        file.seekp(pos);
        file.write(reinterpret_cast<char *>(&t), sizeofT);
    }

    void read(T &t, const int pos) {
        file.seekg(pos);
        file.read(reinterpret_cast<char *>(&t), sizeofT);
    }

};

struct KeyPair {
    char key_[65];
    int val_;

    KeyPair() : val_(0) {
        memset(key_, 0, sizeof(key_));
    }

    KeyPair(const char key[65], int val) : val_(val) {
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

constexpr int SLOTS = 4;
static_assert(SLOTS % 2 == 0, "Slot count must be even!");

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
        std::cerr << "opened bpt file " << file_name << " with root at " << root_ << std::endl;
    }
    
    ~BPlusTree() {
        disk_.write_info(root_, 2);
    }

    std::optional<int> find(const char key[65]) {
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

    void find_all(const char key[65], std::vector<int>& vec) {
        vec.clear();
        KeyPair kp(key, INT_MIN);
        if (root_ == 0) {
            return;
        }
        pos_ = root_;
        std::cerr << "currently at " << root_ << std::endl;
        disk_.read(cur_, root_);
        std::cerr << "current page type is " << (cur_.type_ == PageType::Leaf ? "Leaf" : (cur_.type_ == PageType::Internal ? "Internal" : "Invalid")) << std::endl;
        while (cur_.type_ != PageType::Leaf) {
            int k = cur_.lower_bound(kp);
            pos_ = cur_.ch_[k];
            disk_.read(cur_, pos_);
        }
        int k = cur_.lower_bound(kp);
        std::cerr << "now at page " << pos_ << ", found at slot " << k << std::endl;
        std::cerr << "slot contains key " << cur_.data_[k].key_ << std::endl;
        if (strcmp(cur_.data_[k].key_, key)) {
            return;
        }
        int curk = k;
        while (strcmp(cur_.data_[curk].key_, key) == 0) {
            std::cerr << "now at slot " << curk << std::endl;
            std::cerr << "size " << cur_.size_ << std::endl;
            vec.push_back(cur_.data_[curk].val_);
            if (curk < cur_.size_ - 1) {
                curk++;
                std::cerr << cur_.data_[curk].key_ << std::endl;
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
            std::cerr << "created new page as root, numbered " << root_ << std::endl;
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
        if (cur_.data_[k] < kp) {
            cur_.data_[k + 1] = kp;
            cur_.size_++;
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

    void erase(const KeyPair& kp) {
        if (root_ == 0) {
            return;
        }
        pos_ = root_;
        disk_.read(cur_, pos_);
        while (cur_.type_ != PageType::Leaf) {
            int k = cur_.lower_bound(kp);
            pos_ = cur_.ch_[k];
            disk_.read(cur_, pos_);
        }
        int k = cur_.lower_bound(kp);
        if (cur_.data_[k] != kp) {
            return;
        }
        for (int i = k; i < cur_.size_ - 1; i++) {
            cur_.data_[i] = cur_.data_[i + 1];
        }
        cur_.size_--;
        disk_.update(cur_, pos_);
        KeyPair max_pair = cur_.back();
        Page f;
        int fpos = cur_.fa_;
        while (fpos != -1) {
            disk_.read(f, fpos);
            int p = f.lower_bound(kp);
            if (f.data_[p] == kp) {
                f.data_[p] = max_pair;
                disk_.update(f, fpos);
                fpos = f.fa_;
            }
            else {
                break;
            }
        }
        if (cur_.size_ < SLOTS / 2) {
            balance();
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
                if (cur_.right_ != -1) {
                    Page rp;
                    disk_.read(rp, cur_.right_);
                    rp.left_ = newp_pos;
                    disk_.update(rp, cur_.right_);
                }
                cur_.right_ = newp_pos;
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
                if (cur_.right_ != -1) {
                    Page rp;
                    disk_.read(rp, cur_.right_);
                    rp.left_ = newp_pos;
                    disk_.update(rp, cur_.right_);
                }
                cur_.right_ = newp_pos;
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

    bool borrowl() {
        if (cur_.fa_ == -1 || cur_.size_ == 0) {
            // no father or empty
            return false;
        }
        int fpos = cur_.fa_;
        Page f;
        KeyPair max_pair = cur_.back();
        disk_.read(f, fpos);
        int k = f.lower_bound(max_pair);
        if (!k) {
            // nothing in the left
            return false;
        }
        Page bro;
        int bpos = f.ch_[k - 1];
        disk_.read(bro, bpos);
        if (bro.size_ <= SLOTS / 2) {
            return false;
        }
        for (int i = cur_.size_ - 1; i >= 0; i--) {
            cur_.data_[i + 1] = cur_.data_[i];
            cur_.ch_[i + 1] = cur_.ch_[i];
        }
        cur_.data_[0] = bro.back();
        cur_.ch_[0] = bro.ch_[bro.size_ - 1];
        cur_.size_++;
        bro.size_--;
        if(cur_.type_ == PageType::Internal) {
            Page son;
            disk_.read(son, cur_.ch_[0]);
            son.fa_ = pos_;
            disk_.update(son, cur_.ch_[0]);
        }
        f.data_[k - 1] = bro.back();
        disk_.update(cur_, pos_);
        disk_.update(bro, bpos);
        disk_.update(f, fpos);
        return true;
    }

    bool borrowr() {
        if (cur_.fa_ == -1 || cur_.size_ == 0) {
            // no father or empty
            return false;
        }
        int fpos = cur_.fa_;
        Page f;
        disk_.read(f, fpos);
        KeyPair max_pair = cur_.back();
        int k = f.lower_bound(max_pair);
        if (k == f.size_ - 1) {
            // nothing in the right
            return false;
        }
        Page bro;
        int bpos = f.ch_[k + 1];
        disk_.read(bro, bpos);
        if (bro.size_ <= SLOTS / 2) {
            return false;
        }
        cur_.data_[cur_.size_] = bro.data_[0];
        cur_.ch_[cur_.size_] = bro.ch_[0];
        cur_.size_++;
        for (int i = 0; i < bro.size_ - 1; i++) {
            bro.data_[i] = bro.data_[i + 1];
            bro.ch_[i] = bro.ch_[i + 1];
        }
        bro.size_--;
        if (cur_.type_ == PageType::Internal) {
            Page son;
            disk_.read(son, cur_.ch_[cur_.size_ - 1]);
            son.fa_ = pos_;
            disk_.update(son, cur_.ch_[cur_.size_ - 1]);
        }
        f.data_[k] = cur_.back();
        disk_.update(cur_, pos_);
        disk_.update(bro, bpos);
        disk_.update(f, fpos);
        return true;
    }

    void merge() {
        if (cur_.fa_ == -1) {
            // no father
            return;
        }
        int fpos = cur_.fa_;
        Page f;
        KeyPair max_pair = cur_.back();
        disk_.read(f, fpos);
        int k = f.lower_bound(max_pair);
        if (k) {
            // merge with the left
            Page bro;
            int bpos = f.ch_[k - 1];
            disk_.read(bro, bpos);
            if (cur_.type_ == PageType::Internal) {
                for (int i = 0; i < cur_.size_; i++) {
                    Page son;
                    disk_.read(son, cur_.ch_[i]);
                    son.fa_ = bpos;
                    disk_.update(son, cur_.ch_[i]);
                }
            }
            for (int i = 0; i < cur_.size_; i++) {
                bro.data_[bro.size_ + i] = cur_.data_[i];
                bro.ch_[bro.size_ + i] = cur_.ch_[i];
            }
            bro.size_ += cur_.size_;
            cur_.size_ = 0;
            bro.right_ = cur_.right_;
            if (cur_.right_ != -1) {
                Page rp;
                disk_.read(rp, cur_.right_);
                rp.left_ = bpos;
                disk_.update(rp, cur_.right_);
            }
            for (int i = k; i < f.size_ - 1; i++) {
                f.data_[i] = f.data_[i + 1];
                f.ch_[i] = f.ch_[i + 1];
            }
            f.size_--;
            f.data_[k - 1] = bro.back();
            disk_.update(bro, bpos);
            disk_.update(f, fpos);
            if (f.size_ < SLOTS / 2) {
                // father needs balance
                pos_ = fpos;
                cur_ = f;
                balance();
            }
        }
        else if (k != f.size_ - 1) {
            // merge with the right
            Page bro;
            int bpos = f.ch_[k + 1];
            disk_.read(bro, bpos);
            if (cur_.type_ == PageType::Internal) {
                for (int i = 0; i < bro.size_; i++) {
                    Page son;
                    disk_.read(son, bro.ch_[i]);
                    son.fa_ = pos_;
                    disk_.update(son, bro.ch_[i]);
                }
            }
            for (int i = 0; i < bro.size_; i++) {
                cur_.data_[cur_.size_ + i] = bro.data_[i];
                cur_.ch_[cur_.size_ + i] = bro.ch_[i];
            }
            cur_.size_ += bro.size_;
            bro.size_ = 0;
            cur_.right_ = bro.right_;
            if (bro.right_ != -1) {
                Page rp;
                disk_.read(rp, bro.right_);
                rp.left_ = pos_;
                disk_.update(rp, bro.right_);
            }
            for (int i = k + 1; i < f.size_ - 1; i++) {
                f.data_[i] = f.data_[i + 1];
                f.ch_[i] = f.ch_[i + 1];
            }
            f.size_--;
            f.data_[k] = cur_.back();
            disk_.update(cur_, pos_);
            disk_.update(f, fpos);
            if (f.size_ < SLOTS / 2) {
                pos_ = fpos;
                cur_ = f;
                balance();
            }
        }
    }

    void balance() {
        if (cur_.fa_ == -1) {
            // at root
            if (cur_.size_ == 0) {
                // now empty
                root_ = 0;
            }
            if (cur_.type_ == PageType::Internal && cur_.size_ == 1) {
                // root to be removed
                Page son;
                disk_.read(son, cur_.ch_[0]);
                son.fa_ = -1;
                disk_.update(son, cur_.ch_[0]);
                root_ = cur_.ch_[0];
            }
            return;
        }
        if (borrowl()) return;
        if (borrowr()) return;
        merge();
    }

};

int main() {
    std::ios::sync_with_stdio(0);
    std::cin.tie(0);
    std::cout.tie(0);
    BPlusTree bpt;
    int q;
    std::cin >> q;
    while (q--) {
        std::string op, key;
        int val;
        std::cin >> op;
        if (op == "insert") {
            std::cin >> key >> val;
            bpt.insert(KeyPair(key.c_str(), val));
        }
        else if (op == "find") {
            std::cin >> key;
            std::vector<int> vec;
            bpt.find_all(key.c_str(), vec);
            if (vec.empty()) {
                std::cout << "null\n";
            }
            else {
                for (auto v : vec) {
                    std::cout << v << ' ';
                }
                std::cout << '\n';
            }
        }
        else if (op == "delete") {
            std::cin >> key >> val;
            bpt.erase(KeyPair(key.c_str(), val));
        }
    }
    return 0;
}