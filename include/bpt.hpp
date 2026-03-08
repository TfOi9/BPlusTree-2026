#ifndef BPT_HPP
#define BPT_HPP

#include <optional>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

#include "buffer.hpp"
#include "config.hpp"
#include "page.hpp"
#include "page_guard.hpp"

namespace sjtu {

template<typename KeyType, typename ValueType>
class BPlusTree {
private:
    using KeyPairType = KeyPair<KeyType, ValueType>;
    using LeafPageType = LeafPage<KeyType, ValueType>;
    using InternalPageType = InternalPage<KeyType, ValueType>;

    struct Context {
        std::vector<ReadPageGuard> read_set_;
        std::vector<WritePageGuard> write_set_;

        void ClearRead() {
            for (auto it = read_set_.rbegin(); it != read_set_.rend(); ++it) {
                it->Drop();
            }
            read_set_.clear();
        }

        void ClearWrite() {
            for (auto it = write_set_.rbegin(); it != write_set_.rend(); ++it) {
                it->Drop();
            }
            write_set_.clear();
        }

        void Clear() {
            ClearRead();
            ClearWrite();
        }
    };

    BufferManager buffer_;
    mutable std::shared_mutex tree_latch_;

    static_assert(sizeof(LeafPageType) <= PAGE_SIZE, "LeafPage exceeds PAGE_SIZE");
    static_assert(sizeof(InternalPageType) <= PAGE_SIZE, "InternalPage exceeds PAGE_SIZE");

    static bool KeyEqualValue(const KeyType &a, const KeyType &b) {
        return key_equal(a, b);
    }

    ReadPageGuard FetchRead(page_id_t page_id) {
        return ReadPageGuard(&buffer_, buffer_.FetchPageForGuard(page_id));
    }

    WritePageGuard FetchWrite(page_id_t page_id) {
        return WritePageGuard(&buffer_, buffer_.FetchPageForGuard(page_id));
    }

    template<typename PageT>
    WritePageGuard NewPage(page_id_t page_id) {
        WritePageGuard guard(&buffer_, buffer_.NewPageForGuard(page_id));
        auto *page = guard.template AsMut<PageT>();
        page->Init();
        return guard;
    }

    void InitializeHeader() {
        WritePageGuard header_guard = FetchWrite(HEADER_PAGE_ID);
        auto *header = header_guard.template AsMut<HeaderPage>();
        if (header->type_ != PageType::Header) {
            header->Init();
        }
    }

    page_id_t GetRootPageId() {
        ReadPageGuard header_guard = FetchRead(HEADER_PAGE_ID);
        return header_guard.template As<HeaderPage>()->root_page_id_;
    }

    void SetRootPageId(page_id_t root_page_id) {
        WritePageGuard header_guard = FetchWrite(HEADER_PAGE_ID);
        header_guard.template AsMut<HeaderPage>()->root_page_id_ = root_page_id;
    }

    page_id_t AllocatePageId() {
        WritePageGuard header_guard = FetchWrite(HEADER_PAGE_ID);
        auto *header = header_guard.template AsMut<HeaderPage>();
        page_id_t page_id = header->next_page_id_;
        header->next_page_id_++;
        return page_id;
    }

    void AdjustTreeSize(int delta) {
        WritePageGuard header_guard = FetchWrite(HEADER_PAGE_ID);
        header_guard.template AsMut<HeaderPage>()->size_ += delta;
    }

    static int FindChildIndex(const InternalPageType *page, page_id_t child_page_id) {
        for (int index = 0; index < page->size_; ++index) {
            if (page->children_[index] == child_page_id) {
                return index;
            }
        }
        return -1;
    }

    static int LowerBoundInLeaf(const LeafPageType *page, const KeyPairType &key_pair) {
        if (page->size_ == 0) {
            return 0;
        }
        int index = page->LowerBound(key_pair);
        return index < 0 ? 0 : index;
    }

