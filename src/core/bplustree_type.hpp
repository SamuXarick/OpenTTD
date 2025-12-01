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
	BPlusNode *parent = nullptr;

	BPlusNode(bool leaf = true) : is_leaf(leaf), count(0), parent(nullptr) {}
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
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);
			return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] };
		}

		//struct Proxy {
		//	const Key *k;
		//	Value *v;

		//	const Key &first() const {
		//		return *k;
		//	}

		//	Value &second() const {
		//		return *v;
		//	}
		//};

		//Proxy operator->() const {
		//	return Proxy{ &this->leaf_->keys[this->index_], &this->leaf_->values[this->index_] };
		//}

		// Increment
		iterator &operator++() {
//			std::cout << "Leaf=" << leaf_ << " index=" << index_ << " count=" << leaf_->count << "\n";

			if (this->leaf_ == nullptr) {
				return *this;
			}
			if (++this->index_ >= this->leaf_->count) {
				this->leaf_ = this->leaf_->next_leaf;
				this->index_ = 0;
			}
			return *this;
		}

		iterator operator++(int) {
			auto tmp = *this;
			++(*this);
			return tmp;
		}

		// Decrement
		iterator &operator--() {
			if (this->leaf_ == nullptr) {
				// Special case: --end() should land on the last element
				this->leaf_ = this->owner_->rightmost_leaf();           // helper to find last leaf
				this->index_ = this->leaf_ != nullptr ? this->leaf_->count - 1 : 0;
				std::cout << "Leaf=" << leaf_ << " index=" << index_ << " count=" << leaf_->count << "\n";
				return *this;
			}

			if (this->index_ == 0) {
				// Move to previous leaf
				this->leaf_ = this->leaf_->prev_leaf;
				if (this->leaf_ != nullptr) this->index_ = this->leaf_->count - 1;
			} else {
				--this->index_;
			}
			std::cout << "Leaf=" << leaf_ << " index=" << index_ << " count=" << leaf_->count << "\n";
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

		//reference operator*() const {
		//	assert(this->leaf_ != nullptr);
		//	assert(this->index_ < this->leaf_->count);
		//	return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] };
		//}

		//struct Proxy {
		//	const Key *k;
		//	const Value *v;

		//	const Key &first() const {
		//		return *k;
		//	}

		//	const Value &second() const {
		//		return *v;
		//	}
		//};

		//Proxy operator->() const {
		//	return Proxy{ &this->leaf_->keys[this->index_], &this->leaf_->values[this->index_] };
		//}

		//const_iterator &operator++() {
		//	if (this->leaf_ == nullptr) {
		//		return *this;
		//	}
		//	if (++this->index_ >= this->leaf_->count) {
		//		this->leaf_ = this->leaf_->next_leaf;
		//		this->index_ = 0;
		//	}
		//	return *this;
		//}

		//const_iterator operator++(int) {
		//	auto tmp = *this;
		//	++(*this);
		//	return tmp;
		//}

		//// Decrement
		//const_iterator &operator--() {
		//	if (this->leaf_ == nullptr) {
		//		// Special case: --end() should land on the last element
		//		this->leaf_ = this->owner_->rightmost_leaf();           // helper to find last leaf
		//		this->index_ = this->leaf_ != nullptr ? this->leaf_->count - 1 : 0;
		//		return *this;
		//	}

		//	if (this->index_ == 0) {
		//		// Move to previous leaf
		//		this->leaf_ = this->leaf_->prev_leaf;
		//		if (this->leaf_ != nullptr) this->index_ = this->leaf_->count - 1;
		//	} else {
		//		--this->index_;
		//	}
		//	return *this;
		//}

		//const_iterator operator--(int) {
		//	auto tmp = *this;
		//	--(*this);
		//	return tmp;
		//}
		friend bool operator==(const const_iterator &a, const const_iterator &b) {
			return a.leaf_ == b.leaf_ && a.index_ == b.index_;
		}

		friend bool operator!=(const const_iterator &a, const const_iterator &b) {
			return !(a == b);
		}
	};

	Node *leftmost_leaf() const {
		Node *node = this->root.get();
		if (node == nullptr) {
			return nullptr;
		}
		while (node != nullptr && !node->is_leaf) {
			if (node->children[0] == nullptr) {
				return nullptr; // guard
			}
			node = node->children[0].get();
		}
		return node;
	}

	Node *rightmost_leaf() const {
		Node *node = this->root.get();
		if (node == nullptr) {
			return nullptr;
		}
		while (node != nullptr && !node->is_leaf) {
			if (node->children[node->count] == nullptr) {
				return nullptr; // guard
			}
			node = node->children[node->count].get(); // rightmost child
		}
		return node;
	}

	void insert(const Key &key, const Value &value) {
		if (this->root == nullptr) {
			this->root = std::make_unique<Node>(true);
		}
		Node *leaf = this->find_leaf(key);
		size_t i = this->lower_bound(leaf->keys, leaf->count, key);

		// Duplicate check
		if (i < leaf->count && leaf->keys[i] == key) {
			// Policy: overwrite
			leaf->values[i] = value;
			return;
		}

		// Shift right
		for (size_t j = leaf->count; j > i; --j) {
			leaf->keys[j]   = std::move(leaf->keys[j - 1]);
			leaf->values[j] = std::move(leaf->values[j - 1]);
		}
		leaf->keys[i]   = key;
		leaf->values[i] = value;
		++leaf->count;

		if (leaf->count == B) {
			this->split_leaf(leaf);
		}
	}

	Node *find_leaf(const Key &key) const {
		Node *node = this->root.get();
		if (node == nullptr) {
			return nullptr; // empty tree
		}

		while (!node->is_leaf) {
			// Find child index to descend into
			size_t i = this->upper_bound(node->keys, node->count, key);
			node = node->children[i].get();
		}
		return node;
	}

	void split_leaf(Node *leaf) {
		size_t mid = leaf->count / 2;
		auto new_leaf = std::make_unique<Node>(true);

		// Copy half
		for (size_t j = mid; j < leaf->count; ++j) {
			new_leaf->keys[j - mid]   = std::move(leaf->keys[j]);
			new_leaf->values[j - mid] = std::move(leaf->values[j]);
		}
		new_leaf->count = leaf->count - mid;
		leaf->count = mid;
		assert(new_leaf->count > 0 && "split_leaf produced empty right leaf");

		// Link leaves
		new_leaf->next_leaf = leaf->next_leaf;
		if (new_leaf->next_leaf != nullptr) {
			new_leaf->next_leaf->prev_leaf = new_leaf.get();
		}
		new_leaf->prev_leaf = leaf;
		leaf->next_leaf = new_leaf.get();

		// Separator = first key of new_leaf
		Key separator = new_leaf->keys[0];

		// Insert separator into parent
		this->insert_into_parent(leaf, separator, new_leaf.release());
	}

	void insert_into_parent(Node *left, const Key &separator, Node *right) {
		Node *parent = left->parent;

		if (parent == nullptr) {
			// Promote old root (left) and new right into a fresh root
			auto new_root = std::make_unique<Node>(false);

			// Release old root ownership so we can reattach it under the new root
			Node *old_root = this->root.release(); // must equal `left`
			assert(left == old_root && "left must be the current root when parent==nullptr");

			new_root->keys[0] = separator;
			new_root->children[0].reset(old_root);
			new_root->children[1].reset(right);
			new_root->count = 1;

			// Null-out remaining child slots for safety
			for (size_t j = 2; j <= B; ++j) {
				new_root->children[j].reset();
			}

			// Wire parents
			old_root->parent = new_root.get();
			right->parent    = new_root.get();

			// Install the new root
			this->root = std::move(new_root);
			return;
		}

		// Find index of left in parent
		size_t i = 0;
		while (i <= parent->count && parent->children[i].get() != left) {
			++i;
		}

		// Shift keys/children right
		for (size_t j = parent->count; j > i; --j) {
			parent->keys[j] = std::move(parent->keys[j - 1]);
		}
		for (size_t j = parent->count + 1; j > i + 1; --j) {
			parent->children[j] = std::move(parent->children[j - 1]);
			if (parent->children[j] != nullptr) {
				parent->children[j]->parent = parent;
			}
		}

		// Insert separator and right child
		parent->keys[i] = separator;
		parent->children[i + 1].reset(right);
		right->parent = parent;
		++parent->count;

		// Check overflow
		if (parent->count == B) {
			this->split_internal(parent);
		}
	}

	void split_internal(Node *node) {
		size_t old_count = node->count;
		size_t mid = old_count / 2;

		assert(mid < old_count); // safe to access node->keys[mid]

		// New right internal node
		auto new_node = std::make_unique<Node>(false);

		// Separator key to promote
		Key separator = node->keys[mid];

		// Copy keys[mid+1..end] into new_node
		for (size_t j = mid + 1; j < node->count; ++j) {
			new_node->keys[j - (mid + 1)] = std::move(node->keys[j]);
		}
		new_node->count = node->count - mid - 1;

		// Copy children[mid+1..end] into new_node
		for (size_t j = mid + 1; j <= node->count; ++j) {
			new_node->children[j - (mid + 1)] = std::move(node->children[j]);
			if (new_node->children[j - (mid + 1)] != nullptr) {
				new_node->children[j - (mid + 1)]->parent = new_node.get();
			}
		}

		// Left node keeps first mid keys and mid+1 children
		node->count = mid;
		for (size_t j = mid + 1; j <= B; ++j) {
			node->children[j].reset();
		}

		assert(node->count == mid);
		assert(new_node->count > 0);

		// Insert separator into parent
		this->insert_into_parent(node, separator, new_node.release());
	}

	// Minimum keys for leaf/internal nodes (B is max keys in a node)
	size_t min_keys([[maybe_unused]] bool is_leaf) const {
		// Classic B+ tree: min is ceil(B/2)
		return (B + 1) / 2;
	}

	// Update the separator key at sep_idx in parent.
	// For leaf children, the separator equals the min key of the right child.
	void update_separator(Node *parent, size_t sep_idx) {
		// parent->keys[sep_idx] separates children[sep_idx] | children[sep_idx+1]
		Node *right = parent->children[sep_idx + 1].get();
		if (right->is_leaf) {
			parent->keys[sep_idx] = right->keys[0];
		} else {
			// For internal nodes, separator stays as stored; no recomputation here.
			// If you use redistribution, you will explicitly set parent->keys[sep_idx].
		}
	}

	// Borrow the first key from right sibling into leaf at the end.
	// Update the parent separator to the new right.min.
	void borrow_from_right_leaf(Node *parent, size_t child_idx) {
		Node *leaf  = parent->children[child_idx].get();
		Node *right = parent->children[child_idx + 1].get();

		// Preconditions: right exists and right->count > min_keys(true)
		// Move right[0] -> leaf[end]
		leaf->keys[leaf->count]   = std::move(right->keys[0]);
		leaf->values[leaf->count] = std::move(right->values[0]);
		++leaf->count;

		// Shift right left
		for (size_t j = 0; j + 1 < right->count; ++j) {
			right->keys[j]   = std::move(right->keys[j + 1]);
			right->values[j] = std::move(right->values[j + 1]);
		}
		--right->count;

		// Update linkage invariant for leaves (unchanged pointers)
		// Update parent separator (leaf | right)
		this->update_separator(parent, child_idx);
	}

	// Borrow the last key from left sibling into leaf at the front.
	// Update the parent separator to the new leaf.min.
	void borrow_from_left_leaf(Node *parent, size_t child_idx) {
		Node *leaf = parent->children[child_idx].get();
		Node *left = parent->children[child_idx - 1].get();

		// Preconditions: left exists and left->count > min_keys(true)
		// Shift leaf right to make room at index 0
		for (size_t j = leaf->count; j > 0; --j) {
			leaf->keys[j]   = std::move(leaf->keys[j - 1]);
			leaf->values[j] = std::move(leaf->values[j - 1]);
		}

		// Move left[last] -> leaf[0]
		leaf->keys[0]   = std::move(left->keys[left->count - 1]);
		leaf->values[0] = std::move(left->values[left->count - 1]);
		++leaf->count;
		--left->count;

		// Update parent separator (left | leaf) becomes leaf.min
		parent->keys[child_idx - 1] = leaf->keys[0];
	}

	// Merge leaf child at child_idx into its right sibling.
	// Remove the separator from parent and unlink the merged leaf.
	void merge_leaf_with_right(Node *parent, size_t child_idx) {
		Node *leaf  = parent->children[child_idx].get();
		Node *right = parent->children[child_idx + 1].get();

		// Prepend leaf’s keys to right (shift right to make room)
		for (size_t j = right->count; j > 0; --j) {
			right->keys[j]   = std::move(right->keys[j - 1]);
			right->values[j] = std::move(right->values[j - 1]);
		}
		for (size_t j = 0; j < leaf->count; ++j) {
			right->keys[j]   = std::move(leaf->keys[j]);
			right->values[j] = std::move(leaf->values[j]);
		}
		right->count += leaf->count;

		// Unlink leaf from leaf list
		if (leaf->prev_leaf != nullptr) {
			leaf->prev_leaf->next_leaf = leaf->next_leaf;
		}
		if (leaf->next_leaf != nullptr) {
			leaf->next_leaf->prev_leaf = leaf->prev_leaf;
		}

		// Remove leaf child from parent: delete separator at child_idx and child pointer at child_idx
		this->remove_separator_and_child(parent, child_idx);

		// Delete the leaf node (unique_ptr in parent is already cleared by remove)
		// Underflow can cascade into parent; caller must check parent underflow.
	}

	// Merge leaf child at child_idx into its left sibling (child_idx - 1).
	void merge_leaf_with_left(Node *parent, size_t child_idx) {
		Node *leaf = parent->children[child_idx].get();
		Node *left = parent->children[child_idx - 1].get();

		// Append leaf to left
		for (size_t j = 0; j < leaf->count; ++j) {
			left->keys[left->count + j]   = std::move(leaf->keys[j]);
			left->values[left->count + j] = std::move(leaf->values[j]);
		}
		left->count += leaf->count;

		// Unlink leaf
		if (leaf->prev_leaf != nullptr) {
			leaf->prev_leaf->next_leaf = leaf->next_leaf;
		}
		if (leaf->next_leaf != nullptr) {
			leaf->next_leaf->prev_leaf = leaf->prev_leaf;
		}

		// Remove separator and child at child_idx - 1 (separator between left|leaf resides at child_idx - 1)
		this->remove_separator_and_child(parent, child_idx - 1);
	}

	// Remove separator at sep_idx and child pointer at sep_idx+1 (for merge into right),
	// or at sep_idx and child pointer at sep_idx (for merge into left) depending on call site.
	// We implement a general helper that shifts arrays appropriately.
	void remove_separator_and_child(Node *parent, size_t sep_idx) {
		// Remove key at sep_idx
		for (size_t k = sep_idx; k + 1 < parent->count; ++k) {
			parent->keys[k] = std::move(parent->keys[k + 1]);
		}
		// Remove child at sep_idx+1 (the right child of the separator)
		for (size_t c = sep_idx + 1; c + 1 <= parent->count; ++c) {
			parent->children[c] = std::move(parent->children[c + 1]);
			if (parent->children[c] != nullptr) {
				parent->children[c]->parent = parent;
			}
		}
		--parent->count;
	}

	// Fix underflow for a child at index i in parent.
	// If the child is a leaf, use the leaf borrow/merge rules.
	// Otherwise, dispatch to internal child fix-up (below).
	void fix_underflow(Node *parent, size_t i) {
		Node *child = parent->children[i].get();

		if (child->is_leaf) {
			const size_t min_leaf = this->min_keys(true);

			// Try borrow from right
			if (i + 1 <= parent->count) {
				Node *right = parent->children[i + 1].get();
				if (right != nullptr && right->count > min_leaf) {
					this->borrow_from_right_leaf(parent, i);
					return;
				}
			}
			// Try borrow from left
			if (i > 0) {
				Node *left = parent->children[i - 1].get();
				if (left != nullptr && left->count > min_leaf) {
					this->borrow_from_left_leaf(parent, i);
					return;
				}
			}
			// Merge: prefer merging into right if exists, else into left
			if (i + 1 <= parent->count) {
				this->merge_leaf_with_right(parent, i);
			} else {
				this->merge_leaf_with_left(parent, i);
			}

			// After merge, parent may underflow (internal node)
			this->fix_internal_underflow_cascade(parent);
		} else {
			this->fix_underflow_internal_child(parent, i);
		}
	}

	// Rotate from the right sibling: move parent.keys[i] down into child,
	// move sibling.keys[0] up into parent, and move sibling.children[0] into child.
	void borrow_from_right_internal(Node *parent, size_t i) {
		Node *child = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		// Move parent key down into child at end
		child->keys[child->count] = std::move(parent->keys[i]);

		// Move right's first child to child as new rightmost child
		child->children[child->count + 1] = std::move(right->children[0]);
		if (child->children[child->count + 1] != nullptr) {
			child->children[child->count + 1]->parent = child;
		}

		++child->count;

		// Move right's first key up into parent
		parent->keys[i] = std::move(right->keys[0]);

		// Shift right’s keys and children left
		for (size_t k = 0; k + 1 < right->count; ++k) {
			right->keys[k] = std::move(right->keys[k + 1]);
		}
		for (size_t c = 0; c + 1 <= right->count; ++c) {
			right->children[c] = std::move(right->children[c + 1]);
			if (right->children[c] != nullptr) {
				right->children[c]->parent = right;
			}
		}
		--right->count;
	}

	// Rotate from the left sibling: move parent.keys[i-1] down into child at front,
	// move left’s last key up into parent, and move left’s last child to child as new leftmost child.
	void borrow_from_left_internal(Node *parent, size_t i) {
		Node *child = parent->children[i].get();
		Node *left  = parent->children[i - 1].get();

		// Shift child's keys and children right to make room at front
		for (size_t k = child->count; k > 0; --k) {
			child->keys[k] = std::move(child->keys[k - 1]);
		}
		for (size_t c = child->count + 1; c > 0; --c) {
			child->children[c] = std::move(child->children[c - 1]);
			if (child->children[c] != nullptr) {
				child->children[c]->parent = child;
			}
		}

		// Move parent key down into child[0]
		child->keys[0] = std::move(parent->keys[i - 1]);

		// Move left’s last child to child[0]
		child->children[0] = std::move(left->children[left->count]);
		if (child->children[0] != nullptr) {
			child->children[0]->parent = child;
		}

		++child->count;

		// Move left’s last key up into parent
		parent->keys[i - 1] = std::move(left->keys[left->count - 1]);

		// Pop left’s last key (and child already moved)
		--left->count;
	}

	// Merge child at i with right sibling at i+1 using parent.keys[i] as middle key.
	// All keys and children are combined into child; right sibling pointer and separator are removed from parent.
	void merge_internal(Node *parent, size_t i) {
		Node *child = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		// Move parent key down
		child->keys[child->count] = std::move(parent->keys[i]);

		// Copy right’s keys and children
		for (size_t k = 0; k < right->count; ++k) {
			child->keys[child->count + 1 + k] = std::move(right->keys[k]);
		}
		for (size_t c = 0; c <= right->count; ++c) {
			child->children[child->count + 1 + c] = std::move(right->children[c]);
			if (child->children[child->count + 1 + c] != nullptr) {
				child->children[child->count + 1 + c]->parent = child;
			}
		}
		child->count += 1 + right->count;

		// Remove separator and right child from parent
		this->remove_separator_and_child(parent, i);
	}

	// Fix underflow when parent’s child at i is an internal node.
	void fix_underflow_internal_child(Node *parent, size_t i) {
//		Node *child = parent->children[i].get();
		const size_t min_internal = this->min_keys(false);

		// Try borrow from right
		if (i + 1 <= parent->count) {
			Node *right = parent->children[i + 1].get();
			if (right != nullptr && right->count > min_internal) {
				this->borrow_from_right_internal(parent, i);
				return;
			}
		}

		// Try borrow from left
		if (i > 0) {
			Node *left = parent->children[i - 1].get();
			if (left != nullptr && left->count > min_internal) {
				this->borrow_from_left_internal(parent, i);
				return;
			}
		}

		// Merge
		if (i + 1 <= parent->count) {
			this->merge_internal(parent, i);
		} else {
			// Merge with left sibling at i-1
			this->merge_internal(parent, i - 1);
		}

		// Cascade if parent underflows
		this->fix_internal_underflow_cascade(parent);
	}

	// If an internal node underflows, borrow/merge upward until root is handled.
	// Root special case: if root becomes empty and has one child, promote the child.
	void fix_internal_underflow_cascade(Node *node) {
		// Stop at root
		if (node == this->root.get()) {
			// If root has no keys and one child, shrink height
			if (!node->is_leaf && node->count == 0) {
				this->root = std::move(node->children[0]);
				if (this->root != nullptr) {
					this->root->parent = nullptr;
				}
			}
			return;
		}

		Node *parent = node->parent;
		// Find node’s index in parent
		size_t i = 0;
		while (i <= parent->count && parent->children[i].get() != node) {
			++i;
		}

		const size_t min_internal = this->min_keys(false);
		if (node->count >= min_internal) {
			return;
		}

		// Reuse the same logic as fix_underflow_internal_child
		this->fix_underflow_internal_child(parent, i);
	}

	// Return iterator to first element
	iterator begin() { 
		Node *first = this->leftmost_leaf();
		if (first == nullptr || first->count == 0) {
			return this->end();
		}
		return iterator(first, 0, this);
	}

	//const_iterator begin() const {
	//	const Node *first = this->leftmost_leaf();
	//	if (first == nullptr || first->count == 0) {
	//		return this->cend();
	//	}
	//	return const_iterator(first, 0, this);
	//}

	//const_iterator cbegin() const {
	//	return this->begin();
	//}

	// Return iterator to "one past the last element"
	iterator end() {
		return iterator(nullptr, 0, this); // sentinel
	}

	const_iterator end() const {
		return const_iterator(nullptr, 0, this); // sentinel
	}

	const_iterator cend() const {
		return this->end();
	}

	//// Lower-bound as iterator (ordered seek).
	//iterator lower_bound(const Key &key) {
	//	Node *node = this->root.get();
	//	while (!node->is_leaf) {
	//		size_t i = this->upper_bound(node->keys, node->count, key);
	//		node = node->children[i].get();
	//	}
	//	size_t i = this->lower_bound(node->keys, node->count, key);
	//	if (i >= node->count) {
	//		// next leaf (or end) if key is greater than all in this leaf
	//		node = node->next_leaf;
	//		if (node == nullptr) {
	//			return this->end();
	//		}
	//		return iterator(node, 0, this);
	//	}
	//	return iterator(node, i, this);
	//}

	//// Deep copy constructor
	//BPlusTree(const BPlusTree &other) {
	//	this->root = this->clone_node(other.root.get());
	//	this->repair_leaf_links();
	//}

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
		if (src == nullptr) {
			return nullptr;
		}
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
		if (node == nullptr) {
			return;
		}
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
			if (prev != nullptr) {
				prev->next_leaf = leaf;
			}
			prev = leaf;
		}
		if (prev != nullptr) {
			prev->next_leaf = nullptr; // last leaf
		}
	}

	bool contains(const Key &key) const {
		return this->find(key) != this->end();
	}

	iterator find(const Key &key) {
		Node *node = this->root.get();
		while (node != nullptr && !node->is_leaf) {
			size_t i = this->upper_bound(node->keys, node->count, key);
			assert(i <= node->count);                // children size is count+1
			assert(node->children[i] != nullptr);
			node = node->children[i].get();
		}
		if (node == nullptr) {
			return this->end();
		}

		size_t i = this->lower_bound(node->keys, node->count, key);
		if (i < node->count && node->keys[i] == key) {
			return iterator(node, i, this);
		}
		return this->end();
	}

	const_iterator find(const Key &key) const {
		const Node *node = this->root.get();
		while (node != nullptr && !node->is_leaf) {
			size_t i = this->upper_bound(node->keys, node->count, key);
			assert(i <= node->count);                // children size is count+1
			assert(node->children[i] != nullptr);
			node = node->children[i].get();
		}
		if (node == nullptr) {
			return this->cend();
		}

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
			return { it, false };
		}

		// Otherwise, insert new key/value
		this->insert(key, value);
		it = this->find(key); // find again to get iterator to new element
		return { it, true };
	}

	iterator erase(iterator pos) {
		if (pos == this->end()) {
			return this->end();
		}

		Node *leaf = pos.leaf_;
		size_t i   = pos.index_;

		// Erase locally
		for (size_t j = i; j + 1 < leaf->count; ++j) {
			leaf->keys[j]   = std::move(leaf->keys[j + 1]);
			leaf->values[j] = std::move(leaf->values[j + 1]);
		}
		--leaf->count;

		// Capture successor before fix-up: typically {leaf, i} or next leaf
		Node *succ_leaf = nullptr;
		size_t succ_idx = 0;
		if (i < leaf->count) {
			succ_leaf = leaf;
			succ_idx  = i;
		} else if (leaf->next_leaf != nullptr) {
			succ_leaf = leaf->next_leaf;
			succ_idx  = 0;
		}

		// Fix underflow via parent
		if (leaf->parent != nullptr && leaf->count < this->min_keys(true)) {
			Node *parent = leaf->parent;
			size_t ci = 0;
			while (ci <= parent->count && parent->children[ci].get() != leaf) {
				++ci;
			}
			if (ci <= parent->count) {
				this->fix_underflow(parent, ci);
			}
		}

		// Map successor through potential merges:
		// If succ_leaf was merged away, redirect to the neighbor.
		if (succ_leaf == nullptr || succ_leaf->count == 0) {
			// If rightmost path collapsed, end()
			return this->end();
		}
		if (succ_idx >= succ_leaf->count) {
			// If index shifted due to borrowing/merging, clamp to valid range
			succ_idx = std::min(succ_idx, succ_leaf->count - 1);
		}
		return iterator(succ_leaf, succ_idx, this);
	}


