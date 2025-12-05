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

/** Enable it if you suspect b+ tree doesn't work well */
#define BPLUSTREE_CHECK 1

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
	using Node = typename BPlusNodeSelector<Tkey, Tvalue, B>::type;

	static constexpr size_t MIN_LEAF = (B + 1) / 2;
	static constexpr size_t MIN_INTERNAL = (B + 1) / 2;

public:
	std::unique_ptr<Node> root;

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
		const BPlusTree *tree_ = nullptr;

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
				Node *last = this->tree_->rightmost_leaf();
				this->leaf_ = last;
				if (last != nullptr) {
					if (last->count > 0) {
						this->index_ = last->count - 1;
					} else {
						this->index_ = 0;
					}
				} else {
					this->index_ = 0;
				}
				return *this;
			}
			if (this->index_ == 0) {
				/* Move to previous leaf */
				this->leaf_ = this->leaf_->prev_leaf;
				if (this->leaf_ != nullptr) {
					if (this->leaf_->count > 0) {
						this->index_ = this->leaf_->count - 1;
					} else {
						this->index_ = 0;
					}
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
		using value_type = typename Traits::value_type;
		using reference = typename Traits::reference;
		using pointer = typename Traits::pointer;

		const Node *leaf_ = nullptr;
		size_t index_ = 0;
		const BPlusTree *tree_ = nullptr;

		/* Dereference */
		reference operator*() const
		{
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);
			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf_->keys[this->index_]; // set mode
			} else {
				return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] }; // map mode (const V&)
			}
		}

		/* Increment */
		const_iterator &operator++()
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
		const_iterator &operator--()
		{
			if (this->leaf_ == nullptr) {
				/* --end() => last element */
				const Node *last = this->tree_->rightmost_leaf();
				this->leaf_ = last;
				if (last != nullptr) {
					if (last->count > 0) {
						this->index_ = last->count - 1;
					} else {
						this->index_ = 0;
					}
				} else {
					this->index_ = 0;
				}
				return *this;
			}
			if (this->index_ == 0) {
				this->leaf_ = this->leaf_->prev_leaf;
				if (this->leaf_ != nullptr) {
					if (this->leaf_->count > 0) {
						this->index_ = this->leaf_->count - 1;
					} else {
						this->index_ = 0;
					}
				}
			} else {
				--this->index_;
			}
			return *this;
		}

		friend bool operator==(const const_iterator &a, const const_iterator &b)
		{
			return a.leaf_ == b.leaf_ && a.index_ == b.index_;
		}

		friend bool operator!=(const const_iterator &a, const const_iterator &b)
		{
			return !(a == b);
		}
	};

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

		/* Resolve the key at iterator safely */
		const Tkey key = pos.leaf_->keys[pos.index_];

		/* Find its current leaf (defensive against prior structure shifts) */
		Node *leaf = this->find_leaf(key);

		/* Locate exact position by lower_bound */
		size_t i = lower_bound(leaf->keys, leaf->count, key);
		if (i >= leaf->count || leaf->keys[i] != key) {
			/* Key vanished (shouldn’t happen), return iterator to next key we had computed earlier */
			return this->end();
		}

		/* Erase locally (keys/values shift left) */
		for (size_t j = i; j + 1 < leaf->count; ++j) {
			leaf->keys[j] = std::move(leaf->keys[j + 1]);
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[j] = std::move(leaf->values[j + 1]);
			}
		}
		--leaf->count;

		/* Compute successor key BEFORE any structural changes */
		Tkey succ_key{};
		bool has_succ = false;
		if (i < leaf->count) { // next in same leaf
			succ_key = leaf->keys[i];
			has_succ = true;
		} else if (leaf->next_leaf != nullptr && leaf->next_leaf->count > 0) { // first of next leaf
			succ_key = leaf->next_leaf->keys[0];
			has_succ = true;
		}

		/* Refresh boundary if min changed */
		if (i == 0 && leaf->parent != nullptr) {
			Node *p = leaf->parent;
			size_t child_idx = this->find_child_index(p, leaf);
			if (p->count > 0) {
				if (child_idx > 0) {
					this->refresh_boundary_upward(p, child_idx - 1);
				} else {
					this->refresh_boundary_upward(p, 0);
				}
			} else {
				/* No separators at this parent; just propagate upward along leftmost path */
				this->refresh_boundary_upward(p, 0);
			}
		}

		/* Fix underflow */
		if (leaf->parent != nullptr && leaf->count < MIN_LEAF) {
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
		if (i == 0 && leaf->parent != nullptr) {
			size_t child_idx = this->find_child_index(leaf->parent, leaf);
			if (child_idx > 0) {
				this->maintain_parent_boundary(leaf->parent, child_idx - 1);
			}
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
		if (i == 0 && leaf->parent != nullptr) {
			size_t child_idx = this->find_child_index(leaf->parent, leaf);
			if (child_idx > 0) {
				this->maintain_parent_boundary(leaf->parent, child_idx - 1);
			}
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

	void rewire_children_parent(Node *parent) {
		if (parent == nullptr || parent->is_leaf) {
			return;
		}
		for (size_t i = 0; i <= parent->count; ++i) {
			Node *c = parent->children[i].get();
			if (c != nullptr) {
				c->parent = parent;
			}
		}
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

		/* Belt-and-suspenders: ensure all child parents are correct */
		this->rewire_children_parent(parent);
	}

	void split_internal(Node *node) {
		size_t old_count = node->count;
		size_t mid = old_count / 2;
		assert(mid < old_count);

		auto new_node = std::make_unique<Node>(false);
		Tkey separator = node->keys[mid];

		/* Move keys */
		for (size_t j = mid + 1; j < old_count; ++j) {
			new_node->keys[j - (mid + 1)] = std::move(node->keys[j]);
		}
		new_node->count = old_count - mid - 1;

		/* Move children */
		for (size_t j = mid + 1; j <= old_count; ++j) {
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

		/* Ensure remaining children have correct parent */
		this->rewire_children_parent(node);

		assert(node->count == mid);
		assert(new_node->count > 0);

		this->insert_into_parent(node, separator, new_node.release());

		/* After insert, parent’s children changed; rewire as well */
		this->rewire_children_parent(node->parent); 
	}

	/**
	 * Refresh the separator at sep_idx in 'parent' to match right subtree min,
	 * then, if that changed the ancestor view, propagate the change upward.
	 * This is a conservative, correctness-first approach for debug builds.
	 */
	void refresh_boundary_upward(Node *parent, size_t sep_idx)
	{
		if (parent == nullptr) {
			return;
		}

		/* 1) Refresh at this parent only if there is a valid separator. */
		if (parent->count > 0 && sep_idx < parent->count) {
			this->maintain_parent_boundary(parent, sep_idx);
		}

		/* 2) Propagate upward along the leftmost path.
		 * Even if parent->count == 0, the leftmost path’s subtree minimum can change
		 * and its ancestor separator may need updating. */
		Node *p = parent;
		while (p != nullptr && p->parent != nullptr) {
			Node *gp = p->parent;
			size_t idx_in_gp = this->find_child_index(gp, p);

			/* If this subtree sits to the right of some separator in gp,
			 * refresh that ancestor separator. */
			if (idx_in_gp > 0 && gp->count > 0) {
				this->maintain_parent_boundary(gp, idx_in_gp - 1);
				/* Not on leftmost path anymore; we can stop. */
				break;
			}

			/* Still on the leftmost path; move up. */
			p = gp;
		}
	}

	void maintain_parent_boundary(Node *parent, size_t sep_idx)
	{
		assert(parent != nullptr);

		/* If parent has no separators, there is nothing to refresh locally. */
		if (parent->count == 0) {
			return;
		}

		/* If the requested separator is out of range (e.g., after a merge),
		 * skip the refresh silently. */
		if (sep_idx >= parent->count) {
			return;
		}

		Node *right = parent->children[sep_idx + 1].get();
		if (right == nullptr) {
			/* After a merge, the right child may have been removed explicitly.
			 * Nothing to update here. */
			return;
		}

		if (right->is_leaf) {
			/* Separator must equal the first key of the right leaf. */
			if (right->count > 0) {
				parent->keys[sep_idx] = right->keys[0];
			} else {
				/* Empty right leaf should not persist; merge helpers must remove it. */
				assert(false && "Empty right leaf should have been removed in merge");
			}
		} else {
			/* Internal: separator must equal the minimum of the right subtree. */
			Node *cur = right;
			while (cur != nullptr && !cur->is_leaf) {
				cur = cur->children[0].get();
			}
			assert(cur != nullptr && cur->count > 0);
			parent->keys[sep_idx] = cur->keys[0];
		}
	}

	inline void assert_leaf_sorted(Node *leaf) const {
		if (leaf == nullptr || !leaf->is_leaf) {
			return;
		}
		for (size_t i = 1; i < leaf->count; ++i) {
			assert(leaf->keys[i - 1] < leaf->keys[i]);
		}
	}

	inline void assert_children_ok(Node *n) const {
		if (n == nullptr || n->is_leaf) {
			return;
		}
		for (size_t i = 0; i <= n->count; ++i) {
			assert(n->children[i] != nullptr);
			assert(n->children[i]->parent == n);
		}
	}

	/**
	 * Borrow the first key of right into the end of leaf
	 */
	void borrow_from_right_leaf(Node *parent, size_t child_idx)
	{
		Node *leaf = parent->children[child_idx].get();
		Node *right = parent->children[child_idx + 1].get();

		assert(leaf->count < B && right->count > MIN_LEAF);
		/* Append right’s min key to leaf */
		leaf->keys[leaf->count] = right->keys[0];
		if constexpr (!std::is_void_v<Tvalue>) {
			leaf->values[leaf->count] = right->values[0];
		}
		++leaf->count;

		/* Shift right’s keys/values left */
		for (size_t j = 0; j + 1 < right->count; ++j) {
			right->keys[j] = std::move(right->keys[j + 1]);
			if constexpr (!std::is_void_v<Tvalue>) {
				right->values[j] = std::move(right->values[j + 1]);
			}
		}
		--right->count;

		/* Refresh separator that points to right */
		this->refresh_boundary_upward(parent, child_idx);

		this->assert_leaf_sorted(leaf);
		this->assert_leaf_sorted(right);
	}

	/**
	 * Borrow the last key of left into the front of leaf
	 */
	void borrow_from_left_leaf(Node *parent, size_t child_idx)
	{
		Node *leaf = parent->children[child_idx].get();
		Node *left = parent->children[child_idx - 1].get();

		assert(leaf->count < B && left->count > MIN_LEAF);

		/* Shift leaf right to make space at index 0 */
		for (size_t j = leaf->count; j > 0; --j) {
			leaf->keys[j] = std::move(leaf->keys[j - 1]);
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[j] = std::move(leaf->values[j - 1]);
			}
		}

		/* Move left’s max key to leaf[0] */
		leaf->keys[0] = left->keys[left->count - 1];
		if constexpr (!std::is_void_v<Tvalue>) {
			leaf->values[0] = std::move(left->values[left->count - 1]);
		}
		++leaf->count;

		/* Reduce left count */
		--left->count;

		/* Refresh separator that points to leaf (since its min changed) */
		this->refresh_boundary_upward(parent, child_idx - 1);

		this->assert_leaf_sorted(left);
		this->assert_leaf_sorted(leaf);
	}

	/**
	 * Merge leaf at i + 1 into leaf at i, keep the left leaf.
	 * Preconditions: parent->children[i] and parent->children[i + 1] exist.
	 */
	void merge_leaf_keep_left(Node *parent, size_t i)
	{
		Node *left = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		/* Defensive: right must not contain duplicates of left.max or any erased key */
		assert(left != nullptr && right != nullptr && left->is_leaf && right->is_leaf);

		/* Copy only existing keys post-erase */
		for (size_t j = 0; j < right->count; ++j) {
			left->keys[left->count + j] = std::move(right->keys[j]);
			if constexpr (!std::is_void_v<Tvalue>) {
				left->values[left->count + j] = std::move(right->values[j]);
			}
		}
		left->count += right->count;

		/* Stitch leaves */
		left->next_leaf = right->next_leaf;
		if (left->next_leaf != nullptr) {
			left->next_leaf->prev_leaf = left;
		}

		/* Explicitly remove the separator and right child */
		this->remove_separator_and_right_child(parent, i);

		/* Optional hygiene: clear right leaf links (not strictly necessary since it's detached) */
		right->prev_leaf = nullptr;
		right->next_leaf = nullptr;

		/* After removal, the next separator at i (old i+1) may now reflect a different right-min. */
		if (i < parent->count) {
			this->refresh_boundary_upward(parent, i);
		}
	}

	void remove_separator_and_right_child(Node *parent, size_t sep_idx)
	{
		/* Preconditions */
		assert(parent != nullptr && !parent->is_leaf);
		assert(sep_idx < parent->count);

		/* Shift keys left: [sep_idx + 1..count - 1] -> [sep_idx..count - 2] */
		for (size_t j = sep_idx; j + 1 < parent->count; ++j) {
			parent->keys[j] = std::move(parent->keys[j + 1]);
		}

		/* Shift children left: [sep_idx+2..count] -> [sep_idx+1..count-1] */
		for (size_t j = sep_idx + 1; j + 1 <= parent->count; ++j) {
			parent->children[j] = std::move(parent->children[j + 1]);
			if (parent->children[j] != nullptr) {
				parent->children[j]->parent = parent;
			}
		}

		/* Clear the last child slot that is now out of range */
		parent->children[parent->count].reset();

		/* Decrement count (number of separators) */
		--parent->count;

		/* Safety: ensure all children are valid and wired */
		this->rewire_children_parent(parent);

		/* Optional: if parent becomes empty and is root, shrink height elsewhere */
	}

	void fix_underflow(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();
		assert(child != nullptr);

		const size_t parent_count_before = parent->count;
		const size_t left_count_before = (i > 0 && parent->children[i - 1] != nullptr) ? parent->children[i - 1]->count : 0;
		const size_t right_count_before = (i + 1 <= parent->count && parent->children[i + 1] != nullptr) ? parent->children[i + 1]->count : 0;
		const size_t child_count_before = child->count;

		std::cerr << "UNDERFLOW-ENTER parent.sepCount=" << parent_count_before
			<< " i=" << i
			<< " left.count=" << left_count_before
			<< " child.count=" << child_count_before
			<< " right.count=" << right_count_before
			<< "\n";

		if (child->is_leaf) {
			/* Borrow from right if possible */
			if (i + 1 <= parent->count) {
				Node *right = parent->children[i + 1].get();
				if (right != nullptr && right->count > MIN_LEAF) {
					this->borrow_from_right_leaf(parent, i);
					this->refresh_boundary_upward(parent, i); // separator i points to right
					std::cerr << "BORROW-RIGHT\n";
					return;
				}
			}

			/* Borrow from left if possible */
			if (i > 0) {
				Node *left = parent->children[i - 1].get();
				if (left != nullptr && left->count > MIN_LEAF) {
					this->borrow_from_left_leaf(parent, i);
					this->refresh_boundary_upward(parent, i - 1); // separator (i-1) points to leaf (min changed)
					std::cerr << "BORROW-LEFT\n";
					return;
				}
			}

			/* Merge */
			if (i + 1 <= parent->count) {
				/* Merge right sibling (i+1) into child i; remove separator i */
				this->merge_leaf_keep_left(parent, i);
				std::cerr << "MERGE-KEEP-LEFT\n";
				/* After removal, if i < parent->count, refresh that separator; else refresh last if any */
				if (i < parent->count) {
					this->refresh_boundary_upward(parent, i);
				} else if (parent->count > 0) {
					this->refresh_boundary_upward(parent, parent->count - 1);
				}
			} else {
				/* Rightmost child: merge child i into left sibling (i - 1); remove separator (i - 1) */
				this->merge_leaf_keep_left(parent, i - 1); // mirror variant
				std::cerr << "MERGE-INTO-LEFT\n";
				if ((i - 1) < parent->count) {
					this->refresh_boundary_upward(parent, i - 1);
				} else if (parent->count > 0) {
					this->refresh_boundary_upward(parent, parent->count - 1);
				}
			}

			/* Cascade upward if parent underflows */
			this->fix_internal_underflow_cascade(parent);
			std::cerr << "CASCADE-DONE parent.sepCount=" << parent->count << "\n";
			return;
		}

		/* Internal child underflow path */
		this->fix_underflow_internal_child(parent, i);
	}

	/**
	 * Rotate from the right sibling:
	 * - Move parent.keys[i] down into child at end
	 * - Move right.keys[0] up into parent
	 * - Move right.children[0] into child as new rightmost child
	 */
	void borrow_from_right_internal(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		/* Move parent key down into child */
		child->keys[child->count] = std::move(parent->keys[i]);

		/* Move right’s first child into child */
		child->children[child->count + 1] = std::move(right->children[0]);
		if (child->children[child->count + 1] != nullptr) {
			child->children[child->count + 1]->parent = child;
		}
		++child->count;

		/* Move right’s first key up into parent */
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

		/* Rewire parents defensively */
		this->rewire_children_parent(child);
		this->rewire_children_parent(right);
		this->rewire_children_parent(parent);

		/* Refresh boundary */
		this->refresh_boundary_upward(parent, i);

		/* After modifications: */
		this->assert_children_ok(child);
		this->assert_children_ok(right);
		this->assert_children_ok(parent);
	}

	/**
	 * Rotate from the left sibling:
	 * - Move parent.keys[i-1] down into child at front
	 * - Move left’s last key up into parent
	 * - Move left’s last child into child as new leftmost child
	 */
	void borrow_from_left_internal(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();
		Node *left = parent->children[i - 1].get();

		/* Shift child’s keys/children right */
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

		/* Move left’s last child into child[0] */
		child->children[0] = std::move(left->children[left->count]);
		if (child->children[0] != nullptr) {
			child->children[0]->parent = child;
		}
		++child->count;

		/* Move left’s last key up into parent */
		parent->keys[i - 1] = std::move(left->keys[left->count - 1]);
		--left->count;

		/* Rewire parents defensively */
		this->rewire_children_parent(child);
		this->rewire_children_parent(left);
		this->rewire_children_parent(parent);

		/* Refresh boundary */
		this->refresh_boundary_upward(parent, i - 1);

		/* After modifications: */
		this->assert_children_ok(child);
		this->assert_children_ok(left);
		this->assert_children_ok(parent);
	}

	/**
	 * Merge right sibling (i + 1) into child i. Remove separator i and child (i + 1).
	 */
	void merge_keep_left_internal(Node *parent, size_t i)
	{
		assert(parent != nullptr&& !parent->is_leaf);
		assert(i < parent->count); // must have a right sibling

		Node *left = parent->children[i].get();
		Node *right = parent->children[i + 1].get();

		assert(left != nullptr && right != nullptr);
		assert(!left->is_leaf && !right->is_leaf);

		/* 1) Append separator i to left */
		left->keys[left->count] = std::move(parent->keys[i]);

		/* 2) Copy right’s keys after that */
		for (size_t k = 0; k < right->count; ++k) {
			left->keys[left->count + 1 + k] = std::move(right->keys[k]);
		}

		/* 3) Move right’s children */
		for (size_t c = 0; c <= right->count; ++c) {
			left->children[left->count + 1 + c] = std::move(right->children[c]);
			if (left->children[left->count + 1 + c] != nullptr) {
				left->children[left->count + 1 + c]->parent = left;
			}
		}

		/* 4) Update left count */
		left->count += 1 + right->count;

		/* 5) Remove separator i and right child (i + 1) */
		this->remove_separator_and_right_child(parent, i);

		/* 6) Rewire and refresh */
		this->rewire_children_parent(left);
		this->rewire_children_parent(parent);

		if (i < parent->count) {
			this->refresh_boundary_upward(parent, i);
		} else if (parent->count > 0) {
			this->refresh_boundary_upward(parent, parent->count - 1);
		}
	}

	/**
	* Fix underflow when parent’s child at i is an internal node.
	* Chooses borrow if possible; otherwise merges.
	*/
	void fix_underflow_internal_child(Node *parent, size_t i)
	{
		Node *child = parent->children[i].get();
		assert(parent != nullptr && child != nullptr && !child->is_leaf);

		/* Borrow from right if possible */
		if (i + 1 <= parent->count) {
			Node *right = parent->children[i + 1].get();
			if (right != nullptr && right->count > MIN_INTERNAL) {
				this->borrow_from_right_internal(parent, i);
				/* Refresh the separator i, which points to the right subtree */
				if (parent->count > 0 && i < parent->count) {
					this->refresh_boundary_upward(parent, i);
				}
				return;
			}
		}

		/* Borrow from left if possible */
		if (i > 0) {
			Node *left = parent->children[i - 1].get();
			if (left != nullptr && left->count > MIN_INTERNAL) {
				this->borrow_from_left_internal(parent, i);
				/* Refresh separator (i - 1) due to child min change */
				if (parent->count > 0 && (i - 1) < parent->count) {
					this->refresh_boundary_upward(parent, i - 1);
				}
				return;
			}
		}

		/* Merge: choose direction based on sibling existence */
		if (i + 1 <= parent->count) {
			/* Merge right into child i; remove separator i */
			this->merge_keep_left_internal(parent, i);
			if (parent->count > 0) {
				/* Refresh at i if still valid; otherwise last */
				if (i < parent->count) {
					this->refresh_boundary_upward(parent, i);
				} else {
					this->refresh_boundary_upward(parent, parent->count - 1);
				}
			}
		} else {
			/* Rightmost: merge child i into left sibling; remove separator (i - 1) */
			this->merge_keep_left_internal(parent, i - 1); // mirror call
			if (parent->count > 0) {
				if ((i - 1) < parent->count) {
					this->refresh_boundary_upward(parent, i - 1);
				} else {
					this->refresh_boundary_upward(parent, parent->count - 1);
				}
			}
		}

		/* Cascade upward if parent underflows */
		this->fix_internal_underflow_cascade(parent);
	}

	void maybe_shrink_height()
	{
		if (this->root == nullptr || this->root->is_leaf) {
			return;
		}

		/* If root has no separators, promote its single child. */
		if (this->root->count == 0) {
			std::unique_ptr<Node> child = std::move(this->root->children[0]);
			child->parent = nullptr;
			this->root = std::move(child);
		} else {
			/* Belt-and-suspenders: ensure root’s children point back. */
			for (size_t i = 0; i <= this->root->count; ++i) {
				if (this->root->children[i] != nullptr) {
					this->root->children[i]->parent = this->root.get();
				}
			}
		}
	}

	/**
	 * If an internal node underflows, borrow/merge upward until root is handled.
	 * Root special case: if root becomes empty and has one child, promote the child.
	 */
	void fix_internal_underflow_cascade(Node *node)
	{
		/* Stop at root: shrink height if needed and exit */
		if (node == this->root.get()) {
			this->maybe_shrink_height();
			return;
		}

		Node *parent = node->parent;
		assert(parent != nullptr);

		/* Find node’s index in parent */
		size_t i = this->find_child_index(parent, node);

		/* If node is below minimum, fix it (internal child path) */
		if (node->count < MIN_INTERNAL) {
			this->fix_underflow_internal_child(parent, i);
		}

		/* Optional defensive: if parent becomes empty and isn’t root, its own parent will handle it when reached. */
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
	template <typename Node>
	const Tkey &subtree_min(const Node *node) const
	{
		const Node *cur = node;
		while (cur != nullptr && !cur->is_leaf) {
			cur = cur->children[0].get();
		}
		assert(cur != nullptr && cur->count > 0);
		return cur->keys[0];
	}
public:
	void validate() const
	{
		if (this->root == nullptr) {
			return;
		}

		/* Root invariants */
		if (this->root->is_leaf) {
			assert(this->root->count <= B);
		} else {
			assert(this->root->count <= B);

			for (size_t i = 0; i <= this->root->count; ++i) {
				if (this->root->children[i] == nullptr) {
					assert(false && "null child");
				}
				if (this->root->children[i]->parent != this->root.get()) {
					std::cerr << "Child " << i << " has wrong parent "
						<< this->root->children[i]->parent
						<< " expected " << this->root.get() << "\n";
					this->dump_node(this->root->children[i].get(), 2);
					assert(false);
				}
			}

			/* Root must have at least one child unless the tree is empty */
			for (size_t i = 0; i <= this->root->count; ++i) {
				assert(this->root->children[i] != nullptr);
				assert(this->root->children[i]->parent == this->root.get());
			}
		}

		/* Check invariants recursively */
		this->validate_node(this->root.get(), nullptr, nullptr);

		/* Check leaf linkage */
		Node *leaf = this->leftmost_leaf();
		Node *prev = nullptr;
		while (leaf != nullptr) {
			/* Keys strictly ascending within leaf */
			for (size_t i = 1; i < leaf->count; ++i) {
				assert(leaf->keys[i - 1] < leaf->keys[i]);
			}
			/* Link symmetry (both directions) */
			assert(leaf->prev_leaf == prev);
			if (prev != nullptr) {
				assert(prev->next_leaf == leaf);
			}
			prev = leaf;
			leaf = leaf->next_leaf;
		}

		this->assert_no_leaf_duplicates();
		this->validate_leaf_chain();
	}

private:
	/**
	 * Recursive node validation
	 */
	void validate_node(Node *node, const Tkey *min, const Tkey *max) const
	{
		if (node->is_leaf) {
			/* Capacity bounds */
			assert(node->count <= B);

			if (node->count > 0) {
				/* Keys strictly ascending */
				for (size_t i = 1; i < node->count; ++i) {
					assert(node->keys[i - 1] < node->keys[i]);
				}
				/* Range check */
				if (min != nullptr) {
					assert(node->keys[0] >= *min);
				}
				if (max != nullptr) {
					assert(node->keys[node->count - 1] <= *max);
				}
			}
			/* Leaf has no children */
			for (size_t i = 0; i <= B; ++i) {
				assert(node->children[i] == nullptr);
			}
			return;
		}

		if (node->count > 0) {
			/* Internal keys non-decreasing (B+ trees often allow equal internal keys via redistribution) */
			for (size_t i = 1; i < node->count; ++i) {
				assert(node->keys[i - 1] <= node->keys[i]);
			}
		}

		/* Children count = keys + 1, all non-null */
		for (size_t i = 0; i <= node->count; ++i) {
			assert(node->children[i] != nullptr);
			assert(node->children[i]->parent == node);
		}

		/* Separator consistency: parent key == min of right child */
		for (size_t i = 0; i < node->count; ++i) {
			Node *right = node->children[i + 1].get();
			/* right must exist and have a min */
			assert(right != nullptr);
			assert(right->count > 0 || !right->is_leaf); // tolerate transient empty internal if you allow it
			const Tkey &right_min = this->subtree_min(right);
			this->assert_sep(node, i); // this assert triggered
		}
		
		/* Recurse into children with updated ranges */
		for (size_t i = 0; i <= node->count; ++i) {
			const Tkey *child_min = min;
			if (i > 0) {
				child_min = &node->keys[i - 1];
			}
			const Tkey *child_max = max;
			if (i < node->count) {
				child_max = &node->keys[i];
			}
			this->validate_node(node->children[i].get(), child_min, child_max);
		}
	}

	void validate_leaf_chain() const
	{
		Node *leaf = this->leftmost_leaf();
		bool has_prev = false;
		Tkey prev{};
		while (leaf != nullptr) {
			for (size_t i = 0; i < leaf->count; ++i) {
				if (has_prev) {
					assert(prev < leaf->keys[i]);
				}
				prev = leaf->keys[i];
				has_prev = true;
			}
			leaf = leaf->next_leaf;
		}
	}

	void validate_separators(Node *node) const
	{
		if (node == nullptr || node->is_leaf) {
			return;
		}
		for (size_t i = 0; i < node->count; ++i) {
			Node *right = node->children[i + 1].get();
			assert(right != nullptr);
			const Tkey &right_min = this->subtree_min(right);
			assert(node->keys[i] == right_min);
		}
		for (size_t i = 0; i <= node->count; ++i) {
			this->validate_separators(node->children[i].get());
		}
	}

	/* No duplicates across leaves (global check) */
	void assert_no_leaf_duplicates() const
	{
		std::vector<Tkey> leaves;
		for (Node *leaf = this->leftmost_leaf(); leaf != nullptr; leaf = leaf->next_leaf) {
			for (size_t i = 0; i < leaf->count; ++i) {
				leaves.push_back(leaf->keys[i]);
			}
		}
		for (size_t i = 1; i < leaves.size(); ++i) {
			assert(leaves[i - 1] < leaves[i]); // strictly increasing across leaf chain
		}
	}

	/**
	 * Generic key-to-string helper
	 */
	template <typename K>
	std::string key_to_string(const K &k) const {
		std::ostringstream oss;
		oss << k; // works if K has operator<<
		return oss.str();
	}

	/**
	 * Specialization for std::pair
	 */
	template <typename A, typename B>
	std::string key_to_string(const std::pair<A, B> &p) const {
		std::ostringstream oss;
		oss << "(" << p.first << "," << p.second << ")";
		return oss.str();
	}

	void assert_sep(Node *parent, size_t sep_idx) const
	{
		Node *right = parent->children[sep_idx + 1].get();
		const auto &sep = parent->keys[sep_idx];
		const auto &right_min = this->subtree_min(right);

		if (sep != right_min) {
			std::cerr << "[SEP MISMATCH] parent=" << parent
				<< " sep_idx=" << sep_idx
				<< " sep=" << this->key_to_string(sep)
				<< " right_min=" << this->key_to_string(right_min)
				<< " parent.count=" << parent->count << "\n";
			dump_node(parent, 0);
			dump_node(right, 2);
			assert(false);
		}
	}

public:
	void dump_node(const Node *node, int indent = 0) const
	{
		std::string pad(indent, ' ');

		if (node == nullptr) {
			std::cerr << pad << "null\n";
			return;
		}

		if (node->is_leaf) {
			std::cerr << pad << "Leaf count=" << node->count << " keys=[";
			for (size_t i = 0; i < node->count; ++i) {
				std::cerr << this->key_to_string(node->keys[i]);
				if (i + 1 < node->count) {
					std::cerr << ",";
				}
			}
			std::cerr << "]\n";

			if constexpr (!std::is_void_v<Tvalue>) {
				std::cerr << pad << "  values=[";
				for (size_t i = 0; i < node->count; ++i) {
					std::cerr << node->values[i];
					if (i + 1 < node->count) {
						std::cerr << ",";
					}
				}
				std::cerr << "]\n";
			}
		} else {
			std::cerr << pad << "Internal count=" << node->count << " keys=[";
			for (size_t i = 0; i < node->count; ++i) {
				std::cerr << this->key_to_string(node->keys[i]);
				if (i + 1 < node->count) {
					std::cerr << ",";
				}
			}
			std::cerr << "]\n";

			for (size_t i = 0; i <= node->count; ++i) {
				std::cerr << pad << "  child[" << i << "] ->\n";
				this->dump_node(node->children[i].get(), indent + 4);
			}

			/* Print separators with right.min */
			for (size_t i = 0; i < node->count; ++i) {
				Node *right = node->children[i + 1].get();
				if (right != nullptr) {
					std::cerr << pad << "  separator[" << i << "]="
						<< this->key_to_string(node->keys[i])
						<< " (right.min=" << this->key_to_string(this->subtree_min(right)) << ")\n";
				}
			}
		}
	}
#endif
};

#endif /* BPLUSTREE_TYPE_HPP */