    static int LowerBoundInLeafByKey(const LeafPageType *page, const KeyType &key) {
        if (page->size_ == 0) {
            return 0;
        }
        int index = page->LowerBoundByKey(key);
        return index < 0 ? 0 : index;
    }

    static int LowerBoundInInternal(const InternalPageType *page, const KeyPairType &key_pair) {
        if (page->size_ == 0) {
            return 0;
        }
        int index = page->LowerBound(key_pair);
        return index < 0 ? 0 : index;
    }

    static int LowerBoundInInternalByKey(const InternalPageType *page, const KeyType &key) {
        if (page->size_ == 0) {
            return 0;
        }
        int index = page->LowerBoundByKey(key);
        return index < 0 ? 0 : index;
    }

    static void InsertIntoLeaf(LeafPageType *page, const KeyPairType &key_pair, int index) {
        if (page->size_ == 0) {
            page->data_[0] = key_pair;
            page->size_ = 1;
            return;
        }
        if (index < page->size_ && page->data_[index] < key_pair) {
            ++index;
        }
        for (int move = page->size_ - 1; move >= index; --move) {
            page->data_[move + 1] = page->data_[move];
        }
        page->data_[index] = key_pair;
        page->size_++;
    }

    static void EraseFromLeaf(LeafPageType *page, int index) {
        for (int move = index; move < page->size_ - 1; ++move) {
            page->data_[move] = page->data_[move + 1];
        }
        page->size_--;
    }

    static void InsertIntoInternalAfter(InternalPageType *page, int index, const KeyPairType &key_pair,
                                        page_id_t child_page_id) {
        for (int move = page->size_ - 1; move > index; --move) {
            page->data_[move + 1] = page->data_[move];
            page->children_[move + 1] = page->children_[move];
        }
        page->data_[index + 1] = key_pair;
        page->children_[index + 1] = child_page_id;
        page->size_++;
    }

    static void RemoveInternalEntryAt(InternalPageType *page, int index) {
        for (int move = index; move < page->size_ - 1; ++move) {
            page->data_[move] = page->data_[move + 1];
            page->children_[move] = page->children_[move + 1];
        }
        page->size_--;
    }

    void PropagateMaxKeyChange(page_id_t child_page_id, const KeyPairType &new_max) {
        page_id_t current_child = child_page_id;
        KeyPairType current_max = new_max;
        while (current_child != INVALID_PAGE_ID) {
            WritePageGuard child_guard = FetchWrite(current_child);
            const PageType child_type = *child_guard.template As<PageType>();
            page_id_t parent_page_id = INVALID_PAGE_ID;
            if (child_type == PageType::Leaf) {
                parent_page_id = child_guard.template As<LeafPageType>()->parent_;
            } else if (child_type == PageType::Internal) {
                parent_page_id = child_guard.template As<InternalPageType>()->parent_;
            }
            child_guard.Drop();
            if (parent_page_id == INVALID_PAGE_ID) {
                break;
            }
            WritePageGuard parent_guard = FetchWrite(parent_page_id);
            auto *parent = parent_guard.template AsMut<InternalPageType>();
            int index = FindChildIndex(parent, current_child);
            if (index < 0) {
                break;
            }
            parent->data_[index] = current_max;
            if (index != parent->size_ - 1) {
                break;
            }
            current_child = parent_page_id;
            current_max = parent->Back();
        }
    }

    void DescendReadByKey(const KeyType &key, Context &context) {
        page_id_t root_page_id = GetRootPageId();
        if (root_page_id == INVALID_PAGE_ID) {
            return;
        }
        ReadPageGuard current_guard = FetchRead(root_page_id);
        while (*current_guard.template As<PageType>() != PageType::Leaf) {
            const auto *internal = current_guard.template As<InternalPageType>();
            int index = LowerBoundInInternalByKey(internal, key);
            page_id_t child_page_id = internal->children_[index];
            ReadPageGuard next_guard = FetchRead(child_page_id);
            current_guard.Drop();
            current_guard = std::move(next_guard);
        }
        context.read_set_.push_back(std::move(current_guard));
    }

