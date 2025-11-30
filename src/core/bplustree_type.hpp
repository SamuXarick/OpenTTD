/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bplustree_type.hpp BPlusTree container implementation. */

#ifndef BPLUSTREE_TYPE_HPP
#define BPLUSTREE_TYPE_HPP

#include <iostream>

template <typename Key, typename Value, size_t B = 64>
class BPlusTree;

template <typename Key, typename Value, size_t B>
struct BPlusNode {
	bool is_leaf;
	size_t count; // number of keys currently stored

	std::array<Key, B> keys;
	// For internal nodes: child pointers
	std::array<std::unique_ptr<BPlusNode>, B + 1> children;

	// For leaf nodes: values + linked list
	std::array<Value, B> values;
	BPlusNode *next_leaf = nullptr;
	BPlusNode *prev_leaf = nullptr;

	BPlusNode(bool leaf = true) : is_leaf(leaf), count(0) {}
};

template <typename Key, typename Value, size_t B>
class BPlusTree {
	using Node = BPlusNode<Key, Value, B>;
	std::unique_ptr<Node> root;

	static constexpr size_t max_keys = B;
	static constexpr size_t max_children = B + 1;
public:
	BPlusTree() : root(std::make_unique<Node>(true)) {}
	BPlusTree(BPlusTree &&) = default;
	BPlusTree &operator=(BPlusTree &&) = default;

	// ---- Iterator types -----------------------------------------------------
	struct iterator {
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type        = std::pair<const Key, Value>;
		using difference_type   = std::ptrdiff_t;
		using pointer           = void;
		using reference         = std::pair<const Key &, Value &>;

		Node *leaf_ = nullptr;
		size_t index_ = 0;
		const BPlusTree *owner_ = nullptr;

