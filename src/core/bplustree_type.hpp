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

/**
 * 1) Forward declare the tree
 */
template <typename Tkey, typename Tvalue = void, uint8_t B = 64>
class BPlusTree;

/**
 * 2) Role
 */
enum class BPlusNodeRole : uint8_t {
	Leaf,
	Internal
};

/**
 * 3) Common base (shared fields, virtual dtor)
 */
template <typename Tkey, uint8_t B>
struct BPlusNodeBase {
	BPlusNodeRole role;
	uint8_t count = 0;
	uint8_t index_in_parent = 0;
	std::array<Tkey, B> keys;

	explicit BPlusNodeBase(BPlusNodeRole role) : role(role)
	{
	}

	virtual ~BPlusNodeBase() = default;
};

/**
 * 4) Forward declare internals (needed for parent pointers)
 */
template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusInternalMap;

template <typename Tkey, uint8_t B>
struct BPlusInternalSet;

/**
 * 5) Map mode hierarchy
 */
template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusNodeMap : BPlusNodeBase<Tkey, B> {
	BPlusInternalMap<Tkey, Tvalue, B> *parent = nullptr; // always internal or null (root)

	explicit BPlusNodeMap(BPlusNodeRole role) : BPlusNodeBase<Tkey, B>(role)
	{
	}

	virtual ~BPlusNodeMap() = default; // keep polymorphic at Node level too
};

template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusLeafMap : BPlusNodeMap<Tkey, Tvalue, B> {
	std::array<Tvalue, B> values;
	BPlusLeafMap *next_leaf = nullptr;
	BPlusLeafMap *prev_leaf = nullptr;

	BPlusLeafMap() : BPlusNodeMap<Tkey, Tvalue, B>(BPlusNodeRole::Leaf)
	{
	}
};

template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusInternalMap : BPlusNodeMap<Tkey, Tvalue, B> {
	std::array<std::unique_ptr<BPlusNodeMap<Tkey, Tvalue, B>>, B + 1> children;

	BPlusInternalMap() : BPlusNodeMap<Tkey, Tvalue, B>(BPlusNodeRole::Internal)
	{
	}
};

/**
 * 6) Set mode hierarchy
 */
template <typename Tkey, uint8_t B>
struct BPlusNodeSet : BPlusNodeBase<Tkey, B> {
	BPlusInternalSet<Tkey, B> *parent = nullptr; // always internal or null (root)

	explicit BPlusNodeSet(BPlusNodeRole role) : BPlusNodeBase<Tkey, B>(role)
	{
	}

	virtual ~BPlusNodeSet() = default; // keep polymorphic at Node level too
};

template <typename Tkey, uint8_t B>
struct BPlusLeafSet : BPlusNodeSet<Tkey, B> {
	BPlusLeafSet *next_leaf = nullptr;
	BPlusLeafSet *prev_leaf = nullptr;

	BPlusLeafSet() : BPlusNodeSet<Tkey, B>(BPlusNodeRole::Leaf)
	{
	}
};

template <typename Tkey, uint8_t B>
struct BPlusInternalSet : BPlusNodeSet<Tkey, B> {
	std::array<std::unique_ptr<BPlusNodeSet<Tkey, B>>, B + 1> children;

	BPlusInternalSet() : BPlusNodeSet<Tkey, B>(BPlusNodeRole::Internal)
	{
	}
};

/**
 * 7) Traits — primary template and partial specialization for set mode
 */
template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusNodeTraits {
	using Base = BPlusNodeBase<Tkey, B>;
	using Node = BPlusNodeMap<Tkey, Tvalue, B>;
	using Leaf = BPlusLeafMap<Tkey, Tvalue, B>;
	using Internal = BPlusInternalMap<Tkey, Tvalue, B>;
};

template <typename Tkey, uint8_t B>
struct BPlusNodeTraits<Tkey, void, B> {
	using Base = BPlusNodeBase<Tkey, B>;
	using Node = BPlusNodeSet<Tkey, B>;
	using Leaf = BPlusLeafSet<Tkey, B>;
	using Internal = BPlusInternalSet<Tkey, B>;
};

/**
 * 8) Tree — use traits for types, including root
 */
template <typename Tkey, typename Tvalue, uint8_t B>
class BPlusTree {
private:
	using Traits = BPlusNodeTraits<Tkey, Tvalue, B>;
	using Node = typename Traits::Node;
	using Leaf = typename Traits::Leaf;
	using Internal = typename Traits::Internal;

	static constexpr uint8_t MIN_LEAF = (B + 1) / 2; // ceil(B/2)
	static constexpr uint8_t MIN_INTERNAL = (B - 1) / 2; // ceil(B/2) - 1

	std::unique_ptr<Node> root;

public:
	BPlusTree()
	{
		/* Empty tree: root is an empty leaf */
		this->root = std::make_unique<Leaf>();
		this->root->parent = nullptr;
		this->root->index_in_parent = 0;
	}

	~BPlusTree() = default;

	/**
	 * Check if tree contains a key
	 */
	bool contains(const Tkey &key) const
	{
		return this->find(key) != this->end();
	}

	/**
	 * Swap roots between two trees
	 */
	void swap(BPlusTree &other) noexcept
	{
		this->root.swap(other.root);
	}

	/**
	 * Clear tree: reset to a fresh empty leaf node
	 */
	void clear() noexcept
	{
		this->root = std::make_unique<Leaf>(); // Leaf ctor sets role = Leaf
	}

	/**
	 * Check if tree is empty
	 */
	bool empty() const noexcept
	{
		assert(this->root != nullptr);

		return this->root->count == 0;
	}

	size_t size() const noexcept
	{
		assert(this->root != nullptr);

		return this->count_recursive(this->root.get());
	}

	BPlusTree &operator=(const BPlusTree &other)
	{
		if (this != &other) {
			this->root.reset();
			if (other.root != nullptr) {
				std::vector<Leaf *> leaves;
				this->root = this->clone_node(other.root.get(), nullptr, 0, leaves);

				/* Root invariants: parent = nullptr, index_in_parent = 0 */
				this->root->parent = nullptr;
				this->root->index_in_parent = 0;

				this->rebuild_leaf_chain(leaves);
			}
		}
#if BPLUSTREE_CHECK
		this->verify_node(this->root.get());
#endif
		return *this;
	}

private:
	size_t count_recursive(const Node *node) const
	{
		if (node == nullptr) {
			return 0;
		}

		if (node->role == BPlusNodeRole::Leaf) {
			return node->count;
		} else {
			const Internal *internal = static_cast<const Internal *>(node);
			size_t total = 0;
			for (uint8_t i = 0; i <= internal->count; ++i) {
				total += this->count_recursive(internal->children[i].get());
			}
			return total;
		}
	}

	std::unique_ptr<Node> clone_node(const Node *src, Internal *parent, uint8_t slot, std::vector<Leaf *> &leaves)
	{
		if (src->role == BPlusNodeRole::Leaf) {
			const Leaf *src_leaf = static_cast<const Leaf *>(src);
			std::unique_ptr<Leaf> dst_leaf = std::make_unique<Leaf>();

			dst_leaf->count = src_leaf->count;
			dst_leaf->keys = src_leaf->keys;
			if constexpr (!std::is_void_v<Tvalue>) {
				dst_leaf->values = src_leaf->values;
			}
			dst_leaf->parent = parent;
			dst_leaf->index_in_parent = (parent != nullptr ? slot : 0);

			/* Collect pointer to the destination leaf */
			leaves.push_back(dst_leaf.get());

			return dst_leaf;
		} else {
			const Internal *src_internal = static_cast<const Internal *>(src);
			std::unique_ptr<Internal> dst_internal = std::make_unique<Internal>();

			dst_internal->count = src_internal->count;
			dst_internal->keys = src_internal->keys;
			dst_internal->parent = parent;
			dst_internal->index_in_parent = (parent != nullptr ? slot : 0);

			for (uint8_t i = 0; i <= src_internal->count; ++i) {
				if (src_internal->children[i] != nullptr) {
					std::unique_ptr<Node> child_clone = this->clone_node(src_internal->children[i].get(), dst_internal.get(), i, leaves);
					dst_internal->children[i] = std::move(child_clone);

					if (dst_internal->children[i] != nullptr) {
						dst_internal->children[i]->parent = dst_internal.get();
						dst_internal->children[i]->index_in_parent = i;
					}
				} else {
					dst_internal->children[i].reset();
				}
			}

			return dst_internal;
		}
	}