    void DescendWriteByPair(const KeyPairType &key_pair, Context &context) {
        page_id_t root_page_id = GetRootPageId();
        if (root_page_id == INVALID_PAGE_ID) {
            return;
        }
        context.write_set_.push_back(FetchWrite(root_page_id));
        while (*context.write_set_.back().template As<PageType>() != PageType::Leaf) {
            auto *internal = context.write_set_.back().template AsMut<InternalPageType>();
            int index = LowerBoundInInternal(internal, key_pair);
            context.write_set_.push_back(FetchWrite(internal->children_[index]));
        }
    }

    void UpdateChildParent(page_id_t child_page_id, page_id_t parent_page_id) {
        WritePageGuard child_guard = FetchWrite(child_page_id);
        if (*child_guard.template As<PageType>() == PageType::Leaf) {
            child_guard.template AsMut<LeafPageType>()->parent_ = parent_page_id;
        } else {
            child_guard.template AsMut<InternalPageType>()->parent_ = parent_page_id;
        }
    }

    void CreateNewRoot(page_id_t left_page_id, const KeyPairType &left_max, page_id_t right_page_id,
                       const KeyPairType &right_max) {
        page_id_t root_page_id = AllocatePageId();
        WritePageGuard root_guard = NewPage<InternalPageType>(root_page_id);
        auto *root = root_guard.template AsMut<InternalPageType>();
        root->size_ = 2;
        root->data_[0] = left_max;
        root->data_[1] = right_max;
        root->children_[0] = left_page_id;
        root->children_[1] = right_page_id;
        UpdateChildParent(left_page_id, root_page_id);
        UpdateChildParent(right_page_id, root_page_id);
        SetRootPageId(root_page_id);
    }

    void SplitInternal(Context &context, int level) {
        WritePageGuard &internal_guard = context.write_set_[level];
        auto *internal = internal_guard.template AsMut<InternalPageType>();
        page_id_t internal_page_id = internal_guard.PageId();

        page_id_t new_internal_page_id = AllocatePageId();
        WritePageGuard new_internal_guard = NewPage<InternalPageType>(new_internal_page_id);
        auto *new_internal = new_internal_guard.template AsMut<InternalPageType>();

        int move_start = internal->size_ / 2;
        int move_count = internal->size_ - move_start;
        for (int index = 0; index < move_count; ++index) {
            new_internal->data_[index] = internal->data_[move_start + index];
            new_internal->children_[index] = internal->children_[move_start + index];
            UpdateChildParent(new_internal->children_[index], new_internal_page_id);
        }
        new_internal->size_ = move_count;
        internal->size_ = move_start;

        new_internal->parent_ = internal->parent_;
        new_internal->left_sibling_ = internal_page_id;
        new_internal->right_sibling_ = internal->right_sibling_;
        if (new_internal->right_sibling_ != INVALID_PAGE_ID) {
            WritePageGuard right_guard = FetchWrite(new_internal->right_sibling_);
            right_guard.template AsMut<InternalPageType>()->left_sibling_ = new_internal_page_id;
        }
        internal->right_sibling_ = new_internal_page_id;

        KeyPairType left_max = internal->Back();
        KeyPairType right_max = new_internal->Back();
        if (internal->parent_ == INVALID_PAGE_ID) {
            CreateNewRoot(internal_page_id, left_max, new_internal_page_id, right_max);
            return;
        }

        WritePageGuard &parent_guard = context.write_set_[level - 1];
        auto *parent = parent_guard.template AsMut<InternalPageType>();
        int child_index = FindChildIndex(parent, internal_page_id);
        parent->data_[child_index] = left_max;
        InsertIntoInternalAfter(parent, child_index, right_max, new_internal_page_id);
        if (parent->size_ > static_cast<int>(InternalPageType::MAX_SIZE)) {
            SplitInternal(context, level - 1);
        } else if (child_index == parent->size_ - 2) {
            PropagateMaxKeyChange(parent_guard.PageId(), parent->Back());
        }
    }

