/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bplustree_type.hpp BPlusTree container implementation. */

#ifndef BPLUSTREE_TYPE_HPP
#define BPLUSTREE_TYPE_HPP

template <typename Tkey, typename Tvalue, size_t B = 64>
class BPlusTree;

template <typename Tkey, typename Tvalue, size_t B>
struct BPlusNode {
	bool is_leaf;
	size_t count; // number of keys currently stored

	std::array<Tkey, B> keys;
	/* For internal nodes: child pointers */
	std::array<std::unique_ptr<BPlusNode>, B + 1> children;

	/* For leaf nodes: values + linked list */
	std::array<Tvalue, B> values;
	BPlusNode *next_leaf = nullptr;
	BPlusNode *prev_leaf = nullptr;
	BPlusNode *parent = nullptr;

	BPlusNode(bool leaf = true) : is_leaf(leaf), count(0), parent(nullptr)
	{
	}
};

template <typename Tkey, typename Tvalue, size_t B>
class BPlusTree {
	using Node = BPlusNode<Tkey, Tvalue, B>;
	std::unique_ptr<Node> root;

public:
	BPlusTree() : root(std::make_unique<Node>())
	{
	}
	BPlusTree(BPlusTree &&) = default;
	BPlusTree &operator=(BPlusTree &&) = default;

	/**
	 * Iterator types
	 */
	struct iterator {
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = std::pair<const Tkey, Tvalue>;
		using difference_type = std::ptrdiff_t;
		using pointer = void;
		using reference = std::pair<const Tkey &, Tvalue &>;

		Node *leaf_ = nullptr;
		size_t index_ = 0;
		const BPlusTree *owner_ = nullptr;

