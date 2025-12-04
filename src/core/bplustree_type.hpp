/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bplustree_type.hpp BPlusTree container implementation. */

#ifndef BPLUSTREE_TYPE_HPP
#define BPLUSTREE_TYPE_HPP

/** Enable it if you suspect b+ tree doesn't work well */
#define BPLUSTREE_CHECK 0

#if BPLUSTREE_CHECK
	/** Validate nodes after insert / erase. */
#	define VALIDATE_NODES() this->validate()
#else
	/** Don't check for consistency. */
#	define VALIDATE_NODES() ;
#endif

/**
 * Unified B+ tree template.
 * - If Tvalue != void → behaves like std::map<Tkey,Tvalue>
 * - If Tvalue == void → behaves like std::set<Tkey>
 */
template <typename Tkey, typename Tvalue = void, size_t B = 64>
class BPlusTree;

/**
 * Map mode
 */
template <typename Tkey, typename Tvalue, size_t B>
struct BPlusNodeMap {
	bool is_leaf;
	size_t count;

	std::array<Tkey, B> keys;
	std::array<Tvalue, B> values;

	/* Children must point to the same instantiation of BPlusNode (map mode) */
	std::array<std::unique_ptr<BPlusNodeMap<Tkey, Tvalue, B>>, B + 1> children;

	BPlusNodeMap *next_leaf = nullptr;
	BPlusNodeMap *prev_leaf = nullptr;
	BPlusNodeMap *parent = nullptr;

	explicit BPlusNodeMap(bool leaf = true) : is_leaf(leaf), count(0)
	{
	}
};

/**
 * Set mode
 */
template <typename Tkey, size_t B>
struct BPlusNodeSet {
	bool is_leaf;
	size_t count;

	std::array<Tkey, B> keys;

	/* Children point to the set-mode specialization */
	std::array<std::unique_ptr<BPlusNodeSet<Tkey, B>>, B + 1> children;

	BPlusNodeSet *next_leaf = nullptr;
	BPlusNodeSet *prev_leaf = nullptr;
	BPlusNodeSet *parent = nullptr;

	explicit BPlusNodeSet(bool leaf = true) : is_leaf(leaf), count(0)
	{
	}
};

template <typename Tkey, typename Tvalue, size_t B, bool = std::is_void_v<Tvalue>>
struct BPlusNodeSelector;

/**
 * Map mode
 */
template <typename Tkey, typename Tvalue, size_t B>
struct BPlusNodeSelector<Tkey, Tvalue, B, false> {
	using type = BPlusNodeMap<Tkey, Tvalue, B>;
};

/**
 * Set mode
 */
template <typename Tkey, typename Tvalue, size_t B>
struct BPlusNodeSelector<Tkey, Tvalue, B, true> {
	using type = BPlusNodeSet<Tkey, B>;
};

template <typename Tkey, typename Tvalue, size_t B>
class BPlusTree {
	using Node = typename BPlusNodeSelector<Tkey,Tvalue,B>::type;
	std::unique_ptr<Node> root;

public:
	BPlusTree() : root(std::make_unique<Node>(true))
	{
	}
	BPlusTree(BPlusTree &&) = default;
	BPlusTree &operator=(BPlusTree &&) = default;

	/**
	 * Iterator types: yields either pair<key,value> or const key &
	 */
	template <typename K, typename V>
	struct BPlusIteratorTraits {
		/* Map mode */
		using reference = std::pair<const K &, V &>;
		using value_type = std::pair<const K, V>;
		using pointer = void;
	};

	template <typename K>
	struct BPlusIteratorTraits<K, void> {
		/* Set mode */
		using reference = const K &;
		using value_type = K;
		using pointer = void;
	};

	struct iterator {
		using Traits = BPlusIteratorTraits<Tkey, Tvalue>;
		using iterator_category = std::bidirectional_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = typename Traits::value_type;
		using reference = typename Traits::reference;
		using pointer = typename Traits::pointer;