    void SplitLeaf(Context &context) {
        WritePageGuard &leaf_guard = context.write_set_.back();
        auto *leaf = leaf_guard.template AsMut<LeafPageType>();
        page_id_t leaf_page_id = leaf_guard.PageId();

        page_id_t new_leaf_page_id = AllocatePageId();
        WritePageGuard new_leaf_guard = NewPage<LeafPageType>(new_leaf_page_id);
        auto *new_leaf = new_leaf_guard.template AsMut<LeafPageType>();

        int move_start = leaf->size_ / 2;
        int move_count = leaf->size_ - move_start;
        for (int index = 0; index < move_count; ++index) {
            new_leaf->data_[index] = leaf->data_[move_start + index];
        }
        new_leaf->size_ = move_count;
        leaf->size_ = move_start;

        new_leaf->parent_ = leaf->parent_;
        new_leaf->left_sibling_ = leaf_page_id;
        new_leaf->right_sibling_ = leaf->right_sibling_;
        if (new_leaf->right_sibling_ != INVALID_PAGE_ID) {
            WritePageGuard right_guard = FetchWrite(new_leaf->right_sibling_);
            right_guard.template AsMut<LeafPageType>()->left_sibling_ = new_leaf_page_id;
        }
        leaf->right_sibling_ = new_leaf_page_id;

        KeyPairType left_max = leaf->Back();
        KeyPairType right_max = new_leaf->Back();
        if (leaf->parent_ == INVALID_PAGE_ID) {
            CreateNewRoot(leaf_page_id, left_max, new_leaf_page_id, right_max);
            return;
        }

        WritePageGuard &parent_guard = context.write_set_[context.write_set_.size() - 2];
        auto *parent = parent_guard.template AsMut<InternalPageType>();
        int child_index = FindChildIndex(parent, leaf_page_id);
        parent->data_[child_index] = left_max;
        InsertIntoInternalAfter(parent, child_index, right_max, new_leaf_page_id);
        if (parent->size_ > static_cast<int>(InternalPageType::MAX_SIZE)) {
            SplitInternal(context, static_cast<int>(context.write_set_.size()) - 2);
        } else if (child_index == parent->size_ - 2) {
            PropagateMaxKeyChange(parent_guard.PageId(), parent->Back());
        }
    }