		// Dereference
		reference operator*() const {
			return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] };
		}

		struct Proxy {
			const Key *k;
			Value *v;
			const Key &first() const { return *k; }
			Value &second() const { return *v; }
		};

		Proxy operator->() const {
			return Proxy{ &this->leaf_->keys[this->index_], &this->leaf_->values[this->index_] };
		}

		// Increment
		iterator &operator++() {
//			std::cout << "Leaf=" << leaf_ << " index=" << index_ << " count=" << leaf_->count << "\n";

			if (this->leaf_ == nullptr) return *this;
			if (++this->index_ >= this->leaf_->count) {
				this->leaf_ = this->leaf_->next_leaf;
				this->index_ = 0;
			}
			return *this;
		}

		iterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }

		// Decrement
		iterator &operator--() {
			if (this->leaf_ == nullptr) {
				// Special case: --end() should land on the last element
				this->leaf_ = this->owner_->rightmost_leaf();           // helper to find last leaf
				this->index_ = this->leaf_ != nullptr? this->leaf_->count - 1 : 0;
				return *this;
			}

			if (this->index_ == 0) {
				// Move to previous leaf
				this->leaf_ = this->leaf_->prev_leaf;
				if (this->leaf_ != nullptr) this->index_ = this->leaf_->count - 1;
			} else {
				--this->index_;
			}
			return *this;
		}

		iterator operator--(int) {
			auto tmp = *this;
			--(*this);
			return tmp;
		}

		// Equality
		friend bool operator==(const iterator &a, const iterator &b) {
			return a.leaf_ == b.leaf_ && a.index_ == b.index_;
		}

		friend bool operator!=(const iterator &a, const iterator &b) {
			return !(a == b);
		}
	};

	struct const_iterator {
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type        = std::pair<const Key, const Value>;
		using difference_type   = std::ptrdiff_t;
		using pointer           = void;
		using reference         = std::pair<const Key &, const Value &>;

		const Node *leaf_ = nullptr;
		size_t index_ = 0;
		const BPlusTree *owner_ = nullptr; // back-pointer to the tree

		reference operator*() const {
			return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] };
		}

		struct Proxy {
			const Key *k;
			const Value *v;
			const Key &first() const { return *k; }
			const Value &second() const { return *v; }
		};

		Proxy operator->() const {
			return Proxy{ &this->leaf_->keys[this->index_], &this->leaf_->values[this->index_] };
		}

		const_iterator &operator++() {
			if (this->leaf_ == nullptr) return *this;
			if (++this->index_ >= this->leaf_->count) {
				this->leaf_ = this->leaf_->next_leaf;
				this->index_ = 0;
			}
			return *this;
		}
		const_iterator operator++(int) { auto tmp = *this; ++(*this); return tmp; }

		// Decrement
		const_iterator &operator--() {
			if (this->leaf_ == nullptr) {
				// Special case: --end() should land on the last element
				this->leaf_ = this->owner_->rightmost_leaf();           // helper to find last leaf
				this->index_ = this->leaf_ != nullptr? this->leaf_->count - 1 : 0;
				return *this;
			}

			if (this->index_ == 0) {
				// Move to previous leaf
				this->leaf_ = this->leaf_->prev_leaf;
				if (this->leaf_ != nullptr) this->index_ = this->leaf_->count - 1;
			} else {
				--this->index_;
			}
			return *this;
		}

		const_iterator operator--(int) {
			auto tmp = *this;
			--(*this);
			return tmp;
		}
		friend bool operator==(const const_iterator &a, const const_iterator &b) {
			return a.leaf_ == b.leaf_ && a.index_ == b.index_;
		}

		friend bool operator!=(const const_iterator &a, const const_iterator &b) {
			return !(a == b);
		}
	};

	Node *leftmost_leaf() const {
		Node *node = this->root.get();
		if (node == nullptr) return nullptr;
		while (!node->is_leaf) {
			node = node->children[0].get();
		}
		return node;
	}

	Node *rightmost_leaf() const {
		Node *node = this->root.get();
		if (node == nullptr) return nullptr;
		while (!node->is_leaf) {
			node = node->children[node->count].get(); // rightmost child
		}
		return node;
	}

	void insert(const Key &key, const Value &value) {
		if (this->root == nullptr) {
			// Defensive: if root was somehow null, create a leaf
			this->root = std::make_unique<Node>(true);
		}
		if (this->root->count == B) {
			// Split root
			auto new_root = std::make_unique<Node>(false);
			new_root->children[0] = std::move(this->root);
			this->split_child(new_root.get(), 0);
			this->root = std::move(new_root);
		}
		this->insert_nonfull(this->root.get(), key, value);
		this->check_invariants(this->root.get());
	}

	void erase(const Key &key) {
		this->erase_recursive(this->root.get(), key);

		// If root is internal and became empty, shrink height
		if (!this->root->is_leaf && this->root->count == 0) {
			this->root = std::move(this->root->children[0]);
		}
	}

	bool erase_recursive(Node *node, const Key &key) {
		if (node->is_leaf) {
			// Find key in leaf
			size_t i = this->lower_bound(node->keys, node->count, key);
			if (i == node->count || node->keys[i] != key) return false; // not found

			// Shift left to erase
			for (size_t j = i; j + 1 < node->count; ++j) {
				node->keys[j]   = std::move(node->keys[j + 1]);
				node->values[j] = std::move(node->values[j + 1]);
			}
			--node->count;
			return true;
		} else {
			// Internal node: descend
			size_t i = this->lower_bound(node->keys, node->count, key);
			Node *child = node->children[i].get();

			bool erased = this->erase_recursive(child, key);

			// After erase, check underflow
			if (child->count < this->min_keys(child->is_leaf)) {
				this->fix_underflow(node, i);
			}

			// Update separator if needed
			if (i < node->count && node->children[i]->is_leaf) {
				node->keys[i] = node->children[i + 1]->keys[0];
			}

			return erased;
		}
	}

	// Minimum keys allowed in a node
	size_t min_keys(bool is_leaf) const {
		// For B+ trees, leaves usually require at least ceil(B/2) entries.
		// Internal nodes require at least ceil(B/2) - 1 keys.
		return is_leaf ? (B + 1) / 2 : (B + 1) / 2 - 1;
	}

	void fix_underflow(Node *parent, size_t i) {
		Node *child = parent->children[i].get();

		// Try borrow from left sibling
		if (i > 0 && parent->children[i - 1]->count > this->min_keys(parent->children[i - 1]->is_leaf)) {
			this->borrow_from_left(parent, i);
			return;
		}

		// Try borrow from right sibling
		if (i + 1 <= parent->count && parent->children[i + 1]->count > min_keys(parent->children[i + 1]->is_leaf)) {
			this->borrow_from_right(parent, i);
			return;
		}

		// Otherwise, merge with a sibling
		if (i + 1 <= parent->count) {
			this->merge_child(parent, i); // merge with right sibling
		} else {
			this->merge_child(parent, i - 1); // merge with left sibling
		}
	}

	// Merge parent->children[i] with its right sibling (i+1)
	void merge_child(Node *parent, size_t i) {
		assert(parent != nullptr && "merge_child: parent must exist");
		assert(i < parent->count && "merge_child: i must be a valid separator index");

		Node *left  = parent->children[i].get();
		Node *right = parent->children[i + 1].get();
		assert(left != nullptr && right != nullptr && "merge_child: both children must exist");

		if (left->is_leaf) {
			// Move all keys/values from right into left
			for (size_t j = 0; j < right->count; ++j) {
				left->keys[left->count + j]   = std::move(right->keys[j]);
				left->values[left->count + j] = std::move(right->values[j]);
			}
			left->count += right->count;

			// Fix leaf links (skip right)
			left->next_leaf = right->next_leaf;
			if (right->next_leaf != nullptr) right->next_leaf->prev_leaf = left;

			// Remove separator/key and child in parent (compact arrays)
			for (size_t j = i; j + 1 < parent->count; ++j) {
				parent->keys[j] = std::move(parent->keys[j + 1]);
				parent->children[j + 1] = std::move(parent->children[j + 2]);
			}
			parent->children[parent->count] = nullptr; // clear dangling slot
			parent->count--;
			// right is released when unique_ptr shifts; no explicit delete needed
		} else {
			// Internal merge: bring down separator, append right's keys/children
			size_t left_count = left->count;

			// Place separator from parent
			left->keys[left_count] = std::move(parent->keys[i]);

			// Append right's keys
			for (size_t j = 0; j < right->count; ++j) {
				left->keys[left_count + 1 + j] = std::move(right->keys[j]);
			}
			// Append right's children
			for (size_t j = 0; j < right->count + 1; ++j) {
				left->children[left_count + 1 + j] = std::move(right->children[j]);
			}

			left->count = left_count + 1 + right->count;

			// Compact parent
			for (size_t j = i; j + 1 < parent->count; ++j) {
				parent->keys[j] = std::move(parent->keys[j + 1]);
				parent->children[j + 1] = std::move(parent->children[j + 2]);
			}
			parent->children[parent->count] = nullptr;
			parent->count--;
		}
	}

	// Optional: borrow from right sibling (rotation) to fix underflow without merging
	void borrow_from_right(Node *parent, size_t i) {
		Node *child = parent->children[i].get();
		Node *right = parent->children[i + 1].get();
		assert(child != nullptr && right != nullptr);

		if (child->is_leaf) {
			// Move first key/value from right to end of child
			child->keys[child->count]   = std::move(right->keys[0]);
			child->values[child->count] = std::move(right->values[0]);
			child->count++;

			// Shift right one left
			for (size_t j = 0; j + 1 < right->count; ++j) {
				right->keys[j]   = std::move(right->keys[j + 1]);
				right->values[j] = std::move(right->values[j + 1]);
			}
			right->count--;

			// Update separator to right's new first key
			parent->keys[i] = right->keys[0];
		} else {
			// Internal rotation
			// Move parent separator down to end of child
			child->keys[child->count] = std::move(parent->keys[i]);
			// Move right's first child to end of child
			child->children[child->count + 1] = std::move(right->children[0]);
			// Promote right's first key up to parent
			parent->keys[i] = std::move(right->keys[0]);

			// Shift right's keys/children left
			for (size_t j = 0; j + 1 < right->count; ++j) {
				right->keys[j] = std::move(right->keys[j + 1]);
			}
			for (size_t j = 0; j + 1 <= right->count; ++j) {
				right->children[j] = std::move(right->children[j + 1]);
			}

			child->count++;
			right->count--;
		}
	}

	// Optional: borrow from left sibling (rotation)
	void borrow_from_left(Node *parent, size_t i) {
		Node *left  = parent->children[i - 1].get();
		Node *child = parent->children[i].get();
		assert(left != nullptr && child != nullptr);

		if (child->is_leaf) {
			// Shift child right to make room at front
			for (size_t j = child->count; j > 0; --j) {
				child->keys[j]   = std::move(child->keys[j - 1]);
				child->values[j] = std::move(child->values[j - 1]);
			}
			// Move left's last key/value to front of child
			child->keys[0]   = std::move(left->keys[left->count - 1]);
			child->values[0] = std::move(left->values[left->count - 1]);
			child->count++;
			left->count--;

			// Update separator to child's new first key
			parent->keys[i - 1] = child->keys[0];
		} else {
			// Internal rotation
			// Shift child's keys/children right
			for (size_t j = child->count; j > 0; --j) {
				child->keys[j] = std::move(child->keys[j - 1]);
			}
			for (size_t j = child->count + 1; j > 0; --j) {
				child->children[j] = std::move(child->children[j - 1]);
			}
			// Bring parent separator down to front of child
			child->keys[0] = std::move(parent->keys[i - 1]);
			// Move left's last child to front of child
			child->children[0] = std::move(left->children[left->count]);
			// Promote left's last key up to parent
			parent->keys[i - 1] = std::move(left->keys[left->count - 1]);

			child->count++;
			left->count--;
		}
	}

	// Return iterator to first element
	iterator begin() { 
		Node *first = this->leftmost_leaf();
		return iterator(first, 0, this);
	}

	const_iterator begin() const {
		const Node *first = this->leftmost_leaf();
		return const_iterator(first, 0, this);
	}

	const_iterator cbegin() const { return this->begin(); }

	// Return iterator to "one past the last element"
	iterator end() {
		return iterator(nullptr, 0, this); // sentinel
	}

	const_iterator end() const {
		return const_iterator(nullptr, 0, this); // sentinel
	}

	const_iterator cend() const { return this->end(); }

	// Lower-bound as iterator (ordered seek).
	iterator lower_bound(const Key &key) {
		Node *node = this->root.get();
		while (!node->is_leaf) {
			size_t i = this->lower_bound(node->keys, node->count, key);
			node = node->children[i].get();
		}
		size_t i = this->lower_bound(node->keys, node->count, key);
		if (i >= node->count) {
			// next leaf (or end) if key is greater than all in this leaf
			node = node->next_leaf;
			if (node == nullptr) return this->end();
			return iterator(node, 0, this);
		}
		return iterator(node, i, this);
	}

	// Deep copy constructor
	BPlusTree(const BPlusTree &other) {
		this->root = this->clone_node(other.root.get());
		this->repair_leaf_links();
	}

	// Deep copy assignment
	BPlusTree &operator=(const BPlusTree &other) {
		if (this != &other) {
			this->root = this->clone_node(other.root.get());
			this->repair_leaf_links();
		}
		return *this;
	}

	// Helper: recursively clone a node
	std::unique_ptr<Node> clone_node(const Node *src) {
		if (src == nullptr) return nullptr;
		auto dst = std::make_unique<Node>(src->is_leaf);
		dst->count = src->count;
		dst->keys  = src->keys;

		if (src->is_leaf) {
			dst->values = src->values;
			// Leaf links (next/prev) are fixed later in a second pass
		} else {
			for (size_t i = 0; i <= src->count; ++i) {
				dst->children[i] = this->clone_node(src->children[i].get());
			}
		}
		return dst;
	}

	void collect_leaves(Node *node, std::vector<Node *> &leaves) {
		if (node == nullptr) return;
		if (node->is_leaf) {
			leaves.push_back(node);
		} else {
			for (size_t i = 0; i <= node->count; ++i) {
				this->collect_leaves(node->children[i].get(), leaves);
			}
		}
	}

	void repair_leaf_links() {
		std::vector<Node *> leaves;
		this->collect_leaves(this->root.get(), leaves);

		Node *prev = nullptr;
		for (Node *leaf : leaves) {
			leaf->prev_leaf = prev;
			if (prev != nullptr) prev->next_leaf = leaf;
			prev = leaf;
		}
		if (prev != nullptr) prev->next_leaf = nullptr; // last leaf
	}

	bool contains(const Key &key) const {
		return this->find(key) != this->end();
	}

	iterator find(const Key &key) {
		Node *node = this->root.get();
		while (node != nullptr && !node->is_leaf) {
			size_t i = this->lower_bound(node->keys, node->count, key);
			if (i < node->count && node->keys[i] == key) ++i; // equality goes right
			assert(i <= node->count);                // children size is count+1
			assert(node->children[i] != nullptr);
			node = node->children[i].get();
		}
		if (node == nullptr) return this->end();

		size_t i = this->lower_bound(node->keys, node->count, key);
		if (i < node->count && node->keys[i] == key) {
			return iterator(node, i, this);
		}
		return this->end();
	}

	const_iterator find(const Key &key) const {
		const Node *node = this->root.get();
		while (node != nullptr && !node->is_leaf) {
			size_t i = this->lower_bound(node->keys, node->count, key);
			if (i < node->count && node->keys[i] == key) ++i; // equality goes right
			assert(i <= node->count);                // children size is count+1
			assert(node->children[i] != nullptr);
			node = node->children[i].get();
		}
		if (node == nullptr) return this->cend();

		size_t i = this->lower_bound(node->keys, node->count, key);
		if (i < node->count && node->keys[i] == key) {
			return const_iterator(node, i, this);
		}
		return this->cend();
	}

	void swap(BPlusTree &other) noexcept {
		this->root.swap(other.root);
	}

	void clear() noexcept {
		// Reset to a fresh empty leaf node
		this->root = std::make_unique<Node>(true); 
	}

	bool empty() const noexcept {
		return this->root == nullptr || this->root->count == 0;
	}

	size_t size() const noexcept {
		return this->count_recursive(this->root.get());
	}

	std::pair<iterator, bool> try_emplace(const Key &key, const Value &value) {
		// First, search for key
		auto it = this->find(key);
		if (it != this->end()) {
			// Already exists: return iterator and false
			return {it, false};
		}

		// Otherwise, insert new key/value
		this->insert(key, value);
		it = this->find(key); // find again to get iterator to new element
		return {it, true};
	}

	iterator erase(iterator pos) {
		if (pos == this->end()) return this->end();

		Node *leaf = pos.leaf_;
		size_t i   = pos.index_;

		// Shift left to erase key/value at index i
		for (size_t j = i; j + 1 < leaf->count; ++j) {
			leaf->keys[j]   = std::move(leaf->keys[j + 1]);
			leaf->values[j] = std::move(leaf->values[j + 1]);
		}
		--leaf->count;

		// Return iterator to next element
		if (i < leaf->count) {
			return iterator(leaf, i, this);
		} else if (leaf->next_leaf) {
			return iterator(leaf->next_leaf, 0, this);
		} else {
			return this->end();
		}
	}

