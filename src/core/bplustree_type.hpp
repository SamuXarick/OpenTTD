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
#	define VALIDATE_NODES() this->validate_all()
#else
	/** Don't check for consistency. */
#	define VALIDATE_NODES() ;
#endif

/**
 * Unified B+ tree template.
 * - If Tvalue != void -> behaves like std::map<Tkey,Tvalue>
 * - If Tvalue == void -> behaves like std::set<Tkey>
 */
template<
	typename Tkey,
	typename Tvalue = void,
	typename Compare = std::less<Tkey>,
	typename Allocator = std::allocator<
	std::conditional_t<std::is_void_v<Tvalue>, Tkey, std::pair<const Tkey, Tvalue>>
	>
>
class BPlusTree {
public:
	using key_type = Tkey;
	using mapped_type = Tvalue;
	using key_compare = Compare;
	using allocator_type = Allocator;

	using value_type = std::conditional_t<
		std::is_void_v<Tvalue>,
		Tkey,
		std::pair<const Tkey, Tvalue>
	>;

private:
	struct NodeBase {
		bool is_leaf;
		uint8_t count = 0;
		uint8_t index_in_parent = 0;
		std::array<Tkey, 64> keys;

		NodeBase(bool leaf) : is_leaf(leaf) {}
		virtual ~NodeBase() = default;
	};

	struct InternalSet;
	struct InternalMap;

	struct NodeSet : NodeBase {
		InternalSet *parent = nullptr;

		explicit NodeSet(bool leaf) : NodeBase(leaf)
		{
		}

		virtual ~NodeSet() = default;
	};

	struct NodeMap : NodeBase {
		InternalMap *parent = nullptr;

		explicit NodeMap(bool leaf) : NodeBase(leaf)
		{
		}

		virtual ~NodeMap() = default;
	};

	using Node = std::conditional_t<std::is_void_v<Tvalue>, NodeSet, NodeMap>;

	struct LeafSet : NodeSet {
		LeafSet *next_leaf = nullptr;
		LeafSet *prev_leaf = nullptr;

		LeafSet() : NodeSet(true) {}
	};

	struct LeafMap : NodeMap {
		LeafMap *next_leaf = nullptr;
		LeafMap *prev_leaf = nullptr;

		std::array<Tvalue, 64> values;

		LeafMap() : NodeMap(true) {}
	};

	using Leaf = std::conditional_t<std::is_void_v<Tvalue>, LeafSet, LeafMap>;

	using NodeAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<std::byte>;

	struct NodeDeleter {
		NodeAllocator *alloc = nullptr;

		NodeDeleter() = default;
		explicit NodeDeleter(NodeAllocator *a) : alloc(a) {}

		void operator()(NodeBase *node) const noexcept; // declaration only
	};

	using NodePtr = std::unique_ptr<NodeBase, NodeDeleter>;

	struct InternalSet : NodeSet {
		std::array<NodePtr, 65> children;

		InternalSet() : NodeSet(false) {}
	};

	struct InternalMap : NodeMap {
		std::array<NodePtr, 65> children;

		InternalMap() : NodeMap(false) {}
	};

	using Internal = std::conditional_t<std::is_void_v<Tvalue>, InternalSet, InternalMap>;

private:
	NodeAllocator node_allocator;
	Compare compare;
	allocator_type allocator;

	NodePtr root;

public:
	BPlusTree(const Compare &comp = Compare(), const Allocator &alloc = Allocator()) :
		node_allocator(alloc),
		compare(comp),
		allocator(alloc),
		root(nullptr, NodeDeleter(&this->node_allocator))
	{
		this->root = this->allocate_leaf();
	}

	~BPlusTree() = default;

	key_compare key_comp() const
	{
		return this->compare;
	}

	allocator_type get_allocator() const
	{
		return this->allocator;
	}

private:
	NodePtr allocate_leaf()
	{
		std::byte *mem = this->node_allocator.allocate(sizeof(Leaf));
		Leaf *leaf = new (mem) Leaf(); // placement new
		return NodePtr(static_cast<NodeBase *>(leaf), NodeDeleter(&this->node_allocator));
	}

	NodePtr allocate_internal()
	{
		std::byte *mem = this->node_allocator.allocate(sizeof(Internal));
		Internal *internal = new (mem) Internal();
		return NodePtr(static_cast<NodeBase *>(internal), NodeDeleter(&this->node_allocator));
	}

public:
	/**
	 * Check if tree contains a key
	 */
	bool contains(const Tkey &key) const
	{
		return this->find(key) != this->end();
	}

	/**
	 * Swap roots between two trees.
	 */
	void swap(BPlusTree &other) noexcept
	{
		this->root.swap(other.root);
		std::swap(this->compare, other.compare);
		std::swap(this->allocator, other.allocator);
	}

	/**
	 * Clear tree: reset to a fresh empty leaf node.
	 */
	void clear() noexcept
	{
		this->root.reset();
		this->root = this->allocate_leaf();
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
				NodeBase *root_base = this->root.get();
				if (root_base != nullptr) {
					root_base->index_in_parent = 0;

					Node *root_node = static_cast<Node *>(root_base);
					root_node->parent = nullptr;
				}

				this->rebuild_leaf_chain(leaves);
			}
		}

		VALIDATE_NODES();
		return *this;
	}