    void RebalanceInternal(page_id_t page_id) {
        WritePageGuard current_guard = FetchWrite(page_id);
        auto *current = current_guard.template AsMut<InternalPageType>();
        if (current->parent_ == INVALID_PAGE_ID) {
            if (current->size_ == 1) {
                page_id_t child_page_id = current->children_[0];
                UpdateChildParent(child_page_id, INVALID_PAGE_ID);
                SetRootPageId(child_page_id);
            } else if (current->size_ == 0) {
                SetRootPageId(INVALID_PAGE_ID);
            }
            return;
        }
        if (current->size_ >= static_cast<int>(InternalPageType::HALF_SIZE)) {
            PropagateMaxKeyChange(page_id, current->Back());
            return;
        }

        WritePageGuard parent_guard = FetchWrite(current->parent_);
        auto *parent = parent_guard.template AsMut<InternalPageType>();
        int index = FindChildIndex(parent, page_id);

        if (index > 0) {
            WritePageGuard left_guard = FetchWrite(parent->children_[index - 1]);
            auto *left = left_guard.template AsMut<InternalPageType>();
            if (left->size_ > static_cast<int>(InternalPageType::HALF_SIZE)) {
                for (int move = current->size_ - 1; move >= 0; --move) {
                    current->data_[move + 1] = current->data_[move];
                    current->children_[move + 1] = current->children_[move];
                }
                current->data_[0] = left->data_[left->size_ - 1];
                current->children_[0] = left->children_[left->size_ - 1];
                current->size_++;
                left->size_--;
                UpdateChildParent(current->children_[0], page_id);
                parent->data_[index - 1] = left->Back();
                parent->data_[index] = current->Back();
                PropagateMaxKeyChange(parent_guard.PageId(), parent->Back());
                return;
            }
        }

        if (index + 1 < parent->size_) {
            WritePageGuard right_guard = FetchWrite(parent->children_[index + 1]);
            auto *right = right_guard.template AsMut<InternalPageType>();
            if (right->size_ > static_cast<int>(InternalPageType::HALF_SIZE)) {
                current->data_[current->size_] = right->data_[0];
                current->children_[current->size_] = right->children_[0];
                current->size_++;
                UpdateChildParent(current->children_[current->size_ - 1], page_id);
                for (int move = 0; move < right->size_ - 1; ++move) {
                    right->data_[move] = right->data_[move + 1];
                    right->children_[move] = right->children_[move + 1];
                }
                right->size_--;
                parent->data_[index] = current->Back();
                parent->data_[index + 1] = right->Back();
                PropagateMaxKeyChange(parent_guard.PageId(), parent->Back());
                return;
            }
        }

        if (index > 0) {
            WritePageGuard left_guard = FetchWrite(parent->children_[index - 1]);
            auto *left = left_guard.template AsMut<InternalPageType>();
            int base = left->size_;
            for (int move = 0; move < current->size_; ++move) {
                left->data_[base + move] = current->data_[move];
                left->children_[base + move] = current->children_[move];
                UpdateChildParent(current->children_[move], left_guard.PageId());
            }
            left->size_ += current->size_;
            left->right_sibling_ = current->right_sibling_;
            if (current->right_sibling_ != INVALID_PAGE_ID) {
                WritePageGuard sibling_guard = FetchWrite(current->right_sibling_);
                sibling_guard.template AsMut<InternalPageType>()->left_sibling_ = left_guard.PageId();
            }
            RemoveInternalEntryAt(parent, index);
            parent->data_[index - 1] = left->Back();
            RebalanceInternal(parent_guard.PageId());
            return;
        }

        WritePageGuard right_guard = FetchWrite(parent->children_[index + 1]);
        auto *right = right_guard.template AsMut<InternalPageType>();
        int base = current->size_;
        for (int move = 0; move < right->size_; ++move) {
            current->data_[base + move] = right->data_[move];
            current->children_[base + move] = right->children_[move];
            UpdateChildParent(right->children_[move], page_id);
        }
        current->size_ += right->size_;
        current->right_sibling_ = right->right_sibling_;
        if (right->right_sibling_ != INVALID_PAGE_ID) {
            WritePageGuard sibling_guard = FetchWrite(right->right_sibling_);
            sibling_guard.template AsMut<InternalPageType>()->left_sibling_ = page_id;
        }
        RemoveInternalEntryAt(parent, index + 1);
        parent->data_[index] = current->Back();
        RebalanceInternal(parent_guard.PageId());
    }