		/* Dereference */
		reference operator*() const
		{
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);
			return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] };
		}

		/* Increment */
		iterator &operator++()
		{
			if (this->leaf_ == nullptr) {
				return *this;
			}
			++this->index_;
			if (this->index_ >= this->leaf_->count) {
				this->leaf_ = this->leaf_->next_leaf;
				this->index_ = 0;
			}
			return *this;
		}

		/* Decrement */
		iterator &operator--()
		{
			if (this->leaf_ == nullptr) {
				/* Special case: --end() should land on the last element */
				this->leaf_ = this->owner_->rightmost_leaf(); // helper to find last leaf
				if (this->leaf_ != nullptr) {
					this->index_ = this->leaf_->count - 1;
				} else {
					this->index_ = 0;
				}
				return *this;
			}

			if (this->index_ == 0) {
				/* Move to previous leaf */
				this->leaf_ = this->leaf_->prev_leaf;
				if (this->leaf_ != nullptr) {
					this->index_ = this->leaf_->count - 1;
				}
			} else {
				--this->index_;
			}
			return *this;
		}

		/* Equality */
		friend bool operator==(const iterator &a, const iterator &b)
		{
			return a.leaf_ == b.leaf_ && a.index_ == b.index_;
		}

		friend bool operator!=(const iterator &a, const iterator &b)
		{
			return !(a == b);
		}
	};

	struct const_iterator {
		using iterator_category = std::bidirectional_iterator_tag;
		using value_type = std::pair<const Tkey, const Tvalue>;
		using difference_type = std::ptrdiff_t;
		using pointer = void;
		using reference = std::pair<const Tkey &, const Tvalue &>;

		const Node *leaf_ = nullptr;
		size_t index_ = 0;
		const BPlusTree *owner_ = nullptr; // back-pointer to the tree

		friend bool operator==(const const_iterator &a, const const_iterator &b)
		{
			return a.leaf_ == b.leaf_ && a.index_ == b.index_;
		}

		friend bool operator!=(const const_iterator &a, const const_iterator &b)
		{
			return !(a == b);
		}
	};

	Node *leftmost_leaf() const
	{
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

	Node *rightmost_leaf() const
	{
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

	void insert(const Tkey &key, const Tvalue &value)
	{
		if (this->root == nullptr) {
			this->root = std::make_unique<Node>(true);
		}
		Node *leaf = this->find_leaf(key);
		size_t i = this->lower_bound(leaf->keys, leaf->count, key);

		/* Duplicate check */
		if (i < leaf->count && leaf->keys[i] == key) {
			/* Policy: overwrite */
			leaf->values[i] = value;
			return;
		}

		/* Shift right */
		for (size_t j = leaf->count; j > i; --j) {
			leaf->keys[j] = std::move(leaf->keys[j - 1]);
			leaf->values[j] = std::move(leaf->values[j - 1]);
		}
		leaf->keys[i] = key;
		leaf->values[i] = value;
		++leaf->count;

		if (leaf->count == B) {
			this->split_leaf(leaf);
		}
	}

	Node *find_leaf(const Tkey &key) const
	{
		Node *node = this->root.get();
		if (node == nullptr) {
			return nullptr; // empty tree
		}

		while (!node->is_leaf) {
			/* Find child index to descend into */
			size_t i = this->upper_bound(node->keys, node->count, key);
			node = node->children[i].get();
		}
		return node;
	}

	void split_leaf(Node *leaf)
	{
		size_t mid = leaf->count / 2;
		auto new_leaf = std::make_unique<Node>();

		/* Copy half */
		for (size_t j = mid; j < leaf->count; ++j) {
			new_leaf->keys[j - mid] = std::move(leaf->keys[j]);
			new_leaf->values[j - mid] = std::move(leaf->values[j]);
		}
		new_leaf->count = leaf->count - mid;
		leaf->count = mid;
		assert(new_leaf->count > 0);

		/* Link leaves */
		new_leaf->next_leaf = leaf->next_leaf;
		if (new_leaf->next_leaf != nullptr) {
			new_leaf->next_leaf->prev_leaf = new_leaf.get();
		}
		new_leaf->prev_leaf = leaf;
		leaf->next_leaf = new_leaf.get();

		/* Separator = first key of new_leaf */
		Tkey separator = new_leaf->keys[0];

		/* Insert separator into parent */
		this->insert_into_parent(leaf, separator, new_leaf.release());
	}

	void insert_into_parent(Node *left, const Tkey &separator, Node *right)
	{
		Node *parent = left->parent;

		if (parent == nullptr) {
			/* Promote old root (left) and new right into a fresh root */
			auto new_root = std::make_unique<Node>(false);

			/* Release old root ownership so we can reattach it under the new root */
			Node *old_root = this->root.release(); // must equal `left`
			assert(left == old_root);

			new_root->keys[0] = separator;
			new_root->children[0].reset(old_root);
			new_root->children[1].reset(right);
			new_root->count = 1;

			/* Null-out remaining child slots for safety */
			for (size_t j = 2; j <= B; ++j) {
				new_root->children[j].reset();
			}

			/* Wire parents */
			old_root->parent = new_root.get();
			right->parent = new_root.get();

			/* Install the new root */
			this->root = std::move(new_root);
			return;
		}

		/* Find index of left in parent */
		size_t i = 0;
		while (i <= parent->count && parent->children[i].get() != left) {
			++i;
		}

		/* Shift keys/children right */
		for (size_t j = parent->count; j > i; --j) {
			parent->keys[j] = std::move(parent->keys[j - 1]);
		}
		for (size_t j = parent->count + 1; j > i + 1; --j) {
			parent->children[j] = std::move(parent->children[j - 1]);
			if (parent->children[j] != nullptr) {
				parent->children[j]->parent = parent;
			}
		}

		/* Insert separator and right child */
		parent->keys[i] = separator;
		parent->children[i + 1].reset(right);
		right->parent = parent;
		++parent->count;

		/* Check overflow */
		if (parent->count == B) {
			this->split_internal(parent);
		}
	}

	void split_internal(Node *node)
	{
		size_t old_count = node->count;
		size_t mid = old_count / 2;

		assert(mid < old_count); // safe to access node->keys[mid]

		/* New right internal node */
		auto new_node = std::make_unique<Node>(false);

		/* Separator key to promote */
		Tkey separator = node->keys[mid];

		/* Copy keys[mid + 1..end] into new_node */
		for (size_t j = mid + 1; j < node->count; ++j) {
			new_node->keys[j - (mid + 1)] = std::move(node->keys[j]);
		}
		new_node->count = node->count - mid - 1;

		/* Copy children[mid + 1..end] into new_node */
		for (size_t j = mid + 1; j <= node->count; ++j) {
			new_node->children[j - (mid + 1)] = std::move(node->children[j]);
			if (new_node->children[j - (mid + 1)] != nullptr) {
				new_node->children[j - (mid + 1)]->parent = new_node.get();
			}
		}

		/* Left node keeps first mid keys and mid + 1 children */
		node->count = mid;
		for (size_t j = mid + 1; j <= B; ++j) {
			node->children[j].reset();
		}

		assert(node->count == mid);
		assert(new_node->count > 0);

		/* Insert separator into parent */
		this->insert_into_parent(node, separator, new_node.release());
	}

	/**
	 * Update the separator key at sep_idx in parent.
	 * For leaf children, the separator equals the min key of the right child.
	 */
	void update_separator(Node *parent, size_t sep_idx)
	{
		/* parent->keys[sep_idx] separates children[sep_idx] | children[sep_idx + 1] */
		Node *right = parent->children[sep_idx + 1].get();
		if (right->is_leaf) {
			parent->keys[sep_idx] = right->keys[0];
		} else {
			/* For internal nodes, separator stays as stored; no recomputation here.
			 * If you use redistribution, you will explicitly set parent->keys[sep_idx]. */
		}
	}

	/**
	 * Borrow the first key from right sibling into leaf at the end.
	 * Update the parent separator to the new right.min.
	 */
	void borrow_from_right_leaf(Node *parent, size_t child_idx)
	{
		Node *leaf  = parent->children[child_idx].get();
		Node *right = parent->children[child_idx + 1].get();

		/* Preconditions: right exists and right->count > (B + 1) / 2
		 * Move right[0] -> leaf[end] */
		leaf->keys[leaf->count] = std::move(right->keys[0]);
		leaf->values[leaf->count] = std::move(right->values[0]);
		++leaf->count;

		/* Shift right left */
		for (size_t j = 0; j + 1 < right->count; ++j) {
			right->keys[j] = std::move(right->keys[j + 1]);
			right->values[j] = std::move(right->values[j + 1]);
		}
		--right->count;

		/* Update linkage invariant for leaves (unchanged pointers)
		 * Update parent separator (leaf | right) */
		this->update_separator(parent, child_idx);
	}

	/**
	 * Borrow the last key from left sibling into leaf at the front.
	 * Update the parent separator to the new leaf.min.
	 */
	void borrow_from_left_leaf(Node *parent, size_t child_idx)
	{
		Node *leaf = parent->children[child_idx].get();
		Node *left = parent->children[child_idx - 1].get();

		/* Preconditions: left exists and left->count > (B + 1) / 2
		 * Shift leaf right to make room at index 0 */
		for (size_t j = leaf->count; j > 0; --j) {
			leaf->keys[j] = std::move(leaf->keys[j - 1]);
			leaf->values[j] = std::move(leaf->values[j - 1]);
		}

		/* Move left[last] -> leaf[0] */
		leaf->keys[0] = std::move(left->keys[left->count - 1]);
		leaf->values[0] = std::move(left->values[left->count - 1]);
		++leaf->count;
		--left->count;

		/* Update parent separator (left | leaf) becomes leaf.min */
		parent->keys[child_idx - 1] = leaf->keys[0];
	}

	/**
	 * Merge leaf at i + 1 into leaf at i, keep the left leaf.
	 * Preconditions: parent->children[i] and parent->children[i + 1] exist.
	 */
	void merge_leaf_keep_left(Node *parent, size_t i)
	{
		Node *left  = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		/* Append right’s keys/values to left */
		for (size_t j = 0; j < right->count; ++j) {
			left->keys[left->count + j] = std::move(right->keys[j]);
			left->values[left->count + j] = std::move(right->values[j]);
		}
		left->count += right->count;

		/* Rewire leaf links to bypass right */
		left->next_leaf = right->next_leaf;
		if (left->next_leaf != nullptr) {
			left->next_leaf->prev_leaf = left;
		}

		/* Remove separator at i and child i + 1 from parent */
		this->remove_separator_and_child_right(parent, i);
		/* right gets deleted by unique_ptr when parent child slot is shifted away.
		 * Note: left survives; any iterators pointing into right must be redirected. */
	}

	/**
	 * Remove separator at sep_idx and the RIGHT child of that separator (child at sep_idx + 1).
	 */
	void remove_separator_and_child_right(Node *parent, size_t sep_idx)
	 {
		/* Remove key at sep_idx */
		for (size_t k = sep_idx; k + 1 < parent->count; ++k) {
			parent->keys[k] = std::move(parent->keys[k + 1]);
		}
		/* Remove child at sep_idx + 1 */
		for (size_t c = sep_idx + 1; c + 1 <= parent->count; ++c) {
			parent->children[c] = std::move(parent->children[c + 1]);
			if (parent->children[c]) parent->children[c]->parent = parent;
		}
		--parent->count;
//		/* Optional: null trailing child for cleanliness */
//		parent->children[parent->count + 1].reset();
	}

	void fix_underflow(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();

		if (child->is_leaf) {
			const size_t min_leaf = (B + 1) / 2;

			/* Try borrow from right */
			if (i + 1 <= parent->count) {
				Node *right = parent->children[i + 1].get();
				if (right != nullptr && right->count > min_leaf) {
					this->borrow_from_right_leaf(parent, i);
					return;
				}
			}
			/* Try borrow from left */
			if (i > 0) {
				Node *left = parent->children[i - 1].get();
				if (left != nullptr && left->count > min_leaf) {
					this->borrow_from_left_leaf(parent, i);
					return;
				}
			}

			/* Merge: always merge with right sibling if it exists,
			 * otherwise merge into left sibling. */
			if (i + 1 <= parent->count) {
				this->merge_leaf_keep_left(parent, i); // new helper
			} else {
				/* If no right sibling, merge current leaf into its left sibling */
				this->merge_leaf_keep_left(parent, i - 1);
			}

			/* After merge, parent may underflow (internal node) */
			this->fix_internal_underflow_cascade(parent);
		} else {
			this->fix_underflow_internal_child(parent, i);
		}
	}

	/**
	 * Rotate from the right sibling: move parent.keys[i] down into child,
	 * move sibling.keys[0] up into parent, and move sibling.children[0] into child.
	 */
	void borrow_from_right_internal(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		/* Move parent key down into child at end */
		child->keys[child->count] = std::move(parent->keys[i]);

		/* Move right's first child to child as new rightmost child */
		child->children[child->count + 1] = std::move(right->children[0]);
		if (child->children[child->count + 1] != nullptr) {
			child->children[child->count + 1]->parent = child;
		}
		++child->count;

		/* Move right's first key up into parent */
		parent->keys[i] = std::move(right->keys[0]);

		/* Shift right’s keys and children left */
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

	/**
	 * Rotate from the left sibling: move parent.keys[i - 1] down into child at front,
	 * move left’s last key up into parent, and move left’s last child to child as new leftmost child.
	 */
	void borrow_from_left_internal(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();
		Node *left = parent->children[i - 1].get();

		/* Shift child's keys and children right to make room at front */
		for (size_t k = child->count; k > 0; --k) {
			child->keys[k] = std::move(child->keys[k - 1]);
		}
		for (size_t c = child->count + 1; c > 0; --c) {
			child->children[c] = std::move(child->children[c - 1]);
			if (child->children[c] != nullptr) {
				child->children[c]->parent = child;
			}
		}

		/* Move parent key down into child[0] */
		child->keys[0] = std::move(parent->keys[i - 1]);

		/* Move left’s last child to child[0] */
		child->children[0] = std::move(left->children[left->count]);
		if (child->children[0] != nullptr) {
			child->children[0]->parent = child;
		}
		++child->count;

		/* Move left’s last key up into parent */
		parent->keys[i - 1] = std::move(left->keys[left->count - 1]);

		/* Pop left’s last key (and child already moved) */
		--left->count;
	}

	/**
	 * Merge child at i with right sibling at i + 1 using parent.keys[i] as middle key.
	 * All keys and children are combined into child; right sibling pointer and separator are removed from parent.
	 */
	void merge_internal(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		/* Move parent key down */
		child->keys[child->count] = std::move(parent->keys[i]);

		/* Copy right’s keys and children */
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

		/* Remove separator and right child from parent */
		this->remove_separator_and_child_right(parent, i);
	}

	/**
	 * Fix underflow when parent’s child at i is an internal node.
	 */
	void fix_underflow_internal_child(Node *parent, size_t i)
	{
		const size_t min_internal = (B + 1) / 2;

		/* Try borrow from right */
		if (i + 1 <= parent->count) {
			Node *right = parent->children[i + 1].get();
			if (right != nullptr && right->count > min_internal) {
				this->borrow_from_right_internal(parent, i);
				return;
			}
		}

		/* Try borrow from left */
		if (i > 0) {
			Node *left = parent->children[i - 1].get();
			if (left != nullptr && left->count > min_internal) {
				this->borrow_from_left_internal(parent, i);
				return;
			}
		}

		/* Merge */
		if (i + 1 <= parent->count) {
			this->merge_internal(parent, i);
		} else {
			/* Merge with left sibling at i - 1 */
			this->merge_internal(parent, i - 1);
		}

		/* Cascade if parent underflows */
		this->fix_internal_underflow_cascade(parent);
	}

	/**
	 * If an internal node underflows, borrow/merge upward until root is handled.
	 * Root special case: if root becomes empty and has one child, promote the child.
	 */
	void fix_internal_underflow_cascade(Node *node)
	{
		/* Stop at root */
		if (node == this->root.get()) {
			/* If root has no keys and one child, shrink height */
			if (!node->is_leaf && node->count == 0) {
				this->root = std::move(node->children[0]);
				if (this->root != nullptr) {
					this->root->parent = nullptr;
				}
			}
			return;
		}

		Node *parent = node->parent;
		/* Find node’s index in parent */
		size_t i = 0;
		while (i <= parent->count && parent->children[i].get() != node) {
			++i;
		}

		const size_t min_internal = (B + 1) / 2;
		if (node->count < min_internal) {
			/* Reuse the same logic as fix_underflow_internal_child */
			this->fix_underflow_internal_child(parent, i);
		}
	}

	/**
	 * Return iterator to first element
	 */
	iterator begin()
	{ 
		Node *first = this->leftmost_leaf();
		if (first == nullptr || first->count == 0) {
			return this->end();
		}
		return iterator(first, 0, this);
	}

	/**
	 * Return iterator to "one past the last element"
	 */
	iterator end()
	{
		return iterator(nullptr, 0, this); // sentinel
	}

	const_iterator end() const
	{
		return const_iterator(nullptr, 0, this); // sentinel
	}

	const_iterator cend() const
	{
		return this->end();
	}

	/**
	 * Deep copy assignment
	 */
	BPlusTree &operator=(const BPlusTree &other)
	{
		if (this != &other) {
			this->root = this->clone_node(other.root.get());
			this->repair_leaf_links();
		}
		return *this;
	}

	/**
	 * Helper: recursively clone a node
	 */
	std::unique_ptr<Node> clone_node(const Node *src)
	{
		if (src == nullptr) {
			return nullptr;
		}
		auto dst = std::make_unique<Node>(src->is_leaf);
		dst->count = src->count;
		dst->keys = src->keys;

		if (src->is_leaf) {
			dst->values = src->values;
			/* Leaf links (next/prev) are fixed later in a second pass */
		} else {
			for (size_t i = 0; i <= src->count; ++i) {
				dst->children[i] = this->clone_node(src->children[i].get());
			}
		}
		return dst;
	}

	void collect_leaves(Node *node, std::vector<Node *> &leaves)
	{
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

	void repair_leaf_links()
	{
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

	bool contains(const Tkey &key) const
	{
		return this->find(key) != this->end();
	}

	iterator find(const Tkey &key)
	{
		Node *node = this->root.get();
		while (node != nullptr && !node->is_leaf) {
			size_t i = this->upper_bound(node->keys, node->count, key);
			assert(i <= node->count); // children size is count + 1
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

	const_iterator find(const Tkey &key) const
	{
		const Node *node = this->root.get();
		while (node != nullptr && !node->is_leaf) {
			size_t i = this->upper_bound(node->keys, node->count, key);
			assert(i <= node->count); // children size is count + 1
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

	void swap(BPlusTree &other) noexcept
	{
		this->root.swap(other.root);
	}

	void clear() noexcept
	{
		/* Reset to a fresh empty leaf node */
		this->root = std::make_unique<Node>(); 
	}

	bool empty() const noexcept
	{
		return this->root == nullptr || this->root->count == 0;
	}

	size_t size() const noexcept
	{
		return this->count_recursive(this->root.get());
	}

	std::pair<iterator, bool> try_emplace(const Tkey &key, const Tvalue &value)
	{
		/* First, search for key */
		auto it = this->find(key);
		if (it != this->end()) {
			/* Already exists: return iterator and false */
			return { it, false };
		}

		/* Otherwise, insert new key/value */
		this->insert(key, value);
		it = this->find(key); // find again to get iterator to new element
		return { it, true };
	}

	iterator erase(iterator pos)
	{
		if (pos == this->end()) {
			return this->end();
		}

		Node *leaf = pos.leaf_;
		size_t i   = pos.index_;

		/* Erase locally */
		for (size_t j = i; j + 1 < leaf->count; ++j) {
			leaf->keys[j] = std::move(leaf->keys[j + 1]);
			leaf->values[j] = std::move(leaf->values[j + 1]);
		}
		--leaf->count;

		/* Remember the successor key (if any) before fix-up */
		Tkey succ_key;
		bool has_succ = false;
		if (i < leaf->count) {
			succ_key = leaf->keys[i];
			has_succ = true;
		} else if (leaf->next_leaf != nullptr && leaf->next_leaf->count > 0) {
			succ_key = leaf->next_leaf->keys[0];
			has_succ = true;
		}

		/* Fix underflow */
		if (leaf->parent != nullptr && leaf->count < (B + 1) / 2) {
			Node *parent = leaf->parent;
			size_t ci = 0;
			while (ci <= parent->count && parent->children[ci].get() != leaf) {
				++ci;
			}
			if (ci <= parent->count) {
				this->fix_underflow(parent, ci);
			}
		}

		/* If no successor, we’re at end */
		if (!has_succ) {
			return this->end();
		}

		/* Re-find successor leaf after potential merges */
		Node *succ_leaf = this->find_leaf(succ_key);
		size_t succ_idx = this->lower_bound(succ_leaf->keys, succ_leaf->count, succ_key);
		return iterator(succ_leaf, succ_idx, this);
	}



private:
	size_t count_recursive(const Node *node) const
	{
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

	/**
	 * Binary search over keys[0..count), returning first index i where keys[i] >= key.
	 */
	static size_t lower_bound(const std::array<Tkey, B> &keys, size_t count, const Tkey &key)
	{
		size_t lo = 0;
		size_t hi = count;
		/* Invariant: [lo, hi) is the search range. */
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

	/**
	 * Binary search over keys[0..count), returning first index i where keys[i] > key.
	 */
	static size_t upper_bound(const std::array<Tkey, B> &keys, size_t count, const Tkey &key)
	{
		size_t lo = 0;
		size_t hi = count;
		/* Invariant: [lo, hi) is the search range. */
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

};

#endif /* BPLUSTREE_TYPE_HPP */