private:
	size_t count_recursive(const Node *node) const {
		if (node == nullptr) {
			return 0;
		}
		if (node->is_leaf) {
			return node->count;
		}
		size_t total = 0;
		for (size_t i = 0; i <= node->count; ++i) {
			total += this->count_recursive(node->children[i].get());
		}
		return total;
	}

	//// Iteration: start from leftmost leaf
	//Node *begin_leaf() const {
	//	Node *node = this->root.get();
	//	while (!node->is_leaf) {
	//		node = node->children[0].get();
	//	}
	//	return node;
	//}

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

	// Binary search over keys[0..count), returning first index i where keys[i] > key.
	static size_t upper_bound(const std::array<Key, B> &keys, size_t count, const Key &key) {
		size_t lo = 0;
		size_t hi = count;
		// Invariant: [lo, hi) is the search range.
		while (lo < hi) {
			size_t mid = lo + ((hi - lo) >> 1);
			if (keys[mid] <= key) {
				lo = mid + 1;
			} else {
				hi = mid;
			}
		}
		return lo;
	}


	//// Insert into a node that is guaranteed non-full.
	//void insert_nonfull(Node *node, const Key &key, const Value &value) {
	//	if (node->is_leaf) {
	//		// Find position and insert key/value
	//		size_t i = this->lower_bound(node->keys, node->count, key);
	//		if (i < node->count && node->keys[i] == key) {
	//			// Overwrite value (B+ trees typically allow update on duplicate key)
	//			node->values[i] = value;
	//			return;
	//		}
	//		// Shift right to make room
	//		for (size_t j = node->count; j > i; --j) {
	//			node->keys[j] = std::move(node->keys[j - 1]);
	//			node->values[j] = std::move(node->values[j - 1]);
	//		}
	//		node->keys[i] = key;
	//		node->values[i] = value;
	//		++node->count;
	//	} else {
	//		// Internal: descend into the correct child
	//		size_t i = this->upper_bound(node->keys, node->count, key);
	//		Node *child = node->children[i].get();
	//		if (child->count == B) {
	//			// Child is full; split it and decide where to descend
	//			this->split_child(node, i);
	//			// After split, decide which child to descend based on promoted key
	//			if (key >= node->keys[i]) {
	//				child = node->children[i + 1].get();
	//			}
	//		}
	//		this->insert_nonfull(child, key, value);
	//	}
	//}

	//// Split parent->children[i] into two siblings and update parent
	//void split_child(Node *parent, size_t i) {
	//	Node *child = parent->children[i].get();
	//	assert(child != nullptr && "split_child: child must exist");
	//	assert(parent->count < max_keys && "split_child: parent must have space");
	//	assert(child->count == max_keys && "split_child: child must be full to split");

	//	auto new_node = std::make_unique<Node>(child->is_leaf);
	//	size_t mid = child->count / 2; // lower mid for even B

	//	if (child->is_leaf) {
	//		// Move keys/values [mid .. count-1] to new leaf
	//		const size_t right_count = child->count - mid;
	//		for (size_t j = 0; j < right_count; ++j) {
	//			new_node->keys[j]   = std::move(child->keys[mid + j]);
	//			new_node->values[j] = std::move(child->values[mid + j]);
	//		}
	//		new_node->count = right_count;
	//		child->count    = mid;

	//		// Wire leaf links
	//		new_node->next_leaf = child->next_leaf;
	//		if (child->next_leaf != nullptr) {
	//			child->next_leaf->prev_leaf = new_node.get();
	//		}
	//		child->next_leaf = new_node.get();
	//		new_node->prev_leaf = child;

	//		// Shift parent keys to make room at position i
	//		for (size_t j = parent->count; j > i; --j) {
	//			parent->keys[j] = std::move(parent->keys[j - 1]);
	//		}
	//		// Shift parent children to make room at position i+1
	//		for (size_t j = parent->count + 1; j > i + 1; --j) {
	//			parent->children[j] = std::move(parent->children[j - 1]);
	//		}

	//		// Separator is first key of right leaf (remains in the leaf)
	//		parent->keys[i] = new_node->keys[0];
	//		parent->children[i + 1] = std::move(new_node);
	//		++parent->count;

	//		// Sanity checks for leaf split
	//		assert(parent->children[i] != nullptr);
	//		assert(parent->children[i + 1] != nullptr);
	//		assert(parent->keys[i] == parent->children[i + 1]->keys[0]);

	//		assert(child->count > 0);
	//		assert(parent->children[i + 1]->count > 0);

	//		// Check leaf linkage
	//		assert(child->next_leaf == parent->children[i + 1].get());
	//		assert(parent->children[i + 1]->prev_leaf == child);
	//	} else {
	//		// Internal split: promote middle key; distribute keys/children
	//		// Left keeps keys [0..mid-1] and children [0..mid]
	//		// Right gets keys [mid+1..count-1] and children [mid+1..count]
	//		const Key promote = child->keys[mid];

	//		// Move keys to right sibling
	//		const size_t right_keys = child->count - mid - 1;
	//		for (size_t j = 0; j < right_keys; ++j) {
	//			new_node->keys[j] = std::move(child->keys[mid + 1 + j]);
	//		}
	//		// Move children to right sibling
	//		for (size_t j = 0; j < right_keys + 1; ++j) {
	//			new_node->children[j] = std::move(child->children[mid + 1 + j]);
	//		}

	//		new_node->count = right_keys;
	//		child->count    = mid;

	//		// Shift parent keys/children to make room for promote at i
	//		for (size_t j = parent->count; j > i; --j) {
	//			parent->keys[j] = std::move(parent->keys[j - 1]);
	//		}
	//		for (size_t j = parent->count + 1; j > i + 1; --j) {
	//			parent->children[j] = std::move(parent->children[j - 1]);
	//		}

	//		parent->keys[i] = promote;
	//		parent->children[i + 1] = std::move(new_node);
	//		++parent->count;

	//		// Sanity checks for internal split
	//		assert(parent->children[i] != nullptr);
	//		assert(parent->children[i + 1] != nullptr);
	//		assert(parent->keys[i] == promote);

	//		assert(child->count == mid);
	//		assert(parent->children[i + 1]->count > 0);

	//	}
	//}

	//// Insert (promote_key, right_child) into internal parent at position child_idx+?
	//void insert_into_internal(Node *parent, size_t left_child_idx, const Key &promote_key, std::unique_ptr<Node> right_child) {
	//	assert(!parent->is_leaf);
	//	// Insert promote_key into parent->keys at position left_child_idx
	//	// Shift keys and children right from (left_child_idx+1)
	//	for (size_t j = parent->count; j > left_child_idx; --j) {
	//		parent->keys[j] = std::move(parent->keys[j - 1]);
	//		parent->children[j + 1] = std::move(parent->children[j]);
	//	}
	//	parent->keys[left_child_idx] = promote_key;
	//	parent->children[left_child_idx + 1] = std::move(right_child);
	//	++parent->count;
	//}

	//void check_invariants(Node *node) {
	//	if (node == nullptr) {
	//		return;
	//	}
	//	if (node->is_leaf) {
	//		assert(node->count <= max_keys);
	//		if (node->next_leaf != nullptr) {
	//			assert(node->next_leaf->prev_leaf == node);
	//		}
	//	} else {
	//		assert(node->count <= max_keys);
	//		for (size_t j = 0; j <= node->count; ++j) {
	//			assert(node->children[j] != nullptr);
	//			this->check_invariants(node->children[j].get());
	//		}
	//	}
	//}

};

#endif /* BPLUSTREE_TYPE_HPP */