    void RebalanceLeaf(page_id_t page_id) {
        WritePageGuard current_guard = FetchWrite(page_id);
        auto *current = current_guard.template AsMut<LeafPageType>();
        if (current->parent_ == INVALID_PAGE_ID) {
            if (current->size_ == 0) {
                SetRootPageId(INVALID_PAGE_ID);
            }
            return;
        }
        if (current->size_ >= static_cast<int>(LeafPageType::HALF_SIZE)) {
            PropagateMaxKeyChange(page_id, current->Back());
            return;
        }

        WritePageGuard parent_guard = FetchWrite(current->parent_);
        auto *parent = parent_guard.template AsMut<InternalPageType>();
        int index = FindChildIndex(parent, page_id);

        if (index > 0) {
            WritePageGuard left_guard = FetchWrite(parent->children_[index - 1]);
            auto *left = left_guard.template AsMut<LeafPageType>();
            if (left->size_ > static_cast<int>(LeafPageType::HALF_SIZE)) {
                for (int move = current->size_ - 1; move >= 0; --move) {
                    current->data_[move + 1] = current->data_[move];
                }
                current->data_[0] = left->data_[left->size_ - 1];
                current->size_++;
                left->size_--;
                parent->data_[index - 1] = left->Back();
                parent->data_[index] = current->Back();
                PropagateMaxKeyChange(parent_guard.PageId(), parent->Back());
                return;
            }
        }

        if (index + 1 < parent->size_) {
            WritePageGuard right_guard = FetchWrite(parent->children_[index + 1]);
            auto *right = right_guard.template AsMut<LeafPageType>();
            if (right->size_ > static_cast<int>(LeafPageType::HALF_SIZE)) {
                current->data_[current->size_] = right->data_[0];
                current->size_++;
                for (int move = 0; move < right->size_ - 1; ++move) {
                    right->data_[move] = right->data_[move + 1];
                }
                right->size_--;
                parent->data_[index] = current->Back();
                parent->data_[index + 1] = right->Back();
                PropagateMaxKeyChange(parent_guard.PageId(), parent->Back());
                return;
            }
        }

        if (index > 0) {
            WritePageGuard left_guard = FetchWrite(parent->children_[index - 1]);
            auto *left = left_guard.template AsMut<LeafPageType>();
            int base = left->size_;
            for (int move = 0; move < current->size_; ++move) {
                left->data_[base + move] = current->data_[move];
            }
            left->size_ += current->size_;
            left->right_sibling_ = current->right_sibling_;
            if (current->right_sibling_ != INVALID_PAGE_ID) {
                WritePageGuard sibling_guard = FetchWrite(current->right_sibling_);
                sibling_guard.template AsMut<LeafPageType>()->left_sibling_ = left_guard.PageId();
            }
            RemoveInternalEntryAt(parent, index);
            parent->data_[index - 1] = left->Back();
            RebalanceInternal(parent_guard.PageId());
            return;
        }

        WritePageGuard right_guard = FetchWrite(parent->children_[index + 1]);
        auto *right = right_guard.template AsMut<LeafPageType>();
        int base = current->size_;
        for (int move = 0; move < right->size_; ++move) {
            current->data_[base + move] = right->data_[move];
        }
        current->size_ += right->size_;
        current->right_sibling_ = right->right_sibling_;
        if (right->right_sibling_ != INVALID_PAGE_ID) {
            WritePageGuard sibling_guard = FetchWrite(right->right_sibling_);
            sibling_guard.template AsMut<LeafPageType>()->left_sibling_ = page_id;
        }
        RemoveInternalEntryAt(parent, index + 1);
        parent->data_[index] = current->Back();
        RebalanceInternal(parent_guard.PageId());
    }

    void InsertPessimistic(const KeyPairType &key_pair) {
        std::unique_lock<std::shared_mutex> lock(tree_latch_);
        page_id_t root_page_id = GetRootPageId();
        if (root_page_id == INVALID_PAGE_ID) {
            page_id_t new_root_page_id = AllocatePageId();
            WritePageGuard root_guard = NewPage<LeafPageType>(new_root_page_id);
            auto *root = root_guard.template AsMut<LeafPageType>();
            root->data_[0] = key_pair;
            root->size_ = 1;
            SetRootPageId(new_root_page_id);
            AdjustTreeSize(1);
            return;
        }

        Context context;
        DescendWriteByPair(key_pair, context);
        auto *leaf = context.write_set_.back().template AsMut<LeafPageType>();
        int index = LowerBoundInLeaf(leaf, key_pair);
        if (index < leaf->size_ && leaf->data_[index] == key_pair) {
            return;
        }
        KeyPairType old_max = leaf->size_ > 0 ? leaf->Back() : key_pair;
        InsertIntoLeaf(leaf, key_pair, index);
        AdjustTreeSize(1);
        if (leaf->size_ > static_cast<int>(LeafPageType::MAX_SIZE)) {
            SplitLeaf(context);
            return;
        }
        if (!(leaf->Back() == old_max)) {
            PropagateMaxKeyChange(context.write_set_.back().PageId(), leaf->Back());
        }
    }