		Node *leaf_ = nullptr;
		size_t index_ = 0;
		const BPlusTree *owner_ = nullptr;

		/* Dereference: conditional return type */
		reference operator*() const
		{
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);
			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf_->keys[this->index_]; // set mode
			} else {
				return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] }; // map mode
			}
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
		using Traits = BPlusIteratorTraits<Tkey, std::conditional_t<std::is_void_v<Tvalue>, void, const Tvalue>>;
		using iterator_category = std::bidirectional_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = typename Traits::value_type;   // For set: K; for map: pair<const K, const V>
		using reference = typename Traits::reference;    // For set: const K&; for map: pair<const K&, const V&>
		using pointer = typename Traits::pointer;

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

	/**
	 * Find the index of a child in its parent
	 */
	size_t find_child_index(Node *parent, Node *child) const {
		for (size_t i = 0; i <= parent->count; ++i) {
			if (parent->children[i].get() == child) {
				return i;
			}
		}
		assert(false); // child not found
		return parent->count; // fallback
	}

	/**
	 * Refresh parent separator if leaf’s minimum key changed
	 */
	void refresh_parent_separator_if_min_changed(Node *leaf) {
		if (leaf->parent == nullptr || leaf->count == 0) {
			return;
		}
		size_t idx = this->find_child_index(leaf->parent, leaf);
		if (idx > 0) {
			this->update_separator(leaf->parent, idx - 1);
		}
	}

	/**
	 * Map mode insert: enabled only if Tvalue is not void
	 */
	template <typename U = Tvalue>
	std::enable_if_t<!std::is_void_v<U>, void> insert(const Tkey &key, const U &value)
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

		/* Centralized separator refresh */
		if (i == 0) {
			this->refresh_parent_separator_if_min_changed(leaf);
		}

		if (leaf->count == B) {
			this->split_leaf(leaf);
		}
	}

	/**
	 * Set mode insert: enabled only if Tvalue is void
	 */
	template <typename U = Tvalue>
	std::enable_if_t<std::is_void_v<U>, void> insert(const Tkey &key)
	{
		if (this->root == nullptr) {
			this->root = std::make_unique<Node>(true);
		}
		Node *leaf = this->find_leaf(key);
		size_t i = this->lower_bound(leaf->keys, leaf->count, key);

		/* Duplicate check */
		if (i < leaf->count && leaf->keys[i] == key) {
			/* Policy: ignore duplicate */
			return;
		}

		/* Shift right */
		for (size_t j = leaf->count; j > i; --j) {
			leaf->keys[j] = std::move(leaf->keys[j - 1]);
		}
		leaf->keys[i] = key;
		++leaf->count;

		/* Centralized separator refresh */
		if (i == 0) {
			this->refresh_parent_separator_if_min_changed(leaf);
		}

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
		auto new_leaf = std::make_unique<Node>(true);

		/* Copy half */
		for (size_t j = mid; j < leaf->count; ++j) {
			new_leaf->keys[j - mid] = std::move(leaf->keys[j]);
			if constexpr (!std::is_void_v<Tvalue>) {
				new_leaf->values[j - mid] = std::move(leaf->values[j]);
			}
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
		size_t i = this->find_child_index(parent, left);

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
	 * If the right child is empty, remove the separator and child entirely.
	 */
	void update_separator(Node *parent, size_t sep_idx)
	{
		Node *right = parent->children[sep_idx + 1].get();
		assert(right != nullptr);

		if (right->is_leaf) {
			if (right->count == 0) {
				/* No right minimum exists: remove this boundary */
				this->remove_separator_and_child_right(parent, sep_idx);
			} else {
				parent->keys[sep_idx] = right->keys[0];
			}
		} else {
			/* For internal nodes, separator stays as stored unless explicitly changed elsewhere */
		}
	}

	/**
	 * Borrow the first key from right sibling into leaf at the end.
	 * Update the parent separator to the new right.min.
	 */
	void borrow_from_right_leaf(Node *parent, size_t child_idx)
	{
		Node *leaf = parent->children[child_idx].get();
		Node *right = parent->children[child_idx + 1].get();

		/* Preconditions: right exists and right->count > (B + 1) / 2 */
		leaf->keys[leaf->count] = std::move(right->keys[0]);
		if constexpr (!std::is_void_v<Tvalue>) {
			leaf->values[leaf->count] = std::move(right->values[0]);
		}
		++leaf->count;

		/* Shift right left */
		for (size_t j = 0; j + 1 < right->count; ++j) {
			right->keys[j] = std::move(right->keys[j + 1]);
			if constexpr (!std::is_void_v<Tvalue>) {
				right->values[j] = std::move(right->values[j + 1]);
			}
		}
		--right->count;

		/* Update parent separator */
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

		/* Preconditions: left exists and left->count > (B + 1) / 2 */
		for (size_t j = leaf->count; j > 0; --j) {
			leaf->keys[j] = std::move(leaf->keys[j - 1]);
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[j] = std::move(leaf->values[j - 1]);
			}
		}

		/* Move left[last] -> leaf[0] */
		leaf->keys[0] = std::move(left->keys[left->count - 1]);
		if constexpr (!std::is_void_v<Tvalue>) {
			leaf->values[0] = std::move(left->values[left->count - 1]);
		}
		++leaf->count;
		--left->count;

		/* Update parent separator at boundary (child_idx - 1) */
		this->update_separator(parent, child_idx - 1);
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
			if constexpr (!std::is_void_v<Tvalue>) {
				left->values[left->count + j] = std::move(right->values[j]);
			}
		}
		left->count += right->count;

		/* Rewire leaf links to bypass right */
		left->next_leaf = right->next_leaf;
		if (left->next_leaf != nullptr) {
			left->next_leaf->prev_leaf = left;
		}

		/* Remove separator at i and child i + 1 from parent */
		this->remove_separator_and_child_right(parent, i);

		/* Refresh boundary to the new right sibling (if any) */
		if (i < parent->count) {
			this->update_separator(parent, i);
		}
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
		/* null trailing child for cleanliness */
		parent->children[parent->count + 1].reset();
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
		Node *left  = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		/* Move parent key down */
		left->keys[left->count] = std::move(parent->keys[i]);

		/* Copy right’s keys and children */
		for (size_t k = 0; k < right->count; ++k) {
			left->keys[left->count + 1 + k] = std::move(right->keys[k]);
		}
		for (size_t c = 0; c <= right->count; ++c) {
			left->children[left->count + 1 + c] = std::move(right->children[c]);
			if (left->children[left->count + 1 + c] != nullptr) {
				left->children[left->count + 1 + c]->parent = left;
			}
		}
		left->count += 1 + right->count;

		/* Remove separator and right child */
		this->remove_separator_and_child_right(parent, i);

		/* Cascade: parent may underflow */
		this->fix_internal_underflow_cascade(parent);
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
		size_t i = this->find_child_index(parent, node);

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
			if constexpr (!std::is_void_v<Tvalue>) {
				dst->values = src->values;
			}
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
		this->root = std::make_unique<Node>(true); 
	}

	bool empty() const noexcept
	{
		return this->root == nullptr || this->root->count == 0;
	}

	size_t size() const noexcept
	{
		return this->count_recursive(this->root.get());
	}

	/**
	 * Map mode try_emplace
	 */
	template <typename U = Tvalue>
	std::enable_if_t<!std::is_void_v<U>, std::pair<iterator, bool>> try_emplace(const Tkey &key, const U &value)
	{
		auto it = this->find(key);
		if (it != this->end()) {
			return { it, false }; // already exists
		}

		this->insert(key, value);
		VALIDATE_NODES();

		it = this->find(key);
		return { it, true };
	}

	/**
	 * Set mode try_emplace
	 */
	template <typename U = Tvalue>
	std::enable_if_t<std::is_void_v<U>, std::pair<iterator, bool>> try_emplace(const Tkey &key)
	{
		auto it = this->find(key);
		if (it != this->end()) {
			return { it, false }; // already exists
		}

		this->insert(key);
		VALIDATE_NODES();

		it = this->find(key);
		return { it, true };
	}

	iterator erase(iterator pos)
	{
		if (pos == this->end()) {
			return this->end();
		}

		Node *leaf = pos.leaf_;
		size_t i = pos.index_;

		/* Erase locally (keys/values shift left) */
		for (size_t j = i; j + 1 < leaf->count; ++j) {
			leaf->keys[j] = std::move(leaf->keys[j + 1]);
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[j] = std::move(leaf->values[j + 1]);
			}
		}
		--leaf->count;

		// Centralized separator refresh
		if (i == 0) {
			this->refresh_parent_separator_if_min_changed(leaf);
		}

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
			size_t ci = this->find_child_index(parent, leaf);
			if (ci <= parent->count) {
				this->fix_underflow(parent, ci);
			}
		}

		VALIDATE_NODES();

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