	void rebuild_leaf_chain(std::vector<Leaf *> &leaves)
	{
		Leaf *prev = nullptr;
		for (Leaf *leaf : leaves) {
			leaf->prev_leaf = prev;
			leaf->next_leaf = nullptr; // filled by next linking back
			if (prev != nullptr) {
				prev->next_leaf = leaf;
			}
			prev = leaf;
		}
	}

	/**
	 * Iterator types: yields either pair<key, value> or const key &
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

public:
	struct iterator {
		using Traits = BPlusIteratorTraits<Tkey, Tvalue>;
		using iterator_category = std::bidirectional_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = typename Traits::value_type;
		using reference = typename Traits::reference;
		using pointer = typename Traits::pointer;

		Leaf *leaf_ = nullptr;
		uint8_t index_ = 0;
		const BPlusTree *tree_ = nullptr;

		/* Dereference: conditional return type */
		reference operator*() const
		{
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);
			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf_->keys[this->index_]; // set mode
			} else {
				return std::pair<const Tkey &, Tvalue &>(this->leaf_->keys[this->index_], this->leaf_->values[this->index_]); // map mode
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
				Leaf *last = this->tree_->rightmost_leaf();
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

		const Leaf *leaf_ = nullptr;
		uint8_t index_ = 0;
		const BPlusTree *tree_ = nullptr;

		/* Dereference */
		reference operator*() const
		{
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);
			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf_->keys[this->index_]; // set mode
			} else {
				return std::pair<const Tkey &, const Tvalue &>(this->leaf_->keys[this->index_], this->leaf_->values[this->index_]); // map mode (const V &)
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
				/* Special case: --end() => last element */
				const Leaf *last = this->tree_->rightmost_leaf();
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
		Leaf *first = this->leftmost_leaf();
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
		Leaf *leaf = this->find_leaf(key);
		if (leaf == nullptr) {
			return this->end();
		}

		uint8_t i = this->lower_bound(leaf->keys, leaf->count, key);
		if (i < leaf->count && leaf->keys[i] == key) {
			return iterator(leaf, i, this);
		}
		return this->end();
	}

	const_iterator find(const Tkey &key) const
	{
		const Leaf *leaf = this->find_leaf(key);
		if (leaf == nullptr) {
			return this->cend();
		}

		uint8_t i = this->lower_bound(leaf->keys, leaf->count, key);
		if (i < leaf->count && leaf->keys[i] == key) {
			return const_iterator(leaf, i, this);
		}
		return this->cend();
	}

	iterator lower_bound(const Tkey &key)
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend while we’re in an internal node */
		while (node->role == BPlusNodeRole::Internal) {
			Internal *internal = static_cast<Internal *>(node);
			uint8_t i = this->upper_bound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = internal->children[i].get();
		}

		/* At this point, node must be a leaf */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);

		uint8_t i = this->lower_bound(leaf->keys, leaf->count, key);
		return iterator(leaf, i, this);
	}

	/**
	 * Map mode emplace without re-find
	 */
	template <typename U = Tvalue>
	std::enable_if_t<!std::is_void_v<U>, std::pair<iterator, bool>> emplace(const Tkey &key, const U &value)
	{
		iterator it = this->lower_bound(key);
		Leaf *leaf = it.leaf_;
		uint8_t idx = it.index_;

		if (idx < leaf->count && leaf->keys[idx] == key) {
			return std::pair<iterator, bool>(it, false); // already exists
		}

		/* Perform insert (may split) */
		this->insert(leaf, idx, key, value);

		VALIDATE_NODES();

		/* Fast path: still in same leaf */
		if (idx < leaf->count && leaf->keys[idx] == key) {
			return std::pair<iterator, bool>(it, true);
		}

		/* Split-aware fallback: check right neighbour */
		Leaf *right = leaf->next_leaf;
		assert(idx >= leaf->count); // security: ensure index is in right sibling
		uint8_t j = idx - leaf->count;

		it = iterator(right, j, this);
		assert(this->verify_return_iterator(it, key));

		return std::pair<iterator, bool>(it, true);
	}

	/**
	 * Set mode emplace without re-find
	 */
	template <typename U = Tvalue>
	std::enable_if_t<std::is_void_v<U>, std::pair<iterator, bool>> emplace(const Tkey &key)
	{
		iterator it = this->lower_bound(key);
		Leaf *leaf = it.leaf_;
		uint8_t idx = it.index_;

		if (idx < leaf->count && leaf->keys[idx] == key) {
			return std::pair<iterator, bool>(it, false); // already exists
		}

		/* Perform insert (may split) */
		this->insert(leaf, idx, key);

		VALIDATE_NODES();

		/* Fast path: still in same leaf */
		if (idx < leaf->count && leaf->keys[idx] == key) {
			return std::pair<iterator, bool>(it, true);
		}

		/* Split-aware fallback: check right neighbour */
		Leaf *right = leaf->next_leaf;
		assert(idx >= leaf->count); // security: ensure index is in right sibling
		uint8_t j = idx - leaf->count;

		it = iterator(right, j, this);
		assert(this->verify_return_iterator(it, key));

		return std::pair<iterator, bool>(it, true);
	}

	iterator erase(iterator pos)
	{
		if (pos == this->end()) {
			return this->end();
		}

		Leaf *leaf = pos.leaf_;
		uint8_t i = pos.index_;

		/* Erase locally (shift left) */
		std::move(leaf->keys.begin() + i + 1, leaf->keys.begin() + leaf->count, leaf->keys.begin() + i);

		if constexpr (!std::is_void_v<Tvalue>) {
			std::move(leaf->values.begin() + i + 1, leaf->values.begin() + leaf->count, leaf->values.begin() + i);
		}

		leaf->count--;

		/* Prepare iterator to retarget */
		iterator succ_it(this->end());

		/* Compute successor key before structural changes */
		Tkey succ_key{};
		bool has_succ = false;

		if (i < leaf->count) {
			succ_key = leaf->keys[i];
			has_succ = true;
			succ_it = iterator(leaf, i, this);
		} else if (leaf->next_leaf != nullptr && leaf->next_leaf->count > 0) {
			succ_key = leaf->next_leaf->keys[0];
			has_succ = true;
			succ_it = iterator(leaf->next_leaf, 0, this);
		}

		/* Boundary refresh if min changed */
		if (i == 0 && leaf->parent != nullptr) {
			Internal *parent = leaf->parent;
			uint8_t child_idx = this->find_child_index(parent, leaf);

			if (parent->count > 0) {
				if (child_idx > 0) {
					this->refresh_boundary_upward(parent, child_idx - 1);
				} else {
					this->refresh_boundary_upward(parent, 0);
				}
			} else {
				this->refresh_boundary_upward(parent, 0);
			}
		}

		/* Fix underflow, passing iterator by reference */
		if (leaf->parent != nullptr && leaf->count < MIN_LEAF) {
			Internal *parent = leaf->parent;
			uint8_t child_idx = this->find_child_index(parent, leaf);

			if (child_idx <= parent->count) {
				this->fix_underflow(parent, child_idx, succ_it);
			}
		}

		VALIDATE_NODES();

		if (!has_succ) {
			return this->end();
		}

		assert(this->verify_return_iterator(succ_it, succ_key));
		return succ_it;
	}