    void ErasePessimistic(const KeyPairType &key_pair) {
        std::unique_lock<std::shared_mutex> lock(tree_latch_);
        page_id_t root_page_id = GetRootPageId();
        if (root_page_id == INVALID_PAGE_ID) {
            return;
        }

        Context context;
        DescendWriteByPair(key_pair, context);
        auto *leaf = context.write_set_.back().template AsMut<LeafPageType>();
        int index = LowerBoundInLeaf(leaf, key_pair);
        if (index >= leaf->size_ || !(leaf->data_[index] == key_pair)) {
            return;
        }
        KeyPairType old_max = leaf->Back();
        EraseFromLeaf(leaf, index);
        AdjustTreeSize(-1);
        if (leaf->parent_ == INVALID_PAGE_ID) {
            if (leaf->size_ == 0) {
                SetRootPageId(INVALID_PAGE_ID);
            }
            return;
        }
        if (leaf->size_ == 0 || leaf->size_ < static_cast<int>(LeafPageType::HALF_SIZE)) {
            RebalanceLeaf(context.write_set_.back().PageId());
            return;
        }
        if (!(leaf->Back() == old_max)) {
            PropagateMaxKeyChange(context.write_set_.back().PageId(), leaf->Back());
        }
    }

public:
    explicit BPlusTree(const std::string &file_name = "bpt.dat") : buffer_(CACHE_CAPACITY, file_name) {
        InitializeHeader();
    }

    ~BPlusTree() = default;

    std::optional<ValueType> find(const KeyType &key) {
        std::shared_lock<std::shared_mutex> lock(tree_latch_);
        Context context;
        DescendReadByKey(key, context);
        if (context.read_set_.empty()) {
            return std::nullopt;
        }
        const auto *leaf = context.read_set_.back().template As<LeafPageType>();
        int index = LowerBoundInLeafByKey(leaf, key);
        if (index >= leaf->size_ || !KeyEqualValue(leaf->data_[index].key_, key)) {
            return std::nullopt;
        }
        return leaf->data_[index].val_;
    }

    void find_all(const KeyType &key, std::vector<ValueType> &vec) {
        std::shared_lock<std::shared_mutex> lock(tree_latch_);
        vec.clear();
        Context context;
        DescendReadByKey(key, context);
        if (context.read_set_.empty()) {
            return;
        }

        ReadPageGuard current_guard = std::move(context.read_set_.back());
        context.read_set_.pop_back();
        while (current_guard.IsValid()) {
            const auto *leaf = current_guard.template As<LeafPageType>();
            int index = LowerBoundInLeafByKey(leaf, key);
            while (index < leaf->size_ && KeyEqualValue(leaf->data_[index].key_, key)) {
                vec.push_back(leaf->data_[index].val_);
                ++index;
            }
            if (index < leaf->size_ || leaf->right_sibling_ == INVALID_PAGE_ID) {
                break;
            }
            page_id_t next_page_id = leaf->right_sibling_;
            current_guard.Drop();
            current_guard = FetchRead(next_page_id);
            const auto *next_leaf = current_guard.template As<LeafPageType>();
            if (next_leaf->size_ == 0 || !KeyEqualValue(next_leaf->data_[0].key_, key)) {
                break;
            }
        }
    }

    void insert(const KeyType &key, const ValueType &value) {
        KeyPairType key_pair(key, value);
        InsertPessimistic(key_pair);
    }

    void erase(const KeyType &key, const ValueType &value) {
        KeyPairType key_pair(key, value);
        ErasePessimistic(key_pair);
    }
};

} // namespace sjtu

#endif // BPT_HPP