#if BPLUSTREE_CHECK
	bool validate() const
	{
		if (this->root == nullptr) {
			return true;
		}

		/* Check invariants recursively */
		bool ok = this->validate_node(this->root.get(), nullptr, nullptr);
		if (!ok) {
			assert(false);
			return false;
		}

		/* Check leaf linkage */
		Node *leaf = leftmost_leaf();
		Node *prev = nullptr;
		while (leaf != nullptr) {
			/* Keys sorted */
			for (size_t i = 1; i < leaf->count; ++i) {
				if (leaf->keys[i - 1] > leaf->keys[i]) {
					assert(false);
					return false;
				}
			}
			/* Link symmetry */
			if (leaf->prev_leaf != prev) {
				assert(false);
				return false;
			}
			prev = leaf;
			leaf = leaf->next_leaf;
		}
		return true;
	}

	/**
	 * Recursive node validation
	 */
	bool validate_node(Node *node, const Tkey *min, const Tkey *max) const
	{
		if (node->is_leaf) {
			/* Keys sorted */
			for (size_t i = 1; i < node->count; ++i) {
				if (node->keys[i - 1] > node->keys[i]) {
					assert(false);
					return false;
				}
			}
			/* Range check */
			if (min != nullptr && node->keys[0] < *min) {
				assert(false);
				return false;
			}
			if (max != nullptr && node->keys[node->count - 1] > *max) {
				assert(false);
				return false;
			}
			return true;
		} else {
			/* Internal node: keys sorted */
			for (size_t i = 1; i < node->count; ++i) {
				if (node->keys[i - 1] > node->keys[i]) {
					assert(false);
					return false;
				}
			}
			/* Children count = keys + 1 */
			for (size_t i = 0; i <= node->count; ++i) {
				if (node->children[i] == nullptr) {
					assert(false);
					return false;
				}
			}
			/* Separator consistency: parent key == min of right child */
			for (size_t i = 0; i < node->count; ++i) {
				Node *right = node->children[i + 1].get();
				/* In leaves, separator == first key of right child */
				if (right->is_leaf) {
					if (node->keys[i] != right->keys[0]) {
						assert(false);
						return false;
					}
				} else {
					/* In internal nodes, separator <= min key of right child */
					if (node->keys[i] > right->keys[0]) {
						assert(false);
						return false;
					}
				}
			}
			/* Recurse into children with updated ranges */
			for (size_t i = 0; i <= node->count; ++i) {
				const Tkey *child_min = (i == 0 ? min : &node->keys[i - 1]);
				const Tkey *child_max = (i == node->count ? max : &node->keys[i]);
				if (!this->validate_node(node->children[i].get(), child_min, child_max)) {
					assert(false);
					return false;
				}
			}
			return true;
		}
	}
#endif

};

#endif /* BPLUSTREE_TYPE_HPP */