private:
	bool verify_return_iterator(const iterator &a, const Tkey &succ_key)
	{
		Leaf *succ_leaf = this->find_leaf(succ_key);
		if (succ_leaf == nullptr) {
			return false; // defensive: no leaf found for succ_key
		}

		uint8_t succ_idx = this->lower_bound(succ_leaf->keys, succ_leaf->count, succ_key);

		return (a.leaf_ == succ_leaf && a.index_ == succ_idx);
	}

	Leaf *leftmost_leaf() const
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend leftmost children while internal */
		while (node->role == BPlusNodeRole::Internal) {
			Internal *internal = static_cast<Internal *>(node);
			assert(internal->children[0] != nullptr);
			node = internal->children[0].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	Leaf *rightmost_leaf() const
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend rightmost children while internal */
		while (node->role == BPlusNodeRole::Internal) {
			Internal *internal = static_cast<Internal *>(node);
			assert(internal->children[internal->count] != nullptr);
			node = internal->children[internal->count].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	/**
	 * Find the index of a child in its parent.
	 * Works for both Leaf and Internal children.
	 */
	template <typename T>
	uint8_t find_child_index(Internal *parent, T *child) const
	{
		assert(parent != nullptr);
		assert(child != nullptr);
		assert(child->index_in_parent <= parent->count);

		/* Retrieve the node at the expected slot */
		[[maybe_unused]] Node *node = parent->children[child->index_in_parent].get();
		assert(node != nullptr);

		/* Verify that the pointer matches the expected child */
		assert(node == child);

		return child->index_in_parent;
	}

	/**
	 * Core insert logic: always shifts keys, optionally shifts values
	 */
	void insert_common(Leaf *leaf, uint8_t i, const Tkey &key)
	{
		/* Shift keys */
		std::move_backward(leaf->keys.begin() + i, leaf->keys.begin() + leaf->count, leaf->keys.begin() + leaf->count + 1);

		leaf->keys[i] = key;
		leaf->count++;

		/* Centralized separator refresh */
		if (i == 0 && leaf->parent != nullptr) {
			Internal *parent = leaf->parent;
			uint8_t child_idx = this->find_child_index(parent, leaf);
			if (child_idx > 0) {
				this->maintain_parent_boundary(parent, child_idx - 1);
			}
		}

		/* Split if leaf is full */
		if (leaf->count == B) {
			this->split_leaf(leaf);
		}
	}

	/**
	 * Map mode insert
	 */
	template <typename U = Tvalue>
	std::enable_if_t<!std::is_void_v<U>, void> insert(Leaf *leaf, uint8_t i, const Tkey &key, const U &value)
	{
		/* Shift values */
		std::move_backward(leaf->values.begin() + i, leaf->values.begin() + leaf->count, leaf->values.begin() + leaf->count + 1);

		leaf->values[i] = value;

		this->insert_common(leaf, i, key);
	}

	/**
	 * Set mode insert
	 */
	template <typename U = Tvalue>
	std::enable_if_t<std::is_void_v<U>, void> insert(Leaf *leaf, uint8_t i, const Tkey &key)
	{
		this->insert_common(leaf, i, key);
	}

	/**
	 * Descend the tree to find the leaf containing or suitable for the given key.
	 */
	Leaf *find_leaf(const Tkey &key) const
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend while internal */
		while (node->role == BPlusNodeRole::Internal) {
			Internal *internal = static_cast<Internal *>(node);
			uint8_t i = this->upper_bound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = internal->children[i].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	/**
	 * Verify and rewire children's parent/index_in_parent fields for a given internal node.
	 */
	void rewire_children_parent(Node *node)
	{
		if (node == nullptr) {
			return;
		}

		/* Must be an internal node */
		assert(node->role == BPlusNodeRole::Internal);
		Internal *internal = static_cast<Internal *>(node);

		for (uint8_t i = 0; i <= internal->count; ++i) {
			[[maybe_unused]] std::unique_ptr<Node> &child = internal->children[i];
			assert(child != nullptr);
			assert(child->parent == internal);
			assert(child->index_in_parent == i);
		}
	}

	/**
	 * Split a full leaf into two halves and insert separator into parent.
	 */
	void split_leaf(Leaf *leaf)
	{
		assert(leaf != nullptr);
		uint8_t mid = leaf->count / 2;

		/* Create a new Leaf node */
		std::unique_ptr<Leaf> new_leaf_node = std::make_unique<Leaf>();
		Leaf *new_leaf = new_leaf_node.get();

		/* Move half of the keys/values into the new leaf */
		std::move(leaf->keys.begin() + mid, leaf->keys.begin() + leaf->count, new_leaf->keys.begin());

		if constexpr (!std::is_void_v<Tvalue>) {
			std::move(leaf->values.begin() + mid, leaf->values.begin() + leaf->count, new_leaf->values.begin());
		}

		new_leaf->count = leaf->count - mid;
		leaf->count = mid;
		assert(new_leaf->count > 0);

		/* Link leaves */
		new_leaf->next_leaf = leaf->next_leaf;
		if (new_leaf->next_leaf != nullptr) {
			new_leaf->next_leaf->prev_leaf = new_leaf;
		}
		new_leaf->prev_leaf = leaf;
		leaf->next_leaf = new_leaf;

		/* Separator = first key of new_leaf */
		Tkey separator = new_leaf->keys[0];

		/* Insert separator into parent */
		this->insert_into_parent(leaf, separator, std::move(new_leaf_node));
	}

	/**
	 * Insert a separator key and right child into the parent of 'left'.
	 * If 'left' has no parent, create a new root.
	 */
	void insert_into_parent(Node *left, const Tkey &separator, std::unique_ptr<Node> right_node)
	{
		/* Parent must be an internal node if it exists */
		Internal *parent = left->parent;
		assert(left->parent == nullptr || left->parent->role == BPlusNodeRole::Internal);

		/* Root case: parent == nullptr → create fresh internal root */
		if (parent == nullptr) {
			std::unique_ptr<Internal> new_root_ptr = std::make_unique<Internal>();
			Internal *new_root = new_root_ptr.get();

			/* Promote old root (which contains 'left') and right_node under new_root */
			std::unique_ptr<Node> old_root = std::move(this->root);

			/* Assert that old_root holds the same node as 'left' */
			assert(old_root.get() == left);

			new_root->keys[0] = separator;
			new_root->children[0] = std::move(old_root);
			new_root->children[1] = std::move(right_node);
			new_root->count = 1;

			/* Fix children's parent/index_in_parent */
			new_root->children[0]->parent = new_root;
			new_root->children[0]->index_in_parent = 0;

			new_root->children[1]->parent = new_root;
			new_root->children[1]->index_in_parent = 1;

			this->root = std::move(new_root_ptr);
			this->rewire_children_parent(this->root.get());
			return;
		}

		/* Parent exists: ensure space */
		while (parent->count == B - 1) {
			this->split_internal(parent);
			assert(left->parent != nullptr && left->parent->role == BPlusNodeRole::Internal);
			parent = left->parent; // left may have moved
		}

		uint8_t i = this->find_child_index(parent, left);

		/* Shift keys and children right */
		std::move_backward(parent->keys.begin() + i, parent->keys.begin() + parent->count, parent->keys.begin() + parent->count + 1);

		std::move_backward(parent->children.begin() + i + 1, parent->children.begin() + parent->count + 1, parent->children.begin() + parent->count + 2);

		/* Refresh children's parent/index_in_parent after shift */
		for (uint8_t j = i + 1; j <= parent->count + 1; ++j) {
			if (parent->children[j] != nullptr) {
				parent->children[j]->parent = parent;
				parent->children[j]->index_in_parent = j;
			}
		}

		/* Insert separator and right child */
		parent->keys[i] = separator;
		assert(parent->children[i + 1] == nullptr);
		parent->children[i + 1] = std::move(right_node);

		if (parent->children[i + 1] != nullptr) {
			parent->children[i + 1]->parent = parent;
			parent->children[i + 1]->index_in_parent = i + 1;
		}

		++parent->count;
		this->rewire_children_parent(parent);
	}

	/**
	 * Split a full internal node into two halves and promote a separator key.
	 */
	void split_internal(Internal *node)
	{
		assert(node != nullptr);

		uint8_t mid = node->count / 2;
		assert(mid < node->count);

		/* Separator promoted to parent */
		Tkey separator = node->keys[mid];

		/* Create new right internal node */
		std::unique_ptr<Internal> right_node = std::make_unique<Internal>();
		Internal *right = right_node.get();

		/* Move keys: left keeps [0..mid - 1], right gets [mid + 1..node->count - 1] */
		std::move(node->keys.begin() + mid + 1, node->keys.begin() + node->count, right->keys.begin());

		right->count = node->count - mid - 1;

		/* Move children: left keeps [0..mid], right gets [mid + 1..node->count] */
		std::move(node->children.begin() + mid + 1, node->children.begin() + node->count + 1, right->children.begin());

		/* Fix parent/index_in_parent for moved children in right */
		for (uint8_t j = 0; j <= right->count; ++j) {
			Node *child = right->children[j].get();
			assert(child != nullptr);
			child->parent = right;
			child->index_in_parent = j;
		}

		/* Left node keeps first mid keys and mid + 1 children */
		node->count = mid;

		/* Clear dangling child slots beyond mid in left */
		std::fill(node->children.begin() + mid + 1, node->children.begin() + B + 1, nullptr);

		/* Ensure remaining children in left are wired correctly */
		this->rewire_children_parent(node);

		assert(node->count == mid);
		assert(right->count > 0);

		/* Insert separator and right child into parent */
		this->insert_into_parent(node, separator, std::move(right_node));

		/* After insertion, parent’s children changed; defensively rewire */
		if (node->parent != nullptr) {
			this->rewire_children_parent(node->parent);
		}
	}

	/**
	 * Refresh the separator at sep_idx in 'parent' to match right subtree min,
	 * then propagate the change upward if needed.
	 */
	void refresh_boundary_upward(Internal *parent, uint8_t sep_idx)
	{
		assert(parent != nullptr);

		/* Refresh at this parent only if there is a valid separator */
		if (sep_idx < parent->count) {
			this->maintain_parent_boundary(parent, sep_idx);
		}

		/* Propagate upward along the leftmost path */
		for (Internal *p = parent; p->parent != nullptr; p = p->parent) {
			Internal *gp = p->parent;
			uint8_t idx_in_gp = this->find_child_index(gp, p);

			/* If this subtree sits to the right of some separator in gp,
			 * refresh that ancestor separator. */
			if (idx_in_gp > 0) {
				this->maintain_parent_boundary(gp, idx_in_gp - 1);
				break; // not on leftmost path anymore
			}
		}
	}

	/**
	 * Refresh the separator at sep_idx in 'parent' to match right subtree min.
	 */
	void maintain_parent_boundary(Internal *parent, uint8_t sep_idx)
	{
		assert(parent != nullptr);

		/* If parent has no separators, nothing to refresh */
		if (parent->count == 0) {
			return;
		}

		/* If sep_idx is out of range (e.g. after merge), skip silently */
		if (sep_idx >= parent->count) {
			return;
		}

		/* Right child at sep_idx + 1 */
		Node *right = parent->children[sep_idx + 1].get();
		assert(right != nullptr);

		/* Case 1: right child is a Leaf */
		if (right->role == BPlusNodeRole::Leaf) {
			Leaf *right_leaf = static_cast<Leaf *>(right);
			assert(right_leaf->count > 0 && "Empty right leaf should have been removed in merge");
			parent->keys[sep_idx] = right_leaf->keys[0];

		/* Case 2: right child is an Internal */
		} else {
			Internal *right_internal = static_cast<Internal *>(right);
			Node *cur = right_internal;

			/* Descend to leftmost leaf of right subtree */
			while (cur->role == BPlusNodeRole::Internal) {
				Internal *internal = static_cast<Internal *>(cur);
				assert(internal->children[0] != nullptr);
				cur = internal->children[0].get();
			}

			Leaf *leaf = static_cast<Leaf *>(cur);
			assert(leaf != nullptr && leaf->count > 0);
			parent->keys[sep_idx] = leaf->keys[0];
		}
	}

	/**
	 * Helper to fetch a child leaf from an internal node.
	 */
	Leaf *get_child_leaf(Internal *parent, uint8_t index)
	{
		assert(parent != nullptr);
		assert(index <= parent->count);

		Node *node = parent->children[index].get();
		assert(node != nullptr);
		assert(node->role == BPlusNodeRole::Leaf);

		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);

		return leaf;
	}

	/**
	 * Borrow the first key of right into the end of leaf.
	 */
	void borrow_from_right_leaf(Internal *parent, uint8_t child_idx, iterator &succ_it)
	{
		assert(parent != nullptr);

		/* Get left and right children as Leaf * */
		Leaf *leaf = this->get_child_leaf(parent, child_idx);
		Leaf *right = this->get_child_leaf(parent, child_idx + 1);

		assert(leaf->count < B && right->count > MIN_LEAF);

		/* Append right’s min key to leaf */
		leaf->keys[leaf->count] = right->keys[0];
		if constexpr (!std::is_void_v<Tvalue>) {
			leaf->values[leaf->count] = right->values[0];
		}

		/* Retarget iterator if it was pointing into right */
		if (succ_it.leaf_ == right) {
			succ_it.leaf_ = leaf;
			succ_it.index_ = leaf->count;
		}

		++leaf->count;

		/* Shift right’s keys/values left */
		std::move(right->keys.begin() + 1, right->keys.begin() + right->count, right->keys.begin());

		if constexpr (!std::is_void_v<Tvalue>) {
			std::move(right->values.begin() + 1, right->values.begin() + right->count, right->values.begin());
		}

		--right->count;

		/* Refresh separator that points to right */
		this->refresh_boundary_upward(parent, child_idx);
	}

	/**
	 * Borrow the last key of left into the front of leaf.
	 */
	void borrow_from_left_leaf(Internal *parent, uint8_t child_idx, iterator &succ_it)
	{
		assert(parent != nullptr);
		assert(child_idx > 0);

		/* Get left and right children as Leaf * */
		Leaf *leaf = this->get_child_leaf(parent, child_idx);
		Leaf *left = this->get_child_leaf(parent, child_idx - 1);

		assert(leaf->count < B && left->count > MIN_LEAF);

		/* Shift leaf right to make space at index 0 */
		std::move_backward(leaf->keys.begin(), leaf->keys.begin() + leaf->count, leaf->keys.begin() + leaf->count + 1);

		if constexpr (!std::is_void_v<Tvalue>) {
			std::move_backward(leaf->values.begin(), leaf->values.begin() + leaf->count, leaf->values.begin() + leaf->count + 1);
		}

		/* Move left’s max key into leaf[0] */
		leaf->keys[0] = left->keys[left->count - 1];
		if constexpr (!std::is_void_v<Tvalue>) {
			leaf->values[0] = std::move(left->values[left->count - 1]);
		}

		/* Retarget iterator if it was pointing into leaf */
		if (succ_it.leaf_ == leaf) {
			++succ_it.index_;
		}

		++leaf->count;
		--left->count;

		/* Refresh separator that points to leaf (since its min changed) */
		this->refresh_boundary_upward(parent, child_idx - 1);
	}

	/**
	 * Merge leaf at i + 1 into leaf at i, keep the left leaf.
	 * Preconditions: parent->children[i] and parent->children[i + 1] exist.
	 */
	void merge_leaf_keep_left(Internal *parent, uint8_t i, iterator &succ_it)
	{
		assert(parent != nullptr);
		assert(i < parent->count);

		/* Get left and right children as Leaf * */
		Leaf *left = this->get_child_leaf(parent, i);
		Leaf *right = this->get_child_leaf(parent, i + 1);

		/* Move only existing keys post-erase */
		std::move(right->keys.begin(), right->keys.begin() + right->count, left->keys.begin() + left->count);

		if constexpr (!std::is_void_v<Tvalue>) {
			std::move(right->values.begin(), right->values.begin() + right->count, left->values.begin() + left->count);
		}

		/* Retarget iterator if it was pointing into right */
		if (succ_it.leaf_ == right) {
			succ_it.leaf_ = left;
			succ_it.index_ += left->count;
		}

		left->count += right->count;

		/* Stitch leaves */
		left->next_leaf = right->next_leaf;
		if (left->next_leaf != nullptr) {
			left->next_leaf->prev_leaf = left;
		}

		/* Explicitly remove the separator and right child */
		this->remove_separator_and_right_child(parent, i);

		/* After removal, the next separator at i may now reflect a different right-min */
		if (i < parent->count) {
			this->refresh_boundary_upward(parent, i);
		}
	}

	/**
	 * Remove separator at sep_idx and its right child from an internal node.
	 */
	void remove_separator_and_right_child(Internal *parent, uint8_t sep_idx)
	{
		assert(parent != nullptr);
		assert(sep_idx < parent->count);

		/* Shift keys left: [sep_idx + 1..count - 1] -> [sep_idx..count - 2] */
		std::move(parent->keys.begin() + sep_idx + 1, parent->keys.begin() + parent->count, parent->keys.begin() + sep_idx);

		/* Shift children left: [sep_idx + 2..count] -> [sep_idx + 1..count - 1] */
		std::move(parent->children.begin() + sep_idx + 2, parent->children.begin() + parent->count + 1, parent->children.begin() + sep_idx + 1);

		/* Fix parent/index_in_parent pointers for shifted children */
		for (uint8_t j = sep_idx + 1; j < parent->count; ++j) {
			Node *child = parent->children[j].get();
			assert(child != nullptr);
			child->parent = parent;
			child->index_in_parent = j;
		}

		/* Clear the last child slot that is now out of range */
		parent->children[parent->count].reset();

		/* Decrement count (number of separators) */
		--parent->count;

		/* Safety: ensure all children are valid and wired */
		this->rewire_children_parent(parent);

		/* Optional: if parent becomes empty and is root, shrink height elsewhere */
	}

	/**
	 * Prefer a leaf-specific signature to avoid variant unwraps at call sites.
	 */
	inline bool can_merge_leaf(const Leaf *left, const Leaf *right)
	{
		assert(left != nullptr && right != nullptr);
		return (left->count + right->count) <= B; // leaf capacity
	}

	/**
	 * Fix underflow of a leaf child at index i by borrowing or merging from siblings.
	 */
	void fix_underflow_leaf_child(Internal *parent, uint8_t i, iterator &succ_it)
	{
		assert(parent != nullptr);
		assert(i <= parent->count);

		/* Current leaf child */
		Leaf *child = this->get_child_leaf(parent, i);

		/* Try borrow from right sibling */
		if (i < parent->count) {
			Leaf *right = this->get_child_leaf(parent, i + 1);

			if (right->count > MIN_LEAF) {
				this->borrow_from_right_leaf(parent, i, succ_it);
				this->refresh_boundary_upward(parent, i); // right-min changed
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Try borrow from left sibling */
		if (i > 0) {
			Leaf *left = this->get_child_leaf(parent, i - 1);

			if (left->count > MIN_LEAF) {
				this->borrow_from_left_leaf(parent, i, succ_it);
				this->refresh_boundary_upward(parent, i - 1); // leaf-min changed
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Merge path */
		if (i < parent->count) {
			Leaf *right = this->get_child_leaf(parent, i + 1);

			if (this->can_merge_leaf(child, right)) {
				this->merge_leaf_keep_left(parent, i, succ_it);
				if (i < parent->count) {
					this->refresh_boundary_upward(parent, i);
				}
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow from left if possible (second chance) */
			if (i > 0) {
				Leaf *left = this->get_child_leaf(parent, i - 1);

				if (left->count > MIN_LEAF) {
					this->borrow_from_left_leaf(parent, i, succ_it);
					this->refresh_boundary_upward(parent, i - 1);
					this->fix_internal_underflow_cascade(parent);
					return;
				}
			}

			/* Last resort: force merge into left */
			assert(i > 0 && "Right merge overflow and no left sibling to merge into");
			this->merge_leaf_keep_left(parent, i - 1, succ_it);
			if (i - 1 < parent->count) {
				this->refresh_boundary_upward(parent, i - 1);
			}
			this->fix_internal_underflow_cascade(parent);
			return;

		} else {
			/* Rightmost child: must merge into left */
			assert(i > 0);
			Leaf *left = this->get_child_leaf(parent, i - 1);

			if (this->can_merge_leaf(left, child)) {
				this->merge_leaf_keep_left(parent, i - 1, succ_it);
				if (i - 1 < parent->count) {
					this->refresh_boundary_upward(parent, i - 1);
				}
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow from left */
			if (left->count > MIN_LEAF) {
				this->borrow_from_left_leaf(parent, i, succ_it);
				this->refresh_boundary_upward(parent, i - 1);
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			assert(false && "Rightmost leaf underflow: cannot merge or borrow");
		}
	}

	void fix_underflow(Internal *parent, uint8_t i, iterator &succ_it)
	{
		assert(parent != nullptr);
		assert(i <= parent->count);

		Node *child = parent->children[i].get();
		assert(child != nullptr);

		if (child->role == BPlusNodeRole::Leaf) {
			this->fix_underflow_leaf_child(parent, i, succ_it);
		} else {
			this->fix_underflow_internal_child(parent, i);
		}
	}

	/**
	 * Helper to fetch a child internal node from an internal node.
	 */
	Internal *get_child_internal(Internal *parent, uint8_t index)
	{
		assert(parent != nullptr);
		assert(index <= parent->count);

		Node *node = parent->children[index].get();
		assert(node != nullptr);
		assert(node->role == BPlusNodeRole::Internal);

		Internal *internal = static_cast<Internal *>(node);
		assert(internal != nullptr);

		return internal;
	}

	/**
	 * Rotate from the right sibling:
	 * - Move parent.keys[i] down into child at end
	 * - Move right.keys[0] up into parent
	 * - Move right.children[0] into child as new rightmost child
	 */
	void borrow_from_right_internal(Internal *parent, uint8_t i)
	{
		assert(parent != nullptr);
		assert(i < parent->count);

		Internal *child = this->get_child_internal(parent, i);
		Internal *right = this->get_child_internal(parent, i + 1);

		/* Move parent key down into child */
		child->keys[child->count] = std::move(parent->keys[i]);

		/* Move right’s first child into child */
		child->children[child->count + 1] = std::move(right->children[0]);
		if (child->children[child->count + 1] != nullptr) {
			Node *moved = child->children[child->count + 1].get();
			moved->parent = child;
			moved->index_in_parent = child->count + 1;
		}
		++child->count;

		/* Move right’s first key up into parent */
		parent->keys[i] = std::move(right->keys[0]);

		/* Shift right’s keys left */
		std::move(right->keys.begin() + 1, right->keys.begin() + right->count, right->keys.begin());

		/* Shift right’s children left */
		std::move(right->children.begin() + 1, right->children.begin() + right->count + 1, right->children.begin());

		/* Fix parent/index_in_parent pointers for shifted children */
		for (uint8_t c = 0; c < right->count; ++c) {
			Node *child_ptr = right->children[c].get();
			assert(child_ptr != nullptr);
			child_ptr->parent = right;
			child_ptr->index_in_parent = c;
		}

		--right->count;

		/* Rewire parents defensively */
		this->rewire_children_parent(child);
		this->rewire_children_parent(right);
		this->rewire_children_parent(parent);

		/* Refresh boundary */
		this->refresh_boundary_upward(parent, i);
	}

	/**
	 * Rotate from the left sibling:
	 * - Move parent.keys[i-1] down into child at front
	 * - Move left’s last key up into parent
	 * - Move left’s last child into child as new leftmost child
	 */
	void borrow_from_left_internal(Internal *parent, uint8_t i)
	{
		assert(parent != nullptr);
		assert(i > 0 && i <= parent->count);

		Internal *child = this->get_child_internal(parent, i);
		Internal *left = this->get_child_internal(parent, i - 1);

		/* Shift child’s keys right to open slot at 0 */
		std::move_backward(child->keys.begin(), child->keys.begin() + child->count, child->keys.begin() + child->count + 1);

		/* Shift child’s children right to open slot at 0 */
		std::move_backward(child->children.begin(), child->children.begin() + child->count + 1, child->children.begin() + child->count + 2);

		/* Fix parent/index_in_parent pointers for shifted children */
		for (uint8_t c = 1; c <= child->count + 1; ++c) {
			Node *shifted = child->children[c].get();
			if (shifted != nullptr) {
				shifted->parent = child;
				shifted->index_in_parent = c;
			}
		}

		/* Move parent key down into child[0] */
		child->keys[0] = std::move(parent->keys[i - 1]);

		/* Move left’s last child into child[0] */
		child->children[0] = std::move(left->children[left->count]);
		if (child->children[0] != nullptr) {
			Node *moved = child->children[0].get();
			moved->parent = child;
			moved->index_in_parent = 0;
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
	}

	/**
	 * Recipient gets: left.count + 1 (parent sep) + right.count
	 */
	inline bool can_merge_internal(const Internal *left, const Internal *right)
	{
		assert(left != nullptr && right != nullptr);
		return (left->count + 1 + right->count) <= (B - 1);
	}

	/**
	 * Merge internal at i + 1 into internal at i, keep the left internal.
	 * Preconditions: parent->children[i] and parent->children[i + 1] exist and are Internal.
	 */
	void merge_keep_left_internal(Internal *parent, uint8_t i)
	{
		assert(parent != nullptr);
		assert(i < parent->count);

		Internal *left = this->get_child_internal(parent, i);
		Internal *right = this->get_child_internal(parent, i + 1);

		/* Guard: if merge would overflow, fall back to borrow-from-right */
		if (!this->can_merge_internal(left, right)) {
			this->borrow_from_right_internal(parent, i);
			/* Refresh boundary for separator i (points to right subtree) */
			this->refresh_boundary_upward(parent, i);
			return;
		}

		/* Append separator i */
		left->keys[left->count] = std::move(parent->keys[i]);

		/* Move right’s keys into left */
		std::move(right->keys.begin(), right->keys.begin() + right->count, left->keys.begin() + left->count + 1);

		/* Move right’s children into left */
		std::move(right->children.begin(), right->children.begin() + right->count + 1, left->children.begin() + left->count + 1);

		/* Fix parent/index_in_parent pointers for moved children */
		for (uint8_t c = 0; c <= right->count; ++c) {
			Node *moved = left->children[left->count + 1 + c].get();
			assert(moved != nullptr);
			moved->parent = left;
			moved->index_in_parent = left->count + 1 + c;
		}

		left->count += 1 + right->count;

		/* Remove separator i and child i + 1 from parent */
		this->remove_separator_and_right_child(parent, i);

		/* Defensive rewiring */
		this->rewire_children_parent(left);
		this->rewire_children_parent(parent);

		/* Boundary refresh: separator at i now points to the merged right-min,
		 * or if i is out of range, refresh the last separator. */
		if (i < parent->count) {
			this->refresh_boundary_upward(parent, i);
		} else if (parent->count > 0) {
			this->refresh_boundary_upward(parent, parent->count - 1);
		}
	}

	/**
	 * Merge internal at i into internal at i - 1, keep the left internal.
	 * Preconditions: parent->children[i - 1] and parent->children[i] exist and are Internal.
	 */
	void merge_into_left_internal(Internal *parent, uint8_t i)
	{
		assert(parent != nullptr);
		assert(i > 0 && i <= parent->count);

		Internal *left = this->get_child_internal(parent, i - 1);
		Internal *child = this->get_child_internal(parent, i);

		/* Guard: if merge would overflow, fall back to borrow-from-left */
		if (left->count + 1 + child->count > B - 1) {
			this->borrow_from_left_internal(parent, i);
			if (i - 1 < parent->count) {
				this->refresh_boundary_upward(parent, i - 1);
			}
			return;
		}

		/* Append separator i - 1 */
		left->keys[left->count] = std::move(parent->keys[i - 1]);

		/* Move child's keys into left */
		std::move(child->keys.begin(), child->keys.begin() + child->count, left->keys.begin() + left->count + 1);

		/* Move child's children into left */
		std::move(child->children.begin(), child->children.begin() + child->count + 1, left->children.begin() + left->count + 1);

		/* Fix parent/index_in_parent pointers for moved children */
		for (uint8_t c = 0; c <= child->count; ++c) {
			Node *moved = left->children[left->count + 1 + c].get();
			assert(moved != nullptr);
			moved->parent = left;
			moved->index_in_parent = left->count + 1 + c;
		}

		left->count += 1 + child->count;

		/* Remove separator i - 1 and child i from parent */
		this->remove_separator_and_right_child(parent, i - 1);

		/* Defensive rewiring */
		this->rewire_children_parent(left);
		this->rewire_children_parent(parent);

		/* Boundary refresh: separator at i - 1 may now reflect a different right-min,
		 * or if out of range, refresh the last separator. */
		if (i - 1 < parent->count) {
			this->refresh_boundary_upward(parent, i - 1);
		} else if (parent->count > 0) {
			this->refresh_boundary_upward(parent, parent->count - 1);
		}
	}

	/**
	 * Fix underflow when parent’s child at i is an internal node.
	 * Chooses borrow if possible; otherwise merges.
	 */
	void fix_underflow_internal_child(Internal *parent, uint8_t i)
	{
		assert(parent != nullptr);
		assert(i <= parent->count);

		/* Current internal child */
		Internal *child = this->get_child_internal(parent, i);

		/* Borrow from right if possible */
		if (i < parent->count) {
			Internal *right = this->get_child_internal(parent, i + 1);

			if (right->count > MIN_INTERNAL) {
				this->borrow_from_right_internal(parent, i);
				this->refresh_boundary_upward(parent, i); // right-min changed
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Borrow from left if possible */
		if (i > 0) {
			Internal *left = this->get_child_internal(parent, i - 1);

			if (left->count > MIN_INTERNAL) {
				this->borrow_from_left_internal(parent, i);
				this->refresh_boundary_upward(parent, i - 1); // left subtree min may change
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Merge logic */
		if (i < parent->count) {
			Internal *right = this->get_child_internal(parent, i + 1);

			if (this->can_merge_internal(child, right)) {
				this->merge_keep_left_internal(parent, i);
				this->refresh_boundary_upward(parent, i);
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow-left if available (second chance) */
			if (i > 0) {
				Internal *left = this->get_child_internal(parent, i - 1);

				if (left->count > MIN_INTERNAL) {
					this->borrow_from_left_internal(parent, i);
					this->refresh_boundary_upward(parent, i - 1);
					this->fix_internal_underflow_cascade(parent);
					return;
				}
			}

			/* Last resort: mirror merge into left */
			assert(i > 0 && "Internal underflow at i=0 with no feasible borrow/merge");
			this->merge_into_left_internal(parent, i);
			this->refresh_boundary_upward(parent, i - 1);
			this->fix_internal_underflow_cascade(parent);
			return;

		} else {
			/* Rightmost child: must merge into left */
			assert(i > 0);
			Internal *left = this->get_child_internal(parent, i - 1);

			if (this->can_merge_internal(left, child)) {
				this->merge_into_left_internal(parent, i);
				this->refresh_boundary_upward(parent, i - 1);
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow-left */
			if (left->count > MIN_INTERNAL) {
				this->borrow_from_left_internal(parent, i);
				this->refresh_boundary_upward(parent, i - 1);
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			assert(false && "Rightmost internal underflow: cannot merge or borrow");
		}
	}

	/**
	 * Shrink tree height if root is an empty internal node.
	 * Root special case: if root is a leaf, nothing to shrink.
	 */
	void maybe_shrink_height()
	{
		assert(this->root != nullptr);

		/* Case 1: root is a Leaf → nothing to shrink */
		if (this->root->role == BPlusNodeRole::Leaf) {
			return;
		}

		/* Case 2: root is an Internal */
		Internal *root_internal = static_cast<Internal *>(this->root.get());
		assert(root_internal->role == BPlusNodeRole::Internal);

		/* If root has no separators, promote its single child */
		if (root_internal->count == 0) {
			std::unique_ptr<Node> child = std::move(root_internal->children[0]);
			assert(child != nullptr);

			/* Reset child's parent to null (new root) */
			child->parent = nullptr;
			child->index_in_parent = 0;

			this->root = std::move(child);
		} else {
			/* Ensure all children of root are wired correctly */
			this->rewire_children_parent(root_internal);
		}
	}

	/**
	 * Return true if the given internal node is the root
	 */
	bool is_root_internal(Internal *node) const
	{
		return node == static_cast<Internal *>(this->root.get());
	}

	/**
	 * Fetch parent as Internal, with invariant checks
	 */
	Internal *get_parent_internal(Internal *node) const
	{
		assert(node != nullptr);
		assert(node->parent != nullptr);
		assert(node->parent->role == BPlusNodeRole::Internal);

		Internal *parent = static_cast<Internal *>(node->parent);
		assert(parent != nullptr);

		return parent;
	}

	/**
	 * If an internal node underflows, borrow/merge upward until root is handled.
	 * Root special case: if root becomes empty and has one child, promote the child.
	 */
	void fix_internal_underflow_cascade(Internal *node)
	{
		assert(node != nullptr);

		/* Stop at root: shrink height if needed and exit */
		if (this->is_root_internal(node)) {
			this->maybe_shrink_height();
			return;
		}

		Internal *parent = this->get_parent_internal(node);

		/* Find node’s index in parent */
		uint8_t i = this->find_child_index(parent, node);

		/* If node is below minimum, fix it (internal child path) */
		if (node->count < MIN_INTERNAL) {
			this->fix_underflow_internal_child(parent, i);
		}

		/* Defensive note: if parent becomes empty and isn’t root,
		 * its own parent will handle it when reached. */
	}

	/**
	 * Linear search over keys[0..count), returning first index i where keys[i] >= key.
	 */
	static uint8_t lower_bound(const std::array<Tkey, B> &keys, uint8_t count, const Tkey &key)
	{
		for (uint8_t i = 0; i < count; ++i) {
			if (!(keys[i] < key)) { // equivalent to keys[i] >= key
				return i;
			}
		}
		return count;
	}

	/**
	 * Linear search over keys[0..count), returning first index i where keys[i] > key.
	 */
	static uint8_t upper_bound(const std::array<Tkey, B> &keys, uint8_t count, const Tkey &key)
	{
		for (uint8_t i = 0; i < count; ++i) {
			if (key < keys[i]) { // equivalent to keys[i] > key
				return i;
			}
		}
		return count;
	}

#if BPLUSTREE_CHECK
	void validate() const
	{
		assert(this->root != nullptr);

		/* Root invariants */
		if (this->root->role == BPlusNodeRole::Leaf) {
			[[maybe_unused]] const Leaf *rleaf = static_cast<const Leaf *>(this->root.get());
			assert(rleaf->count <= B);
		} else if (this->root->role == BPlusNodeRole::Internal) {
			const Internal *rint = static_cast<const Internal *>(this->root.get());
			assert(rint->count <= B);

			/* Root must have at least one child unless the tree is empty */
			for (uint8_t i = 0; i <= rint->count; ++i) {
				const std::unique_ptr<Node> &ch = rint->children[i];
				assert(ch != nullptr && "null child");

				if (ch->parent != rint) {
					std::cerr << "Child " << i
						<< " has wrong parent " << ch->parent
						<< " expected " << rint << "\n";
					this->dump_node(ch.get(), 2);
					assert(false);
				}
			}
		} else {
			assert(false && "Root must be Leaf or Internal");
		}

		/* Check invariants recursively */
		this->validate_node(this->root.get(), nullptr, nullptr);

		/* Check leaf linkage */
		Leaf *leaf = this->leftmost_leaf();
		Leaf *prev = nullptr;
		while (leaf != nullptr) {
			/* Keys strictly ascending within leaf */
			for (uint8_t i = 1; i < leaf->count; ++i) {
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

	/**
	 * Recursive node validation
	 */
	void validate_node(Node *node, const Tkey *min, const Tkey *max) const
	{
		assert(node != nullptr);

		if (node->role == BPlusNodeRole::Leaf) {
			const Leaf *leaf = static_cast<const Leaf *>(node);

			/* Capacity bounds */
			assert(leaf->count <= B);

			if (leaf->count > 0) {
				/* Keys strictly ascending */
				for (uint8_t i = 1; i < leaf->count; ++i) {
					assert(leaf->keys[i - 1] < leaf->keys[i]);
				}
				/* Range check */
				if (min != nullptr) {
					assert(leaf->keys[0] >= *min);
				}
				if (max != nullptr) {
					assert(leaf->keys[leaf->count - 1] <= *max);
				}
			}
			/* Leaf has no children */
			return;
		}

		assert(node->role == BPlusNodeRole::Internal && "validate_node: node must be Leaf or Internal");
		const Internal *internal = static_cast<const Internal *>(node);

		if (internal->count > 0) {
			/* Internal keys non-decreasing (B+ trees allow equal internal keys via redistribution) */
			for (uint8_t i = 1; i < internal->count; ++i) {
				assert(internal->keys[i - 1] <= internal->keys[i]);
			}
		}

		/* Children count = keys + 1, all non-null and correctly parented */
		for (uint8_t i = 0; i <= internal->count; ++i) {
			[[maybe_unused]] const std::unique_ptr<Node> &ch = internal->children[i];
			assert(ch != nullptr);
			assert(ch->parent == internal);
		}

		/* Separator consistency: parent key == min of right child */
		for (uint8_t i = 0; i < internal->count; ++i) {
			Node *right = internal->children[i + 1].get();
			assert(right != nullptr);

			bool right_has_min = false;
			if (right->role == BPlusNodeRole::Leaf) {
				const Leaf *rleaf = static_cast<const Leaf *>(right);
				right_has_min = (rleaf->count > 0);
			} else if (right->role == BPlusNodeRole::Internal) {
				right_has_min = true; // internal min is defined via subtree
			}
			assert(right_has_min);

			[[maybe_unused]] const Tkey &right_min = this->subtree_min(right);
			assert(internal->keys[i] == right_min);
			this->assert_sep(internal, i);
		}

		/* Recurse into children with updated ranges */
		for (uint8_t i = 0; i <= internal->count; ++i) {
			const Tkey *child_min = min;
			if (i > 0) {
				child_min = &internal->keys[i - 1];
			}
			const Tkey *child_max = max;
			if (i < internal->count) {
				child_max = &internal->keys[i];
			}
			this->validate_node(internal->children[i].get(), child_min, child_max);
		}
	}

	void validate_leaf_chain() const
	{
		Leaf *leaf = this->leftmost_leaf();
		bool has_prev = false;
		Tkey prev{}; // default-constructed sentinel

		while (leaf != nullptr) {
			/* Capacity bounds */
			assert(leaf->count <= B);

			for (uint8_t i = 0; i < leaf->count; ++i) {
				if (has_prev) {
					assert(prev < leaf->keys[i] && "Leaf chain keys must be strictly ascending");
				}
				prev = leaf->keys[i];
				has_prev = true;
			}

			/* Link symmetry check */
			if (leaf->next_leaf != nullptr) {
				assert(leaf->next_leaf->prev_leaf == leaf && "Leaf linkage must be bidirectional");
			}

			leaf = leaf->next_leaf;
		}
	}

	void validate_separators(Node *node) const
	{
		if (node == nullptr) {
			return;
		}

		if (node->role == BPlusNodeRole::Leaf) {
			/* nothing to validate inside a leaf */
			return;
		}

		assert(node->role == BPlusNodeRole::Internal && "validate_separators: node must be Leaf or Internal");
		Internal *internal = static_cast<Internal *>(node);

		for (uint8_t i = 0; i < internal->count; ++i) {
			Node *right = internal->children[i + 1].get();
			assert(right != nullptr);

			const Tkey &right_min = this->subtree_min(right);
			assert(internal->keys[i] == right_min && "Separator must equal min of right subtree");
		}

		for (uint8_t i = 0; i <= internal->count; ++i) {
			this->validate_separators(internal->children[i].get());
		}
	}

	/**
	 * No duplicates across leaves (global check).
	 * Ensures keys are strictly increasing across the entire leaf chain.
	 */
	void assert_no_leaf_duplicates() const
	{
		Leaf *leaf = this->leftmost_leaf();
		bool has_prev = false;
		Tkey prev{}; // sentinel

		while (leaf != nullptr) {
			assert(leaf->count <= B);

			for (uint8_t i = 0; i < leaf->count; ++i) {
				if (has_prev) {
					assert(prev < leaf->keys[i] && "Duplicate or out-of-order key across leaves");
				}
				prev = leaf->keys[i];
				has_prev = true;
			}

			leaf = leaf->next_leaf;
		}
	}

	/**
	 * Recursive verification of parent/index_in_parent invariants
	 */
	void verify_node(Node *n)
	{
		if (n == nullptr) {
			return;
		}

		if (n->role == BPlusNodeRole::Internal) {
			Internal *internal = static_cast<Internal *>(n);
			for (uint8_t j = 0; j <= internal->count; ++j) {
				if (internal->children[j] != nullptr) {
					Node *child = internal->children[j].get();
					assert(child->parent == internal);
					assert(child->index_in_parent == j);
					this->verify_node(child);
				}
			}
		}
	}
#endif /* BPLUSTREE_CHECK */

	const Tkey &subtree_min(Node *node) const
	{
		assert(node != nullptr);

		Node *cur = node;

		/* Descend until we reach a leaf */
		while (cur->role == BPlusNodeRole::Internal) {
			Internal *internal = static_cast<Internal *>(cur);
			assert(internal->children[0] != nullptr);
			cur = internal->children[0].get();
		}

		assert(cur->role == BPlusNodeRole::Leaf);
		Leaf *leaf = static_cast<Leaf *>(cur);
		assert(leaf->count > 0);

		return leaf->keys[0];
	}

	/**
	 * Generic key-to-string helper.
	 * Works for any type K that supports operator<<.
	 */
	template <typename K>
	std::string key_to_string(const K &k) const
	{
		std::ostringstream oss;
		oss << k;
		return oss.str();
	}

	/**
	 * Specialization for std::pair.
	 * Prints as "(first,second)" using key_to_string recursively.
	 */
	template <typename V, typename K>
	std::string key_to_string(const std::pair<V, K> &p) const
	{
		std::ostringstream oss;
		oss << "(" << this->key_to_string(p.first)
			<< "," << this->key_to_string(p.second)
			<< ")";
		return oss.str();
	}

	void assert_sep(const Internal *parent, uint8_t sep_idx) const
	{
		assert(parent != nullptr);
		assert(sep_idx < parent->count);

		Node *right_node = parent->children[sep_idx + 1].get();
		assert(right_node != nullptr);

		const Tkey &sep = parent->keys[sep_idx];
		const Tkey &right_min = this->subtree_min(right_node);

		if (sep != right_min) {
			std::cerr << "[SEP MISMATCH] parent=" << parent
				<< " sep_idx=" << sep_idx
				<< " sep=" << this->key_to_string(sep)
				<< " right_min=" << this->key_to_string(right_min)
				<< " parent.count=" << parent->count
				<< "\n";

			this->dump_node(parent, 0);
			this->dump_node(right_node, 2); // right_node is already a Node *
			assert(false);
		}
	}

public:
	void dump_node(const Node *node = nullptr, int indent = 0) const
	{
		std::string pad(indent, ' ');

		if (node == nullptr) {
			if (indent == 0) {
				node = this->root.get();
			} else {
				std::cerr << pad << "null\n";
				return;
			}
		}

		if (node->role == BPlusNodeRole::Leaf) {
			const Leaf *leaf = static_cast<const Leaf *>(node);

			std::cerr << pad << "Leaf count=" << leaf->count << " keys=[";
			for (uint8_t i = 0; i < leaf->count; ++i) {
				std::cerr << this->key_to_string(leaf->keys[i]);
				if (i + 1 < leaf->count) {
					std::cerr << ",";
				}
			}
			std::cerr << "]\n";

			if constexpr (!std::is_void_v<Tvalue>) {
				std::cerr << pad << "  values=[";
				for (uint8_t i = 0; i < leaf->count; ++i) {
					std::cerr << leaf->values[i];
					if (i + 1 < leaf->count) {
						std::cerr << ",";
					}
				}
				std::cerr << "]\n";
			}

		} else if (node->role == BPlusNodeRole::Internal) {
			const Internal *internal = static_cast<const Internal *>(node);

			std::cerr << pad << "Internal count=" << internal->count << " keys=[";
			for (uint8_t i = 0; i < internal->count; ++i) {
				std::cerr << this->key_to_string(internal->keys[i]);
				if (i + 1 < internal->count) {
					std::cerr << ",";
				}
			}
			std::cerr << "]\n";

			for (uint8_t i = 0; i <= internal->count; ++i) {
				std::cerr << pad << "  child[" << i << "] ->\n";
				this->dump_node(internal->children[i].get(), indent + 4);
			}

			/* Print separators with right.min */
			for (uint8_t i = 0; i < internal->count; ++i) {
				Node *right = internal->children[i + 1].get();
				if (right != nullptr) {
					std::cerr << pad << "  separator[" << i << "]="
						<< this->key_to_string(internal->keys[i])
						<< " (right.min=" << this->key_to_string(this->subtree_min(right)) << ")\n";
				}
			}
		} else {
			std::cerr << pad << "Unknown node role\n";
		}
	}
};

#endif /* BPLUSTREE_TYPE_HPP */