private:
	size_t count_recursive(const Node *node) const {
		if (node == nullptr) return 0;
		if (node->is_leaf) return node->count;
		size_t total = 0;
		for (size_t i = 0; i <= node->count; ++i) {
			total += this->count_recursive(node->children[i].get());
		}
		return total;
	}

	// Iteration: start from leftmost leaf
	Node *begin_leaf() const {
		Node *node = this->root.get();
		while (!node->is_leaf) node = node->children[0].get();
		return node;
	}

	// Binary search over keys[0..count), returning first index i where keys[i] >= key.
	static size_t lower_bound(const std::array<Key, B> &keys, size_t count, const Key &key) {
		size_t lo = 0;
		size_t hi = count;
		// Invariant: [lo, hi) is the search range.
		while (lo < hi) {
			size_t mid = lo + ((hi - lo) >> 1);
			if (keys[mid] < key) {
				lo = mid + 1;
			} else {
				hi = mid;
			}
		}
		return lo;
	}

	// Insert into a node that is guaranteed non-full.
	void insert_nonfull(Node *node, const Key &key, const Value &value) {
		if (node->is_leaf) {
			// Find position and insert key/value
			size_t i = this->lower_bound(node->keys, node->count, key);
			if (i < node->count && node->keys[i] == key) {
				// Overwrite value (B+ trees typically allow update on duplicate key)
				node->values[i] = value;
				return;
			}
			// Shift right to make room
			for (size_t j = node->count; j > i; --j) {
				node->keys[j] = std::move(node->keys[j - 1]);
				node->values[j] = std::move(node->values[j - 1]);
			}
			node->keys[i] = key;
			node->values[i] = value;
			++node->count;
		} else {
			// Internal: descend into the correct child
			size_t i = this->lower_bound(node->keys, node->count, key);
			Node *child = node->children[i].get();
			if (child->count == B) {
				// Child is full; split it and decide where to descend
				this->split_child(node, i);
				// After split, decide which child to descend based on promoted key
				if (key >= node->keys[i]) {
					child = node->children[i + 1].get();
				}
			}
			this->insert_nonfull(child, key, value);
		}
	}

	// Split parent->children[i] into two siblings and update parent
	void split_child(Node *parent, size_t i) {
		Node *child = parent->children[i].get();
		assert(child != nullptr && "split_child: child must exist");
		assert(parent->count < max_keys && "split_child: parent must have space");
		assert(child->count == max_keys && "split_child: child must be full to split");

		auto new_node = std::make_unique<Node>(child->is_leaf);
		size_t mid = child->count / 2; // lower mid for even B

		if (child->is_leaf) {
			// Move keys/values [mid .. count-1] to new leaf
			const size_t right_count = child->count - mid;
			for (size_t j = 0; j < right_count; ++j) {
				new_node->keys[j]   = std::move(child->keys[mid + j]);
				new_node->values[j] = std::move(child->values[mid + j]);
			}
			new_node->count = right_count;
			child->count    = mid;

			// Wire leaf links
			new_node->next_leaf = child->next_leaf;
			if (child->next_leaf != nullptr) child->next_leaf->prev_leaf = new_node.get();
			child->next_leaf = new_node.get();
			new_node->prev_leaf = child;

			// Shift parent keys to make room at position i
			for (size_t j = parent->count; j > i; --j) {
				parent->keys[j] = std::move(parent->keys[j - 1]);
			}
			// Shift parent children to make room at position i+1
			for (size_t j = parent->count + 1; j > i + 1; --j) {
				parent->children[j] = std::move(parent->children[j - 1]);
			}

			// Separator is first key of right leaf (remains in the leaf)
			parent->keys[i] = new_node->keys[0];
			parent->children[i + 1] = std::move(new_node);
			++parent->count;

			// Sanity checks for leaf split
			assert(parent->children[i] != nullptr);
			assert(parent->children[i + 1] != nullptr);
			assert(parent->keys[i] == parent->children[i + 1]->keys[0]);

			assert(child->count > 0);
			assert(parent->children[i + 1]->count > 0);

			// Check leaf linkage
			assert(child->next_leaf == parent->children[i + 1].get());
			assert(parent->children[i + 1]->prev_leaf == child);
		} else {
			// Internal split: promote middle key; distribute keys/children
			// Left keeps keys [0..mid-1] and children [0..mid]
			// Right gets keys [mid+1..count-1] and children [mid+1..count]
			const Key promote = child->keys[mid];

			// Move keys to right sibling
			const size_t right_keys = child->count - mid - 1;
			for (size_t j = 0; j < right_keys; ++j) {
				new_node->keys[j] = std::move(child->keys[mid + 1 + j]);
			}
			// Move children to right sibling
			for (size_t j = 0; j < right_keys + 1; ++j) {
				new_node->children[j] = std::move(child->children[mid + 1 + j]);
			}

			new_node->count = right_keys;
			child->count    = mid;

			// Shift parent keys/children to make room for promote at i
			for (size_t j = parent->count; j > i; --j) {
				parent->keys[j] = std::move(parent->keys[j - 1]);
			}
			for (size_t j = parent->count + 1; j > i + 1; --j) {
				parent->children[j] = std::move(parent->children[j - 1]);
			}

			parent->keys[i] = promote;
			parent->children[i + 1] = std::move(new_node);
			++parent->count;

			// Sanity checks for internal split
			assert(parent->children[i] != nullptr);
			assert(parent->children[i + 1] != nullptr);
			assert(parent->keys[i] == promote);

			assert(child->count == mid);
			assert(parent->children[i + 1]->count > 0);

		}
	}

	// Insert (promote_key, right_child) into internal parent at position child_idx+?
	void insert_into_internal(Node *parent, size_t left_child_idx,
		const Key &promote_key, std::unique_ptr<Node> right_child) {
		assert(!parent->is_leaf);
		// Insert promote_key into parent->keys at position left_child_idx
		// Shift keys and children right from (left_child_idx+1)
		for (size_t j = parent->count; j > left_child_idx; --j) {
			parent->keys[j] = std::move(parent->keys[j - 1]);
			parent->children[j + 1] = std::move(parent->children[j]);
		}
		parent->keys[left_child_idx] = promote_key;
		parent->children[left_child_idx + 1] = std::move(right_child);
		++parent->count;
	}

	void check_invariants(Node *node) {
		if (node == nullptr) return;
		if (node->is_leaf) {
			assert(node->count <= max_keys);
			if (node->next_leaf) assert(node->next_leaf->prev_leaf == node);
		} else {
			assert(node->count <= max_keys);
			for (size_t j = 0; j <= node->count; ++j) {
				assert(node->children[j] != nullptr);
				check_invariants(node->children[j].get());
			}
		}
	}

};

#endif /* BPLUSTREE_TYPE_HPP */