private:
	size_t count_recursive(const NodeBase *node) const
	{
		if (node == nullptr) {
			return 0;
		}

		if (node->is_leaf) {
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

	NodePtr clone_node(const NodeBase *src, Internal *parent, uint8_t slot, std::vector<Leaf *> &leaves)
	{
		if (src->is_leaf) {
			/* Clone leaf node */
			const Leaf *src_leaf = static_cast<const Leaf *>(src);

			NodePtr dst = this->allocate_leaf();
			Leaf *dst_leaf = static_cast<Leaf *>(dst.get());

			dst_leaf->count = src_leaf->count;
			dst_leaf->keys = src_leaf->keys;

			if constexpr (!std::is_void_v<Tvalue>) {
				dst_leaf->values = src_leaf->values;
			}

			dst_leaf->parent = parent;
			dst_leaf->index_in_parent = (parent != nullptr ? slot : 0);

			leaves.push_back(dst_leaf);
			return dst;
		}

		/* Clone internal node */
		const Internal *src_internal = static_cast<const Internal *>(src);

		NodePtr dst = this->allocate_internal();
		Internal *dst_internal = static_cast<Internal *>(dst.get());

		dst_internal->count = src_internal->count;
		dst_internal->keys = src_internal->keys;

		dst_internal->parent = parent;
		dst_internal->index_in_parent = (parent != nullptr ? slot : 0);

		for (uint8_t i = 0; i <= src_internal->count; ++i) {
			if (src_internal->children[i] != nullptr) {
				NodePtr child_clone = this->clone_node(src_internal->children[i].get(), dst_internal, i, leaves);

				dst_internal->children[i] = std::move(child_clone);

				if (!dst_internal->children[i]->is_leaf) {
					Internal *child_internal = static_cast<Internal *>(dst_internal->children[i].get());

					child_internal->parent = dst_internal;
					child_internal->index_in_parent = i;
				}
			} else {
				dst_internal->children[i].reset();
			}
		}

		return dst;
	}

	void rebuild_leaf_chain(std::vector<Leaf *> &leaves)
	{
		if (leaves.empty()) {
			return;
		}

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
	template <typename K, typename V, bool IsVoid = std::is_void_v<V>>
	struct BPlusIteratorTraits;

	/* Set mode: Tvalue = void */
	template <typename K, typename V>
	struct BPlusIteratorTraits<K, V, true> {
		using key_type = K;
		using mapped_type = void;
		using value_type = K;

		using reference = K &;
		using const_reference = const K &;

		using pointer = K *;
		using const_pointer = const K *;
	};

	/* Map mode: Tvalue != void, proxy by value */
	template <typename K, typename V>
	struct BPlusIteratorTraits<K, V, false> {
		using key_type = K;
		using mapped_type = V;
		using value_type = std::pair<const K, V>;

		struct reference {
			const K &first;
			V &second;
		};

		struct const_reference {
			const K &first;
			const V &second;
		};

		using pointer = void; // not implemented yet
		using const_pointer = void; // not implemented yet
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

		iterator() = default;

		iterator(Leaf *leaf, uint8_t index, const BPlusTree *tree) : leaf_(leaf), index_(index), tree_(tree)
		{
		}

		/* Dereference: conditional return type */
		reference operator*() const
		{
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);

			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf_->keys[this->index_]; // set mode
			} else {
				/* proxy_ already bound to current element */
				return reference{this->leaf_->keys[this->index_], this->leaf_->values[this->index_]};
			}
		}

		/* Increment */
		iterator &operator++()
		{
			if (this->leaf_ == nullptr) {
				/* ++end() is a no-op */
				assert(false && "Cannot increment end()");
				return *this;
			}

			++this->index_;

			if (this->index_ >= this->leaf_->count) {
				/* Move to next leaf */
				this->leaf_ = this->leaf_->next_leaf;
				this->index_ = 0;
			}

			return *this;
		}

		/* Decrement */
		iterator &operator--()
		{
			if (this->leaf_ == nullptr) {
				/* Special case: --end() should land on the last element (if any) */
				if (this->tree_->empty()) {
					/* Empty tree: stay at end() */
					return *this;
				}

				Leaf *last = this->tree_->rightmost_leaf();
				assert(last != nullptr);
				assert(last->count > 0);

				this->leaf_ = last;
				this->index_ = last->count - 1;

				return *this;
			}

			if (this->index_ == 0) {
				/* Move to previous leaf */
				this->leaf_ = this->leaf_->prev_leaf;

				if (this->leaf_ != nullptr) {
					assert(this->leaf_->count > 0);
					this->index_ = this->leaf_->count - 1;
				} else {
					assert(false && "Cannot decrement begin()");
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
		using Traits = BPlusIteratorTraits<Tkey, Tvalue>;
		using iterator_category = std::bidirectional_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = typename Traits::value_type;
		using reference = typename Traits::const_reference;
		using pointer = typename Traits::const_pointer;

		const Leaf *leaf_ = nullptr;
		uint8_t index_ = 0;
		const BPlusTree *tree_ = nullptr;

		const_iterator() = default;

		const_iterator(const Leaf *leaf, uint8_t index, const BPlusTree *tree) : leaf_(leaf), index_(index), tree_(tree)
		{
		}

		const_iterator(const iterator &it) : leaf_(it.leaf_), index_(it.index_), tree_(it.tree_)
		{
		}

		/* Dereference: conditional return type */
		reference operator*() const
		{
			assert(this->leaf_ != nullptr);
			assert(this->index_ < this->leaf_->count);
			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf_->keys[this->index_];
			} else {
				return reference{this->leaf_->keys[this->index_], this->leaf_->values[this->index_]};
			}
		}

		/* Increment */
		const_iterator &operator++()
		{
			if (this->leaf_ == nullptr) {
				/* ++end() is a no-op */
				assert(false && "Cannot increment end()");
				return *this;
			}

			++this->index_;

			if (this->index_ >= this->leaf_->count) {
				/* Move to next leaf */
				this->leaf_ = this->leaf_->next_leaf;
				this->index_ = 0;
			}

			return *this;
		}

		/* Decrement */
		const_iterator &operator--()
		{
			if (this->leaf_ == nullptr) {
				/* Special case: --end() should land on the last element (if any) */
				if (this->tree_->empty()) {
					/* Empty tree: stay at end() */
					return *this;
				}

				return *this;
				const Leaf *last = this->tree_->rightmost_leaf();

				assert(last != nullptr);
				assert(last->count > 0);

				this->leaf_ = last;
				this->index_ = last->count - 1;

				return *this;
			}

			if (this->index_ == 0) {
				/* Move to previous leaf */
				this->leaf_ = this->leaf_->prev_leaf;

				if (this->leaf_ != nullptr) {
					assert(this->leaf_->count > 0);
					this->index_ = this->leaf_->count - 1;
				} else {
					assert(false && "Cannot decrement begin()");
				}
			} else {
				--this->index_;
			}

			return *this;
		}

		/* Equality */
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
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

		Leaf *first = this->leftmost_leaf();
		if (first == nullptr || first->count == 0) {
			return this->end();
		}
		return iterator(first, 0, this);
	}

	const_iterator begin() const
	{
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

		const Leaf *first = this->leftmost_leaf();
		if (first == nullptr || first->count == 0) {
			return this->end();
		}
		return const_iterator(first, 0, this);
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
		return const_iterator(nullptr, 0, this);
	}

	iterator find(const Tkey &key)
	{
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

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
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

		const Leaf *leaf = this->find_leaf(key);
		if (leaf == nullptr) {
			return this->end();
		}

		uint8_t i = this->lower_bound(leaf->keys, leaf->count, key);
		if (i < leaf->count && leaf->keys[i] == key) {
			return const_iterator(leaf, i, this);
		}

		return this->end();
	}

	iterator lower_bound(const Tkey &key)
	{
		assert(this->root != nullptr);

		Node *node = static_cast<Node *>(this->root.get());

		/* Descend while we're in an internal node */
		while (!node->is_leaf) {
			Internal *internal = static_cast<Internal *>(node);
			uint8_t i = this->upper_bound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = static_cast<Node *>(internal->children[i].get());
		}

		/* At this point, node must be a leaf */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);

		uint8_t i = this->lower_bound(leaf->keys, leaf->count, key);
		return iterator(leaf, i, this);
	}

	const_iterator lower_bound(const Tkey &key) const
	{
		assert(this->root != nullptr);

		const Node *node = static_cast<const Node *>(this->root.get());

		/* Descend while we're in an internal node */
		while (!node->is_leaf) {
			const Internal *internal = static_cast<const Internal *>(node);
			uint8_t i = this->upper_bound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = static_cast<const Node *>(internal->children[i].get());
		}

		/* At this point, node must be a leaf */
		const Leaf *leaf = static_cast<const Leaf *>(node);
		assert(leaf != nullptr);

		uint8_t i = this->lower_bound(leaf->keys, leaf->count, key);
		return const_iterator(leaf, i, this);
	}

	/**
	 * Map mode emplace
	 */
	template <typename U = Tvalue> requires (!std::is_void_v<U>)
	std::pair<iterator, bool> emplace(const Tkey &key, const U &value)
	{
		return this->emplace_common(key, [&](Leaf *leaf, uint8_t idx) {
			this->insert(leaf, idx, key, value);
		});
	}

	/**
	 * Set mode emplace
	 */
	template <typename U = Tvalue> requires (std::is_void_v<U>)
	std::pair<iterator, bool> emplace(const Tkey &key)
	{
		return this->emplace_common(key, [&](Leaf *leaf, uint8_t idx) {
			this->insert(leaf, idx, key);
		});
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
			assert(leaf->index_in_parent == this->find_child_index(parent, leaf));
			uint8_t child_idx = leaf->index_in_parent;

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
		if (leaf->parent != nullptr && leaf->count < 32) {
			Internal *parent = leaf->parent;
			assert(leaf->index_in_parent == this->find_child_index(parent, leaf));
			uint8_t child_idx = leaf->index_in_parent;

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

		Node *node = static_cast<Node *>(this->root.get());

		/* Descend leftmost children while internal */
		while (!node->is_leaf) {
			Internal *internal = static_cast<Internal *>(node);
			assert(internal->children[0] != nullptr);
			node = static_cast<Node *>(internal->children[0].get());
		}

		/* Must be a leaf now */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	Leaf *rightmost_leaf() const
	{
		assert(this->root != nullptr);

		Node *node = static_cast<Node *>(this->root.get());

		/* Descend rightmost children while internal */
		while (!node->is_leaf) {
			Internal *internal = static_cast<Internal *>(node);
			assert(internal->children[internal->count] != nullptr);
			node = static_cast<Node *>(internal->children[internal->count].get());
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
		[[maybe_unused]] Node *node = static_cast<Node *>(parent->children[child->index_in_parent].get());
		assert(node != nullptr);

		/* Verify that the pointer matches the expected child */
		assert(node == child);

		return child->index_in_parent;
	}

	/**
	 * Common emplace logic: find position, check existence, call do_insert lambda
	 */
	template <typename Tfunc>
	std::pair<iterator, bool> emplace_common(const Tkey &key, Tfunc &&do_insert)
	{
		iterator it = this->lower_bound(key);
		Leaf *leaf = it.leaf_;
		uint8_t idx = it.index_;

		if (idx < leaf->count && leaf->keys[idx] == key) {
			return std::pair<iterator, bool>(it, false); // already exists
		}

		/* Perform insert (may split) */
		do_insert(leaf, idx);

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
			assert(leaf->index_in_parent == this->find_child_index(parent, leaf));
			uint8_t child_idx = leaf->index_in_parent;
			if (child_idx > 0) {
				this->maintain_parent_boundary(parent, child_idx - 1);
			}
		}

		/* Split if leaf is full */
		if (leaf->count == 64) {
			this->split_leaf(leaf);
		}
	}

	/**
	 * Map mode insert
	 */
	template <typename U = Tvalue> requires (!std::is_void_v<U>)
	void insert(Leaf *leaf, uint8_t i, const Tkey &key, const U &value)
	{
		/* Shift values */
		std::move_backward(leaf->values.begin() + i, leaf->values.begin() + leaf->count, leaf->values.begin() + leaf->count + 1);

		leaf->values[i] = value;

		this->insert_common(leaf, i, key);
	}

	/**
	 * Set mode insert
	 */
	template <typename U = Tvalue> requires (std::is_void_v<U>)
	void insert(Leaf *leaf, uint8_t i, const Tkey &key)
	{
		this->insert_common(leaf, i, key);
	}

	/**
	 * Descend the tree to find the leaf containing or suitable for the given key.
	 */
	Leaf *find_leaf(const Tkey &key) const
	{
		assert(this->root != nullptr);

		Node *node = static_cast<Node *>(this->root.get());

		/* Descend while internal */
		while (!node->is_leaf) {
			Internal *internal = static_cast<Internal *>(node);
			uint8_t i = this->upper_bound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = static_cast<Node *>(internal->children[i].get());
		}

		/* Must be a leaf now */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	/**
	 * Verify and rewire children's parent/index_in_parent fields for a given internal node.
	 */
	bool verify_children_parent(const NodeBase *node) const
	{
		if (node == nullptr) {
			return true;
		}

		/* Must be an internal node */
		assert(!node->is_leaf);
		const Internal *internal = static_cast<const Internal *>(node);

		for (uint8_t i = 0; i <= internal->count; ++i) {
			const NodeBase *child_base = internal->children[i].get();
			assert(child_base != nullptr);

			[[maybe_unused]] const Node *child = static_cast<const Node *>(child_base);

			assert(child->parent == internal);
			assert(child->index_in_parent == i);
		}

		return true;
	}

	/**
	 * Split a full leaf into two halves and insert separator into parent.
	 */
	void split_leaf(Leaf *leaf)
	{
		assert(leaf != nullptr);
		uint8_t mid = leaf->count / 2;

		/* Create a new Leaf node */
		NodePtr new_leaf_node = this->allocate_leaf();
		Leaf *new_leaf = static_cast<Leaf *>(new_leaf_node.get());

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
	void insert_into_parent(Node *left, const Tkey &separator, NodePtr right_node)
	{
		/* Parent must be an internal node if it exists */
		Internal *parent = left->parent;
		assert(left->parent == nullptr || !left->parent->is_leaf);

		/* Root case: parent == nullptr -> create fresh internal root */
		if (parent == nullptr) {
			NodePtr new_root_ptr = this->allocate_internal();
			Internal *new_root = static_cast<Internal *>(new_root_ptr.get());

			/* Promote old root (which contains 'left') and right_node under new_root */
			NodePtr old_root = std::move(this->root);

			/* Assert that old_root holds the same node as 'left' */
			assert(old_root.get() == left);

			new_root->keys[0] = separator;
			new_root->children[0] = std::move(old_root);
			new_root->children[1] = std::move(right_node);
			new_root->count = 1;

			/* Fix children's parent/index_in_parent */
			if (new_root->children[0] != nullptr) {
				Node *c0 = static_cast<Node *>(new_root->children[0].get());
				c0->parent = new_root;
				c0->index_in_parent = 0;
			}

			if (new_root->children[1] != nullptr) {
				Node *c1 = static_cast<Node *>(new_root->children[1].get());
				c1->parent = new_root;
				c1->index_in_parent = 1;
			}

			this->root = std::move(new_root_ptr);
			assert(this->verify_children_parent(this->root.get()));
			return;
		}

		/* Parent exists: ensure space */
		if (parent->count == 64) {
			this->split_internal(parent);
			assert(left->parent != nullptr && !left->parent->is_leaf);
			parent = left->parent; // left may have moved
		}

		assert(left->index_in_parent == this->find_child_index(parent, left));
		uint8_t i = left->index_in_parent;

		/* Shift keys and children right */
		std::move_backward(parent->keys.begin() + i, parent->keys.begin() + parent->count, parent->keys.begin() + parent->count + 1);

		std::move_backward(parent->children.begin() + i + 1, parent->children.begin() + parent->count + 1, parent->children.begin() + parent->count + 2);

		/* Refresh children's parent/index_in_parent after shift */
		for (uint8_t j = i + 1; j <= parent->count + 1; ++j) {
			if (parent->children[j] != nullptr) {
				Node *child = static_cast<Node *>(parent->children[j].get());
				child->parent = parent;
				child->index_in_parent = j;
			}
		}

		/* Insert separator and right child */
		parent->keys[i] = separator;
		assert(parent->children[i + 1] == nullptr);
		parent->children[i + 1] = std::move(right_node);

		if (parent->children[i + 1] != nullptr) {
			Node *child = static_cast<Node *>(parent->children[i + 1].get());
			child->parent = parent;
			child->index_in_parent = i + 1;
		}

		++parent->count;
		assert(this->verify_children_parent(parent));
	}

	/**
	 * Split a full internal node into two halves and promote a separator key.
	 */
	void split_internal(Internal *node)
	{
		assert(node != nullptr);
		assert(node->count == 64 && "split_internal called on non-full internal");

		uint8_t mid = node->count / 2;
		assert(mid < node->count);

		/* Separator promoted to parent */
		Tkey separator = node->keys[mid];

		/* Create new right internal node */
		NodePtr right_node = this->allocate_internal();
		Internal *right = static_cast<Internal *>(right_node.get());

		/* Move keys: left keeps [0..mid - 1], right gets [mid + 1..node->count - 1] */
		std::move(node->keys.begin() + mid + 1, node->keys.begin() + node->count, right->keys.begin());

		right->count = node->count - mid - 1;

		/* Move children: left keeps [0..mid], right gets [mid + 1..node->count] */
		std::move(node->children.begin() + mid + 1, node->children.begin() + node->count + 1, right->children.begin());

		/* Fix parent/index_in_parent for moved children in right */
		for (uint8_t j = 0; j <= right->count; ++j) {
			Node *child = static_cast<Node *>(right->children[j].get());
			assert(child != nullptr);
			child->parent = right;
			child->index_in_parent = j;
		}

		/* Left node keeps first mid keys and mid + 1 children */
		node->count = mid;

		/* Clear dangling child slots beyond mid in left */
		std::fill(node->children.begin() + mid + 1, node->children.begin() + 65, nullptr);

		/* Verify left children wiring */
		assert(this->verify_children_parent(node));

		assert(node->count == mid);
		assert(right->count > 0);

		/* Insert separator and right child into parent */
		this->insert_into_parent(node, separator, std::move(right_node));

		/* After insertion, parent's children changed; defensively rewire */
		assert(this->verify_children_parent(node->parent));
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
			assert(p->index_in_parent == this->find_child_index(gp, p));
			uint8_t idx_in_gp = p->index_in_parent;

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
		Node *right = static_cast<Node *>(parent->children[sep_idx + 1].get());
		assert(right != nullptr);

		/* Case 1: right child is a Leaf */
		if (right->is_leaf) {
			Leaf *right_leaf = static_cast<Leaf *>(right);
			assert(right_leaf->count > 0 && "Empty right leaf should have been removed in merge");
			parent->keys[sep_idx] = right_leaf->keys[0];

		/* Case 2: right child is an Internal */
		} else {
			Internal *right_internal = static_cast<Internal *>(right);
			Node *cur = right_internal;

			/* Descend to leftmost leaf of right subtree */
			while (!cur->is_leaf) {
				Internal *internal = static_cast<Internal *>(cur);
				assert(internal->children[0] != nullptr);
				cur = static_cast<Node *>(internal->children[0].get());
			}

			Leaf *leaf = static_cast<Leaf *>(cur);
			assert(leaf != nullptr && leaf->count > 0);
			parent->keys[sep_idx] = leaf->keys[0];
		}
	}

	/**
	 * Borrow one key/value across leaf neighbors, symmetrically.
	 *
	 * @param leaf The recipient leaf.
	 * @param succ_it Iterator potentially impacted by the move.
	 * @param recipient_is_left If true, borrow from right into leaf; if false, borrow from left into leaf.
	 */
	void borrow_leaf(Leaf *leaf, iterator &succ_it, bool recipient_is_left)
	{
		assert(leaf != nullptr);
		Internal *parent = leaf->parent;
		assert(parent != nullptr);

		assert(leaf->index_in_parent == this->find_child_index(parent, leaf));
		uint8_t idx = leaf->index_in_parent;

		if (recipient_is_left) {
			/* Borrow from right into leaf */
			Leaf *right = leaf->next_leaf;
			assert(right != nullptr);
			assert(leaf->count < 64 && right->count > 32);

			/* 1) Prepare slot at end (no shift needed) */

			/* 2) Insert donor extremum: right.min -> leaf[end] */
			leaf->keys[leaf->count] = right->keys[0];
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[leaf->count] = std::move(right->values[0]);
			}

			/* 3) Retarget iterator if it was pointing into right */
			if (succ_it.leaf_ == right) {
				succ_it.leaf_ = leaf;
				succ_it.index_ = leaf->count;
			}

			/* 4) Update counts */
			++leaf->count;
			--right->count;
			assert(right->count >= 32);

			/* 5) Shift donor left */
			std::move(right->keys.begin() + 1, right->keys.begin() + right->count + 1, right->keys.begin());
			if constexpr (!std::is_void_v<Tvalue>) {
				std::move(right->values.begin() + 1, right->values.begin() + right->count + 1, right->values.begin());
			}

			/* 6) Refresh boundary (separator pointing to right changed) */
			this->refresh_boundary_upward(parent, idx);

		} else {
			/* Borrow from left into leaf */
			Leaf *left = leaf->prev_leaf;
			assert(left != nullptr);
			assert(leaf->count < 64 && left->count > 32);

			/* 1) Prepare slot at front (shift leaf right) */
			std::move_backward(leaf->keys.begin(), leaf->keys.begin() + leaf->count, leaf->keys.begin() + leaf->count + 1);
			if constexpr (!std::is_void_v<Tvalue>) {
				std::move_backward(leaf->values.begin(), leaf->values.begin() + leaf->count, leaf->values.begin() + leaf->count + 1);
			}

			/* 2) Insert donor extremum: left.max -> leaf[0] */
			leaf->keys[0] = left->keys[left->count - 1];
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[0] = std::move(left->values[left->count - 1]);
			}

			/* 3) Retarget iterator if it was pointing into leaf (indices shifted + 1) */
			if (succ_it.leaf_ == leaf) {
				++succ_it.index_;
			}

			/* 4) Update counts */
			++leaf->count;
			--left->count;
			assert(left->count >= 32);

			/* 5) Donor shift not needed (removed last element) */

			/* 6) Refresh boundary (separator pointing to leaf changed) */
			assert(idx > 0); // leaf cannot be the 0th child if it has a left sibling
			this->refresh_boundary_upward(parent, idx - 1);
		}
	}

	/**
	 * Merge right (next_leaf) into left, keeping the left leaf.
	 * Structural removal is done on the parent.
	 */
	void merge_keep_left_leaf(Leaf *left, iterator &succ_it)
	{
		assert(left != nullptr);
		Leaf *right = left->next_leaf;
		assert(right != nullptr);

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

		/* Remove the separator and right child from the parent */
		Internal *parent = left->parent;
		assert(parent != nullptr);
		assert(left->index_in_parent == this->find_child_index(parent, left));
		uint8_t idx = left->index_in_parent;
		this->remove_separator_and_child(parent, idx);

		/* After removal, the separator at idx may now reflect a different right-min */
		if (idx < parent->count) {
			this->refresh_boundary_upward(parent, idx);
		}
	}

	/**
	 * Remove separator at sep_idx and its right child from an internal node.
	 */
	void remove_separator_and_child(Internal *parent, uint8_t sep_idx)
	{
		assert(parent != nullptr);
		assert(sep_idx < parent->count);

		/* Shift keys left: [sep_idx + 1..count - 1] -> [sep_idx..count - 2] */
		std::move(parent->keys.begin() + sep_idx + 1, parent->keys.begin() + parent->count, parent->keys.begin() + sep_idx);

		/* Shift children left: [sep_idx + 2..count] -> [sep_idx + 1..count - 1] */
		std::move(parent->children.begin() + sep_idx + 2, parent->children.begin() + parent->count + 1, parent->children.begin() + sep_idx + 1);

		/* Fix parent/index_in_parent pointers for shifted children */
		for (uint8_t j = sep_idx + 1; j < parent->count; ++j) {
			Node *child = static_cast<Node *>(parent->children[j].get());
			assert(child != nullptr);
			child->parent = parent;
			child->index_in_parent = j;
		}

		/* Clear the last child slot that is now out of range */
		parent->children[parent->count].reset();

		/* Decrement count (number of separators) */
		--parent->count;

		/* Safety: ensure all children are valid and wired */
		assert(this->verify_children_parent(parent));

		/* Optional: if parent becomes empty and is root, shrink height elsewhere */
	}

	/**
	 * Prefer a leaf-specific signature to avoid variant unwraps at call sites.
	 */
	inline bool can_merge_leaf(const Leaf *left, const Leaf *right)
	{
		assert(left != nullptr && right != nullptr);
		return left->count + right->count <= 64; // leaf capacity
	}

	/**
	 * Fix underflow of a leaf child by borrowing or merging from siblings.
	 */
	void fix_underflow_leaf_child(Leaf *child, iterator &succ_it)
	{
		assert(child != nullptr);
		Internal *parent = child->parent;
		assert(parent != nullptr);

		assert(child->index_in_parent == this->find_child_index(parent, child));
		const uint8_t i = child->index_in_parent;

		/* Try borrow from right sibling */
		if (i < parent->count) {
			Leaf *right = child->next_leaf;
			assert((right == nullptr) == (i >= parent->count) && "Link/index mismatch");

			if (right != nullptr && right->count > 32) {
				this->borrow_leaf(child, succ_it, /*recipient_is_left=*/true);
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Try borrow from left sibling */
		if (i > 0) {
			Leaf *left = child->prev_leaf;
			assert((left == nullptr) == (i == 0) && "Link/index mismatch");

			if (left != nullptr && left->count > 32) {
				this->borrow_leaf(child, succ_it, /*recipient_is_left=*/false);
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Merge path (i < parent->count => try merging child with right) */
		if (i < parent->count) {
			Leaf *right = child->next_leaf;
			assert((right == nullptr) == (i >= parent->count) && "Link/index mismatch");

			if (right != nullptr && this->can_merge_leaf(child, right)) {
				this->merge_keep_left_leaf(child, succ_it); // merge right into child (left)
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow from left if possible (second chance) */
			if (i > 0) {
				Leaf *left = child->prev_leaf;
				if (left != nullptr && left->count > 32) {
					this->borrow_leaf(child, succ_it, /*recipient_is_left=*/false);
					this->fix_internal_underflow_cascade(parent);
					return;
				}
			}

			/* Last resort: force merge into left */
			assert(i > 0 && "Right merge overflow and no left sibling to merge into");
			Leaf *left = child->prev_leaf;
			assert(left != nullptr);
			this->merge_keep_left_leaf(left, succ_it); // merge child into left
			this->fix_internal_underflow_cascade(parent);
			return;

		} else {
			/* Rightmost child: must merge into left */
			assert(i > 0);
			Leaf *left = child->prev_leaf;
			assert(left != nullptr);

			if (this->can_merge_leaf(left, child)) {
				this->merge_keep_left_leaf(left, succ_it); // merge child into left
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow from left */
			if (left->count > 32) {
				this->borrow_leaf(child, succ_it, /*recipient_is_left=*/false);
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

		Node *child = static_cast<Node *>(parent->children[i].get());
		assert(child != nullptr);

		if (child->is_leaf) {
			Leaf *leaf = static_cast<Leaf *>(child);
			this->fix_underflow_leaf_child(leaf, succ_it);
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

		Node *node = static_cast<Node *>(parent->children[index].get());
		assert(node != nullptr);
		assert(!node->is_leaf);

		Internal *internal = static_cast<Internal *>(node);
		assert(internal != nullptr);

		return internal;
	}

	/**
	 * Borrow across a boundary between two consecutive internal siblings.
	 *
	 * @param parent The parent internal node.
	 * @param left_idx Index of the left sibling in parent->children.
	 * @param recipient_is_left If true, left receives from right; else right receives from left.
	 *
	 * Preconditions:
	 * - left_idx < parent->count
	 * - right sibling is at left_idx + 1
	 */
	void borrow_internal(Internal *parent, uint8_t left_idx, bool recipient_is_left)
	{
		assert(parent != nullptr);
		assert(left_idx < parent->count);

		Internal *left = this->get_child_internal(parent, left_idx);
		Internal *right = this->get_child_internal(parent, left_idx + 1);
		assert(left != nullptr && right != nullptr);

		if (recipient_is_left) {
			/* Left receives from right */

			/* 1) Prepare recipient slot (end, no shift needed) */

			/* 2) Insert parent key into left[end] */
			left->keys[left->count] = std::move(parent->keys[left_idx]);

			/* 3) Move donor's first child into left[end + 1] */
			left->children[left->count + 1] = std::move(right->children[0]);
			if (left->children[left->count + 1] != nullptr) {
				Node *moved = static_cast<Node *>(left->children[left->count + 1].get());
				moved->parent = left;
				moved->index_in_parent = left->count + 1;
			}

			/* 4) Update recipient count */
			++left->count;

			/* 5) Move donor's first key up into parent */
			parent->keys[left_idx] = std::move(right->keys[0]);

			/* 6) Shift donor left to close gap */
			std::move(right->keys.begin() + 1, right->keys.begin() + right->count, right->keys.begin());
			std::move(right->children.begin() + 1, right->children.begin() + right->count + 1, right->children.begin());

			/* 7) Fix parent/index_in_parent pointers for shifted donor children */
			for (uint8_t c = 0; c < right->count; ++c) {
				Node *p = static_cast<Node *>(right->children[c].get());
				if (p != nullptr) {
					p->parent = right;
					p->index_in_parent = c;
				}
			}

			/* 8) Update donor count */
			--right->count;

		} else {
			/* Right receives from left */

			/* 1) Prepare recipient slot (shift right's keys/children right) */
			std::move_backward(right->keys.begin(), right->keys.begin() + right->count, right->keys.begin() + right->count + 1);
			std::move_backward(right->children.begin(), right->children.begin() + right->count + 1, right->children.begin() + right->count + 2);

			/* 2) Fix parent/index_in_parent pointers for shifted recipient children */
			for (uint8_t c = 1; c <= right->count + 1; ++c) {
				Node *p = static_cast<Node *>(right->children[c].get());
				if (p != nullptr) {
					p->parent = right;
					p->index_in_parent = c;
				}
			}

			/* 3) Insert parent key into right[0] */
			right->keys[0] = std::move(parent->keys[left_idx]);

			/* 4) Move donor's last child into right[0] */
			right->children[0] = std::move(left->children[left->count]);
			if (right->children[0] != nullptr) {
				Node *moved = static_cast<Node *>(right->children[0].get());
				moved->parent = right;
				moved->index_in_parent = 0;
			}

			/* 5) Update recipient count */
			++right->count;

			/* 6) Move donor's last key up into parent */
			parent->keys[left_idx] = std::move(left->keys[left->count - 1]);

			/* 7) Update donor count */
			--left->count;
		}

		/* Defensive rewiring (shared) */
		assert(this->verify_children_parent(left));
		assert(this->verify_children_parent(right));
		assert(this->verify_children_parent(parent));

		/* Refresh boundary separator at left_idx */
		this->refresh_boundary_upward(parent, left_idx);
	}

	/**
	 * Recipient gets: left.count + 1 (parent sep) + right.count
	 */
	inline bool can_merge_internal(const Internal *left, const Internal *right)
	{
		assert(left != nullptr && right != nullptr);
		return left->count + 1 + right->count <= 64;
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
			this->borrow_internal(parent, i, /*recipient_is_left=*/true);
			/* Refresh boundary for separator i (points to right subtree) */
			this->refresh_boundary_upward(parent, i);
			return;
		}

		/* Append separator i */
		left->keys[left->count] = std::move(parent->keys[i]);

		/* Move right's keys into left */
		std::move(right->keys.begin(), right->keys.begin() + right->count, left->keys.begin() + left->count + 1);

		/* Move right's children into left */
		std::move(right->children.begin(), right->children.begin() + right->count + 1, left->children.begin() + left->count + 1);

		/* Fix parent/index_in_parent pointers for moved children */
		for (uint8_t c = 0; c <= right->count; ++c) {
			Node *moved = static_cast<Node *>(left->children[left->count + 1 + c].get());
			assert(moved != nullptr);
			moved->parent = left;
			moved->index_in_parent = left->count + 1 + c;
		}

		left->count += 1 + right->count;

		/* Remove separator i and child i + 1 from parent */
		this->remove_separator_and_child(parent, i);

		/* Defensive rewiring */
		assert(this->verify_children_parent(left));
		assert(this->verify_children_parent(parent));

		/* Boundary refresh: separator at i now points to the merged right-min,
		 * or if i is out of range, refresh the last separator. */
		if (i < parent->count) {
			this->refresh_boundary_upward(parent, i);
		} else if (parent->count > 0) {
			this->refresh_boundary_upward(parent, parent->count - 1);
		}
	}

	/**
	 * Fix underflow when parent's child at i is an internal node.
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

			if (right->count > 31) {
				this->borrow_internal(parent, i, /*recipient_is_left=*/true);
				this->refresh_boundary_upward(parent, i); // right-min changed
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Borrow from left if possible */
		if (i > 0) {
			Internal *left = this->get_child_internal(parent, i - 1);

			if (left->count > 31) {
				this->borrow_internal(parent, i - 1, /*recipient_is_left=*/false);
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

				if (left->count > 31) {
					this->borrow_internal(parent, i - 1, /*recipient_is_left=*/false);
					this->refresh_boundary_upward(parent, i - 1);
					this->fix_internal_underflow_cascade(parent);
					return;
				}
			}

			/* Last resort: mirror merge into left */
			assert(i > 0 && "Internal underflow at i=0 with no feasible borrow/merge");
			this->merge_keep_left_internal(parent, i - 1);
			this->refresh_boundary_upward(parent, i - 1);
			this->fix_internal_underflow_cascade(parent);
			return;

		} else {
			/* Rightmost child: must merge into left */
			assert(i > 0);
			Internal *left = this->get_child_internal(parent, i - 1);

			if (this->can_merge_internal(left, child)) {
				this->merge_keep_left_internal(parent, i - 1);
				this->refresh_boundary_upward(parent, i - 1);
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow-left */
			if (left->count > 31) {
				this->borrow_internal(parent, i - 1, /*recipient_is_left=*/false);
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

		/* Case 1: root is a Leaf -> nothing to shrink */
		if (this->root->is_leaf) {
			return;
		}

		/* Case 2: root is an Internal */
		Internal *root_internal = static_cast<Internal *>(this->root.get());
		assert(!root_internal->is_leaf);

		/* If root has no separators, promote its single child */
		if (root_internal->count == 0) {
			NodePtr child = std::move(root_internal->children[0]);
			assert(child != nullptr);

			/* Reset child's parent to null (new root) */
			Node *child_node = static_cast<Node *>(child.get());
			child_node->parent = nullptr;
			child_node->index_in_parent = 0;

			this->root = std::move(child);
			return;
		}

		/* Ensure all children of root are wired correctly */
		assert(this->verify_children_parent(root_internal));
	}

	/**
	 * If an internal node underflows, borrow/merge upward until root is handled.
	 * Root special case: if root becomes empty and has one child, promote the child.
	 */
	void fix_internal_underflow_cascade(Internal *node)
	{
		assert(node != nullptr);

		/* Is the given internal node the root? */
		if (node == static_cast<Internal *>(this->root.get())) {
			/* Stop at root: shrink height if needed and exit */
			this->maybe_shrink_height();
			return;
		}

		assert(node->parent != nullptr);

		Internal *parent = static_cast<Internal *>(node->parent);
		assert(!parent->is_leaf);

		/* Find node's index in parent */
		assert(node->index_in_parent == this->find_child_index(parent, node));
		uint8_t i = node->index_in_parent;

		/* If node is below minimum, fix it (internal child path) */
		if (node->count < 31) {
			this->fix_underflow_internal_child(parent, i);
		}

		/* Defensive note: if parent becomes empty and isn't root,
		 * its own parent will handle it when reached. */
	}

	/**
	 * Linear search over keys[0..count), returning first index i where keys[i] >= key.
	 */
	uint8_t lower_bound(const std::array<Tkey, 64> &keys, uint8_t count, const Tkey &key) const
	{
		for (uint8_t i = 0; i < count; ++i) {
			if (!this->compare(keys[i], key)) {
				return i;
			}
		}
		return count;
	}

	/**
	 * Linear search over keys[0..count), returning first index i where keys[i] > key.
	 */
	uint8_t upper_bound(const std::array<Tkey, 64> &keys, uint8_t count, const Tkey &key) const
	{
		for (uint8_t i = 0; i < count; ++i) {
			if (this->compare(key, keys[i])) { // equivalent to keys[i] > key
				return i;
			}
		}
		return count;
	}

#if BPLUSTREE_CHECK
	/**
	 * Run all validation checks on the tree.
	 * Combines root invariants, recursive node validation,
	 * separator consistency, leaf chain linkage, and parent/child invariants.
	 */
	void validate_all() const
	{
		assert(this->root != nullptr);

		NodeBase *root_base = this->root.get();

		/* Root invariants */
		if (root_base->is_leaf) {
			[[maybe_unused]] const Leaf *rleaf = static_cast<const Leaf *>(root_base);
			assert(rleaf->count <= 64);
		} else {
			const Internal *rint = static_cast<const Internal *>(root_base);
			assert(rint->count <= 64);
			for (uint8_t i = 0; i <= rint->count; ++i) {
				[[maybe_unused]] const Node *ch = static_cast<const Node *>(rint->children[i].get());
				assert(ch != nullptr);
				assert(ch->parent == rint);
			}
		}

		/* Recursive node validation (capacity, ordering, ranges, separator consistency) */
		this->validate_node(root_base);

		/* Leaf chain validation (ordering, linkage, duplicates) */
		this->validate_leaf_chain();

		/* Parent/index_in_parent invariants */
		this->verify_node(root_base);
	}

	/**
	 * Recursive node validation.
	 * Checks capacity, ordering, range constraints, separator consistency,
	 * and recurses into children.
	 */
	void validate_node(const NodeBase *node, const Tkey *min = nullptr, const Tkey *max = nullptr) const
	{
		assert(node != nullptr);

		if (node->is_leaf) {
			const Leaf *leaf = static_cast<const Leaf *>(node);

			/* Capacity bounds */
			assert(leaf->count <= 64);

			if (leaf->count > 0) {
				/* Keys strictly ascending */
				this->assert_strictly_ascending(leaf->keys, leaf->count);

				/* Range check */
				if (min != nullptr) {
					assert(leaf->keys[0] >= *min);
				}
				if (max != nullptr) {
					assert(leaf->keys[leaf->count - 1] <= *max);
				}
			}
			return; // leaf has no children
		}

		const Internal *internal = static_cast<const Internal *>(node);

		/* Capacity bounds */
		assert(internal->count <= 64);

		if (internal->count > 0) {
			/* Internal keys non-decreasing */
			this->assert_non_decreasing(internal->keys, internal->count);
		}

		/* Children count = keys + 1, all non-null and correctly parented */
		this->assert_child_invariants(internal);

		/* Separator consistency: parent key == min of right child */
		for (uint8_t i = 0; i < internal->count; ++i) {
			this->assert_separator_consistency(internal, i);
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

	template <typename T>
	void assert_strictly_ascending([[maybe_unused]] const T &keys, uint8_t count) const
	{
		for (uint8_t i = 1; i < count; ++i) {
			assert(keys[i - 1] < keys[i]);
		}
	}

	template <typename T>
	void assert_non_decreasing([[maybe_unused]] const T &keys, uint8_t count) const
	{
		for (uint8_t i = 1; i < count; ++i) {
			assert(keys[i - 1] <= keys[i]);
		}
	}

	void assert_child_invariants(const Internal *internal) const
	{
		for (uint8_t i = 0; i <= internal->count; ++i) {
			[[maybe_unused]] Node *child = static_cast<Node *>(internal->children[i].get());
			assert(child != nullptr);
			assert(child->parent == internal);
			assert(child->index_in_parent == i);
		}
	}

	void assert_separator_consistency(const Internal *internal, uint8_t i) const
	{
		Node *right = static_cast<Node *>(internal->children[i + 1].get());
		assert(right != nullptr);
		[[maybe_unused]] const Tkey &right_min = this->subtree_min(right);
		assert(internal->keys[i] == right_min && "Separator must equal min of right subtree");
	}

	/**
	 * No duplicates across leaves (global check).
	 * Ensures keys are strictly increasing across the entire leaf chain.
	 */
	void validate_leaf_chain() const
	{
		Leaf *leaf = this->leftmost_leaf();
		Leaf *prev = nullptr;
		Tkey prev_key{}; // default-constructed sentinel
		bool has_prev = false;

		while (leaf != nullptr) {
			/* Capacity bounds */
			assert(leaf->count <= 64);

			/* Keys strictly ascending within leaf */
			this->assert_strictly_ascending(leaf->keys, leaf->count);

			for (uint8_t i = 0; i < leaf->count; ++i) {
				if (has_prev) {
					assert(prev_key < leaf->keys[i] && "Leaf chain keys must be strictly ascending");
				}
				prev_key = leaf->keys[i];
				has_prev = true;
			}

			/* Link symmetry check */
			assert(leaf->prev_leaf == prev);
			assert(prev == nullptr || prev->next_leaf == leaf);

			prev = leaf;
			leaf = leaf->next_leaf;
		}
	}

	/**
	 * Recursive verification of parent/index_in_parent invariants
	 */
	void verify_node(const NodeBase *n) const
	{
		if (n == nullptr) {
			return;
		}

		if (!n->is_leaf) {
			const Internal *internal = static_cast<const Internal *>(n);

			for (uint8_t j = 0; j <= internal->count; ++j) {
				if (internal->children[j] != nullptr) {
					const NodeBase *child_base = internal->children[j].get();
					const Node *child = static_cast<const Node *>(child_base);
					assert(child->parent == internal);
					assert(child->index_in_parent == j);
					this->verify_node(child);
				}
			}
		}
	}
#endif /* BPLUSTREE_CHECK */

	/**
	 * Return the minimum key in the subtree rooted at node.
	 */
	const Tkey &subtree_min(Node *node) const
	{
		assert(node != nullptr);

		Node *cur = node;

		/* Descend until we reach a leaf */
		while (!cur->is_leaf) {
			Internal *internal = static_cast<Internal *>(cur);
			assert(internal->children[0] != nullptr);
			cur = static_cast<Node *>(internal->children[0].get());
		}

		Leaf *leaf = static_cast<Leaf *>(cur);
		assert(leaf->count > 0);

		return leaf->keys[0];
	}

	/**
	 * Generic key-to-string helper.
	 * Works for any type K that supports operator<<.
	 * Special handling for pair-like types.
	 */
	template <typename T>
	std::string sequence_to_string(const T &key) const
	{
		std::ostringstream oss;
		if constexpr (requires { key.first; key.second; }) {
			oss << "(" << this->sequence_to_string(key.first)
				<< "," << this->sequence_to_string(key.second) << ")";
		} else {
			oss << key;
		}
		return oss.str();
	}

	/**
	 * Separator assertion with detailed error reporting.
	 */
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
				<< " sep=" << this->sequence_to_string(sep)
				<< " right_min=" << this->sequence_to_string(right_min)
				<< " parent.count=" << parent->count << "\n";
			this->dump_node(parent, 0);
			this->dump_node(right_node, 2);
			assert(false);
		}
	}

	/**
	 * Utility to print keys and values in a uniform way.
	 */
	template <typename T>
	void dump_sequence(const T &sequence, uint8_t count) const
	{
		for (uint8_t i = 0; i < count; ++i) {
			std::cerr << this->sequence_to_string(sequence[i]);
			if (i + 1 < count) {
				std::cerr << ",";
			}
		}
	}

public:
	/**
	 * Dump a node and its subtree for debugging.
	 */
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

		if (node->is_leaf) {
			const Leaf *leaf = static_cast<const Leaf *>(node);
			std::cerr << pad << "Leaf count=" << leaf->count << " keys=[";
			this->dump_sequence(leaf->keys, leaf->count);
			std::cerr << "]\n";

			if constexpr (!std::is_void_v<Tvalue>) {
				std::cerr << pad << "  values=[";
				this->dump_sequence(leaf->values, leaf->count);
				std::cerr << "]\n";
			}

		} else {
			const Internal *internal = static_cast<const Internal *>(node);
			std::cerr << pad << "Internal count=" << internal->count << " keys=[";
			this->dump_sequence(internal->keys, internal->count);
			std::cerr << "]\n";

			for (uint8_t i = 0; i <= internal->count; ++i) {
				std::cerr << pad << "  child[" << i << "] ->\n";
				this->dump_node(internal->children[i].get(), indent + 4);
			}

			for (uint8_t i = 0; i < internal->count; ++i) {
				Node *right = internal->children[i + 1].get();
				if (right != nullptr) {
					std::cerr << pad << "  separator[" << i << "]="
						<< this->sequence_to_string(internal->keys[i])
						<< " (right.min=" << this->sequence_to_string(this->subtree_min(right)) << ")\n";
				}
			}
		}
	}
};

template<typename Tkey, typename Tvalue, typename Compare, typename Allocator>
void BPlusTree<Tkey, Tvalue, Compare, Allocator>::NodeDeleter::operator()(NodeBase *node) const noexcept
{
	if (node == nullptr) {
		return;
	}

	if (node->is_leaf) {
		Leaf *leaf = static_cast<Leaf *>(node);
		leaf->~Leaf();
		this->alloc->deallocate(reinterpret_cast<std::byte *>(leaf), sizeof(Leaf));
	} else {
		Internal *internal = static_cast<Internal *>(node);
		internal->~Internal();
		this->alloc->deallocate(reinterpret_cast<std::byte *>(internal), sizeof(Internal));
	}
}

#endif /* BPLUSTREE_TYPE_HPP */
