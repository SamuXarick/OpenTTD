/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file bplustree_type.hpp BPlusTree container implementation, tailored for ScriptList. */

#ifndef BPLUSTREE_TYPE_HPP
#define BPLUSTREE_TYPE_HPP

/**
 * @brief Header-only B+ tree container used as an alternative to std::map / std::set.
 *
 * This file implements a unified B+ tree template supporting both map-like and set-like
 * semantics. When @c Tvalue is @c void the container behaves like a sorted set of keys.
 * When @c Tvalue is non-void the container behaves like a sorted associative map storing
 * @c std::pair<const Tkey, Tvalue>.
 *
 * The implementation uses fixed-size nodes (64 keys per node) and maintains a linked
 * chain of leaf nodes for fast in-order iteration. All operations preserve the standard
 * B+ tree invariants:
 *  - All keys are stored in leaves; internal nodes only store separator keys.
 *  - Internal nodes with @c count separators have @c count + 1 children.
 *  - Leaves are kept between 32 and 64 keys (except the root).
 *  - Internal nodes are kept between 31 and 64 separators (except the root).
 *  - Separator keys always equal the minimum key of their right subtree.
 *  - Leaves form a doubly-linked list in strictly ascending key order.
 *
 * The container provides:
 *  - bidirectional iterators with stable semantics across insert/erase,
 *  - logarithmic search, insertion, and deletion,
 *  - contiguous leaf scans for fast range iteration,
 *  - allocator support compatible with OpenTTD's memory model,
 *  - optional deep validation of all invariants (BPLUSTREE_CHECK).
 *
 * This implementation is designed for clarity, maintainability, and predictable
 * performance characteristics inside OpenTTD's codebase.
 *
 * @see BPlusTree
 */

/**
 * @section bplustree_design_rationale Design Rationale
 *
 * The design of this B+ tree balances three competing goals:
 *  - predictable performance,
 *  - ease of maintenance,
 *  - strict structural correctness.
 *
 * @subsection node_layout Node Layout
 * Nodes use fixed-size @c std::array buffers (64 keys per node). This avoids dynamic
 * allocation inside nodes, improves cache locality, and simplifies split/merge logic.
 * The capacity of 64 was chosen as a practical balance between:
 *  - reducing tree height,
 *  - keeping nodes small enough to fit comfortably in cache,
 *  - simplifying boundary conditions.
 *
 * @subsection leaf_chain Leaf Chain
 * Leaves form a doubly-linked list. This enables:
 *  - O(1) increment/decrement of iterators,
 *  - fast range scans,
 *  - efficient lower_bound() followed by sequential iteration.
 *
 * The leaf chain is rebuilt only during cloning; all other operations maintain it
 * incrementally.
 *
 * @subsection parent_pointers Parent Pointers
 * Every node stores a pointer to its parent and its @c index_in_parent. This allows:
 *  - O(1) upward navigation,
 *  - simple and reliable separator maintenance,
 *  - efficient underflow repair without recursive searches.
 *
 * @subsection invariants Invariant Enforcement
 * The implementation centralizes invariant maintenance:
 *  - @c MaintainBoundaryLocal and @c MaintainBoundaryUpward keep separators correct.
 *  - @c FixUnderflowLeafChild and @c FixUnderflowInternalChild handle borrow/merge.
 *  - @c ReindexChildren ensures parent/child wiring is always consistent.
 *
 * When @c BPLUSTREE_CHECK is enabled, @c ValidateAll() performs:
 *  - full subtree validation,
 *  - separator consistency checks,
 *  - leaf chain ordering checks,
 *  - parent/child pointer correctness checks.
 *
 * @subsection iterator_design Iterator Design
 * Iterators store:
 *  - a pointer to the current leaf,
 *  - an index within that leaf,
 *  - a pointer back to the tree.
 *
 * This design ensures:
 *  - stable iterators across most structural changes,
 *  - O(1) increment/decrement,
 *  - no need for proxy objects except in map mode.
 *
 * @subsection allocator_support Allocator Support
 * The tree uses two allocators:
 *  - @c Allocator for values (map mode),
 *  - a rebound allocator for node storage.
 *
 * This matches STL conventions and OpenTTD's allocator expectations.
 *
 * @subsection header_only Why Header-Only?
 * The template is fully defined in this header to:
 *  - simplify integration into OpenTTD,
 *  - avoid linker complications with templates,
 *  - keep implementation and documentation together.
 */

/** Enable it if you suspect b+ tree doesn't work well. */
#define BPLUSTREE_CHECK 0

#if BPLUSTREE_CHECK
	/** Validate nodes after insert / erase. */
#	define VALIDATE_NODES() this->ValidateAll()
#else
	/** Don't check for consistency. */
#	define VALIDATE_NODES() ;
#endif

/**
 * @brief Unified B+ tree template.
 *
 * Behaves as a map or set depending on @p Tvalue:
 * - If @p Tvalue is not void, the tree behaves like std::map<Tkey, Tvalue>.
 * - If @p Tvalue is void, the tree behaves like std::set<Tkey>.
 *
 * Nodes are fixed-size with a maximum of 64 keys and 65 children per internal
 * node. Leaf nodes form a doubly-linked chain for efficient iteration over
 * key ranges. Memory for nodes is obtained from a rebound allocator type
 * derived from @p Allocator and managed via unique_ptr with a custom deleter.
 *
 * Complexity:
 * - Lookup: O(log n)
 * - Insert: O(log n)
 * - Erase: O(log n)
 * - Iteration: O(n)
 *
 * @tparam Tkey       Key type. Must be totally ordered via operator<.
 * @tparam Tvalue     Mapped type. Use void for set-like mode.
 * @tparam Allocator  Allocator used for node storage and value storage.
 */
template <typename Tkey, typename Tvalue = void, typename Allocator = std::allocator<std::conditional_t<std::is_void_v<Tvalue>, Tkey, std::pair<const Tkey, Tvalue>>>>
class BPlusTree {
private:
	/** Forward declaration for parent pointer of nodes. */
	struct Internal;

	/**
	 * @brief Common node header for both internal and leaf nodes.
	 *
	 * @invariant is_leaf determines the dynamic node type.
	 * @invariant count is the number of valid keys in the node.
	 * @invariant index_in_parent is the index in the parent's children[] array.
	 * @invariant parent is nullptr only for the root.
	 */
	struct Node {
		bool is_leaf;                    ///< True if this node is a leaf.
		uint8_t count = 0;               ///< Number of valid keys in the node.
		uint8_t index_in_parent = 0;     ///< Index into parent->children[].
		Internal *parent = nullptr;      ///< Parent internal node, nullptr for root.
		std::array<Tkey, 64> keys;       ///< Stored keys in sorted order.

		explicit Node(bool leaf) : is_leaf(leaf) {}
	};

	/**
	 * @brief Storage for leaf values, specialized on whether Tvalue is void.
	 *
	 * In set mode (Tvalue = void), this is an empty base class (EBO).
	 * In map mode, this holds a fixed-size array of mapped values.
	 *
	 * @tparam V      Mapped value type.
	 * @tparam IsVoid True if V is void (set mode).
	 */
	template <typename V, bool IsVoid = std::is_void_v<V>>
	struct LeafValueStorage;

	/** @brief Set mode: no values array (empty base, zero-cost). */
	template <typename V>
	struct LeafValueStorage<V, true> {
		/* empty - EBO makes this zero-cost */
	};

	/** @brief Map mode: leaf nodes store values parallel to keys. */
	template <typename V>
	struct LeafValueStorage<V, false> {
		std::array<V, 64> values; ///< Values aligned with keys[0..count-1].
	};

	/**
	 * @brief Leaf node in the B+ tree.
	 *
	 * Stores keys and optionally values, and participates in the doubly-linked
	 * leaf chain used for iteration.
	 *
	 * @invariant keys[0..count-1] are strictly ascending.
	 * @invariant next_leaf / prev_leaf form a consistent bi-directional chain.
	 */
	struct Leaf : Node, LeafValueStorage<Tvalue> {
		Leaf *next_leaf = nullptr; ///< Next leaf in key order.
		Leaf *prev_leaf = nullptr; ///< Previous leaf in key order.

		Leaf() : Node(true) {}
	};

	/** Allocator used for raw node storage (bytes). */
	using NodeAllocator = typename std::allocator_traits<Allocator>::template rebind_alloc<std::byte>;

	/**
	 * @brief Custom deleter for node unique_ptr.
	 *
	 * Stores a pointer to the node allocator and destroys/deallocates nodes
	 * using the correct concrete node type (Leaf or Internal).
	 */
	struct NodeDeleter {
		NodeAllocator *allocator = nullptr; ///< Allocator used for node memory.

		NodeDeleter() = default;
		explicit NodeDeleter(NodeAllocator *allocator) : allocator(allocator) {}

		void operator()(Node *node) const noexcept; ///< Destroy and deallocate @p node.
	};

	/** Ownership type for nodes with custom deleter. */
	using NodePtr = std::unique_ptr<Node, NodeDeleter>;

	/**
	 * @brief Internal node in the B+ tree.
	 *
	 * Holds keys and child pointers. For @p count keys, there are @p count + 1
	 * active children in the @p children array.
	 *
	 * @invariant children[0..count] are non-null.
	 * @invariant children[count+1..64] are null.
	 */
	struct Internal : Node {
		std::array<NodePtr, 65> children; ///< Children pointers, size = keys + 1.

		Internal() : Node(false) {}
	};

	NodeAllocator node_allocator; ///< Allocator used for node storage.
	Allocator allocator;          ///< Allocator used for value/mapped storage.

	NodePtr root;                 ///< Root node of the tree (leaf or internal).

	NodePtr AllocateLeaf()
	{
		std::byte *mem = this->node_allocator.allocate(sizeof(Leaf));
		Leaf *leaf = new (mem) Leaf();
		return NodePtr(static_cast<Node *>(leaf), NodeDeleter(&this->node_allocator));
	}

	NodePtr AllocateInternal()
	{
		std::byte *mem = this->node_allocator.allocate(sizeof(Internal));
		Internal *internal = new (mem) Internal();
		return NodePtr(static_cast<Node *>(internal), NodeDeleter(&this->node_allocator));
	}

public:
	/**
	 * @brief Construct an empty B+ tree.
	 *
	 * Allocates an initial empty leaf as the root and stores copies of
	 * the supplied allocator for node and value allocation.
	 *
	 * @param allocator Allocator instance used for node and value storage.
	 */
	BPlusTree(const Allocator &allocator = Allocator()) :
		node_allocator(allocator),
		allocator(allocator),
		root(nullptr, NodeDeleter(&this->node_allocator))
	{
		this->root = this->AllocateLeaf();
	}

	~BPlusTree() = default;

	/**
	 * @brief Check whether the tree contains a key.
	 *
	 * Performs a lookup equivalent to @c find(key) != end().
	 *
	 * @param key Key to look up.
	 * @return True if @p key is present, false otherwise.
	 */
	bool contains(const Tkey &key) const
	{
		return this->find(key) != this->end();
	}

	/**
	 * @brief Swap roots and allocators between two trees.
	 *
	 * Swaps the internal root pointer and allocator state with @p other.
	 * Node ownership is exchanged; no individual node is moved.
	 *
	 * @param other Other BPlusTree to swap with.
	 */
	void swap(BPlusTree &other) noexcept
	{
		this->root.swap(other.root);
		std::swap(this->allocator, other.allocator);
	}

	/**
	 * @brief Clear the tree and reset to a fresh empty leaf node.
	 *
	 * Destroys the entire tree structure and replaces the root with a new,
	 * empty leaf node. All iterators are invalidated.
	 */
	void clear() noexcept
	{
		this->root.reset();
		this->root = this->AllocateLeaf();
	}

	/**
	 * @brief Check whether the tree is empty.
	 *
	 * @return True if the tree contains no keys, false otherwise.
	 */
	bool empty() const noexcept
	{
		assert(this->root != nullptr);

		return this->root->count == 0;
	}

	/**
	 * @brief Return the number of keys stored in the tree.
	 *
	 * This method recursively counts keys in all leaves. It is O(n) and
	 * primarily intended for debugging and validation.
	 *
	 * @return Total number of keys stored.
	 */
	size_t size() const noexcept
	{
		assert(this->root != nullptr);

		return this->CountRecursive(this->root.get());
	}

	/**
	 * @brief Copy constructor.
	 *
	 * Constructs this tree as a deep copy of @p other.
	 * Node structure, leaf chain, and parent pointers are all rebuilt.
	 *
	 * @param other Tree to copy from.
	 */
	BPlusTree(const BPlusTree &other)
	{
		this->CopyFrom(other);
	}

	/**
	 * @brief Copy-assignment operator.
	 *
	 * Replaces the contents of this tree with a deep copy of @p other.
	 * Node structure, leaf chain, and parent pointers are all rebuilt.
	 *
	 * @param other Tree to copy from.
	 * @return Reference to this tree.
	 */
	BPlusTree &operator=(const BPlusTree &other)
	{
		if (this != &other) {
			this->CopyFrom(other);
		}
		return *this;
	}

private:
	size_t CountRecursive(const Node *node) const
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
				total += this->CountRecursive(internal->children[i].get());
			}
			return total;
		}
	}

	/**
	 * @brief Internal helper to deep-copy the contents of @p other into this tree.
	 *
	 * Rebuilds the entire node structure, parent pointers, and leaf chain.
	 * Assumes the current tree has already been cleared/reset by the caller.
	 *
	 * @param other Source tree to copy from.
	 */
	void CopyFrom(const BPlusTree &other)
	{
		this->root.reset();

		if (other.root != nullptr) {
			std::vector<Leaf *> leaves;
			this->root = this->CloneNode(other.root.get(), nullptr, 0, leaves);

			/* Root invariants: parent = nullptr, index_in_parent = 0 */
			Node *root_node = this->root.get();
			assert(root_node != nullptr);
			this->SetParent(root_node, nullptr, 0);

			this->RebuildLeafChain(leaves);
		}

		VALIDATE_NODES();
	}

	NodePtr CloneNode(const Node *src, Internal *parent, uint8_t slot, std::vector<Leaf *> &leaves)
	{
		if (src->is_leaf) {
			/* Clone leaf node */
			const Leaf *src_leaf = static_cast<const Leaf *>(src);

			NodePtr dst = this->AllocateLeaf();
			Leaf *dst_leaf = static_cast<Leaf *>(dst.get());

			dst_leaf->count = src_leaf->count;
			dst_leaf->keys = src_leaf->keys;

			if constexpr (!std::is_void_v<Tvalue>) {
				dst_leaf->values = src_leaf->values;
			}

			this->SetParent(dst_leaf, parent, parent != nullptr ? slot : 0);

			leaves.push_back(dst_leaf);
			return dst;
		}

		/* Clone internal node */
		const Internal *src_internal = static_cast<const Internal *>(src);

		NodePtr dst = this->AllocateInternal();
		Internal *dst_internal = static_cast<Internal *>(dst.get());

		dst_internal->count = src_internal->count;
		dst_internal->keys = src_internal->keys;

		this->SetParent(dst_internal, parent, parent != nullptr ? slot : 0);

		for (uint8_t i = 0; i <= src_internal->count; ++i) {
			if (src_internal->children[i] != nullptr) {
				NodePtr child_clone = this->CloneNode(src_internal->children[i].get(), dst_internal, i, leaves);

				dst_internal->children[i] = std::move(child_clone);

				if (!dst_internal->children[i]->is_leaf) {
					Internal *child_internal = static_cast<Internal *>(dst_internal->children[i].get());

					this->SetParent(child_internal, dst_internal, i);
				}
			} else {
				dst_internal->children[i].reset();
			}
		}

		return dst;
	}

	void RebuildLeafChain(std::vector<Leaf *> &leaves)
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
	 * @brief Iterator traits for map vs set modes.
	 *
	 * Specializes key/value/reference/pointer types used by iterators,
	 * depending on whether Tvalue is void.
	 *
	 * @tparam K Key type.
	 * @tparam V Value type.
	 * @tparam IsVoid True if V is void.
	 */
	template <typename K, typename V, bool IsVoid = std::is_void_v<V>>
	struct BPlusIteratorTraits;

	/** @brief Iterator traits for set mode (Tvalue = void). */
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

	/** @brief Iterator traits for map mode (Tvalue != void) using proxy types. */
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

		struct pointer {
			reference ref;

			const reference *operator->() const { return &ref; }
			reference *operator->() { return &ref; }
		};

		struct const_pointer {
			const_reference ref;

			const const_reference *operator->() const { return &ref; }
			const_reference *operator->() { return &ref; }
		};
	};

public:
	/**
	 * @brief Bidirectional iterator over the tree.
	 *
	 * In set mode, dereferences to @c Tkey. In map mode, dereferences to
	 * a proxy reference with @c first and @c second members behaving like
	 * std::pair<const Tkey, Tvalue>.
	 *
	 * Iterators remain valid across non-structural modifications but are
	 * invalidated by insertions or erasures that split/merge nodes.
	 */
	struct iterator {
		using Traits = BPlusIteratorTraits<Tkey, Tvalue>;
		using iterator_category = std::bidirectional_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = typename Traits::value_type;
		using reference = typename Traits::reference;
		using pointer = typename Traits::pointer;

		Leaf *leaf = nullptr;         ///< Current leaf node.
		uint8_t index = 0;            ///< Index within leaf->keys[0..count-1].
		const BPlusTree *tree = nullptr; ///< Owning tree, for end()/--end() handling.

		iterator() = default;

		iterator(Leaf *leaf, uint8_t index, const BPlusTree *tree) : leaf(leaf), index(index), tree(tree) {}

		/* Dereference: conditional return type */
		reference operator*() const
		{
			assert(this->leaf != nullptr);
			assert(this->index < this->leaf->count);

			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf->keys[this->index]; // set mode
			} else {
				/* proxy already bound to current element */
				return reference{this->leaf->keys[this->index], this->leaf->values[this->index]};
			}
		}

		[[nodiscard]] pointer operator->() const
		{
			assert(this->leaf != nullptr);
			assert(this->index < this->leaf->count);

			if constexpr (std::is_void_v<Tvalue>) {
				return &this->leaf->keys[this->index];
			} else {
				return pointer{reference{this->leaf->keys[this->index], this->leaf->values[this->index]}};
			}
		}

		/* Increment */
		iterator &operator++()
		{
			if (this->leaf == nullptr) {
				/* ++end() is a no-op */
				assert(false && "Cannot increment end()");
				return *this;
			}

			++this->index;

			if (this->index >= this->leaf->count) {
				/* Move to next leaf */
				this->leaf = this->leaf->next_leaf;
				this->index = 0;
			}

			return *this;
		}

		/* Decrement */
		iterator &operator--()
		{
			if (this->leaf == nullptr) {
				/* Special case: --end() should land on the last element (if any) */
				if (this->tree->empty()) {
					/* Empty tree: stay at end() */
					return *this;
				}

				Leaf *last = this->tree->RightmostLeaf();
				assert(last != nullptr);
				assert(last->count > 0);

				this->leaf = last;
				this->index = last->count - 1;

				return *this;
			}

			if (this->index == 0) {
				/* Move to previous leaf */
				this->leaf = this->leaf->prev_leaf;

				if (this->leaf != nullptr) {
					assert(this->leaf->count > 0);
					this->index = this->leaf->count - 1;
				} else {
					assert(false && "Cannot decrement begin()");
				}
			} else {
				--this->index;
			}

			return *this;
		}

		/* Equality */
		friend bool operator==(const iterator &a, const iterator &b)
		{
			return a.leaf == b.leaf && a.index == b.index && a.tree == b.tree;
		}

		friend bool operator!=(const iterator &a, const iterator &b)
		{
			return !(a == b);
		}
	};

	/**
	 * @brief Constant bidirectional iterator over the tree.
	 *
	 * Behaves like @c iterator but yields const references / proxies.
	 */
	struct const_iterator {
		using Traits = BPlusIteratorTraits<Tkey, Tvalue>;
		using iterator_category = std::bidirectional_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = typename Traits::value_type;
		using reference = typename Traits::const_reference;
		using pointer = typename Traits::const_pointer;

		const Leaf *leaf = nullptr;     ///< Current leaf node.
		uint8_t index = 0;              ///< Index within leaf->keys[0..count-1].
		const BPlusTree *tree = nullptr; ///< Owning tree, for end()/--end() handling.

		const_iterator() = default;

		const_iterator(const Leaf *leaf, uint8_t index, const BPlusTree *tree) : leaf(leaf), index(index), tree(tree) {}

		const_iterator(const iterator &it) : leaf(it.leaf), index(it.index), tree(it.tree) {}

		/* Dereference: conditional return type */
		reference operator*() const
		{
			assert(this->leaf != nullptr);
			assert(this->index < this->leaf->count);

			if constexpr (std::is_void_v<Tvalue>) {
				return this->leaf->keys[this->index];
			} else {
				return reference{this->leaf->keys[this->index], this->leaf->values[this->index]};
			}
		}

		[[nodiscard]] pointer operator->() const
		{
			assert(this->leaf != nullptr);
			assert(this->index < this->leaf->count);

			if constexpr (std::is_void_v<Tvalue>) {
				return &this->leaf->keys[this->index];
			} else {
				return pointer{reference{this->leaf->keys[this->index], this->leaf->values[this->index]}};
			}
		}

		/* Increment */
		const_iterator &operator++()
		{
			if (this->leaf == nullptr) {
				/* ++end() is a no-op */
				assert(false && "Cannot increment end()");
				return *this;
			}

			++this->index;

			if (this->index >= this->leaf->count) {
				/* Move to next leaf */
				this->leaf = this->leaf->next_leaf;
				this->index = 0;
			}

			return *this;
		}

		/* Decrement */
		const_iterator &operator--()
		{
			if (this->leaf == nullptr) {
				/* Special case: --end() should land on the last element (if any) */
				if (this->tree->empty()) {
					/* Empty tree: stay at end() */
					return *this;
				}

				const Leaf *last = this->tree->RightmostLeaf();
				assert(last != nullptr);
				assert(last->count > 0);

				this->leaf = last;
				this->index = last->count - 1;

				return *this;
			}

			if (this->index == 0) {
				/* Move to previous leaf */
				this->leaf = this->leaf->prev_leaf;

				if (this->leaf != nullptr) {
					assert(this->leaf->count > 0);
					this->index = this->leaf->count - 1;
				} else {
					assert(false && "Cannot decrement begin()");
				}
			} else {
				--this->index;
			}

			return *this;
		}

		/* Equality */
		friend bool operator==(const const_iterator &a, const const_iterator &b)
		{
			return a.leaf == b.leaf && a.index == b.index && a.tree == b.tree;
		}

		friend bool operator!=(const const_iterator &a, const const_iterator &b)
		{
			return !(a == b);
		}
	};

	/**
	 * @brief Return iterator to the first element in the tree.
	 *
	 * If the tree is empty, returns end().
	 *
	 * @return Iterator to the smallest key, or end() if empty.
	 */
	iterator begin()
	{
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

		Leaf *first = this->LeftmostLeaf();
		if (first == nullptr || first->count == 0) {
			return this->end();
		}
		return iterator(first, 0, this);
	}

	/**
	 * @brief Return const_iterator to the first element in the tree.
	 *
	 * If the tree is empty, returns end().
	 *
	 * @return Const iterator to the smallest key, or end() if empty.
	 */
	const_iterator begin() const
	{
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

		const Leaf *first = this->LeftmostLeaf();
		if (first == nullptr || first->count == 0) {
			return this->end();
		}
		return const_iterator(first, 0, this);
	}

	/**
	 * @brief Return iterator to the element past the last key.
	 *
	 * This acts as the usual end() sentinel and does not point to a valid key.
	 *
	 * @return Iterator representing one-past-the-end.
	 */
	iterator end()
	{
		return iterator(nullptr, 0, this); // sentinel
	}

	/**
	 * @brief Return const_iterator to the element past the last key.
	 *
	 * @return Const iterator representing one-past-the-end.
	 */
	const_iterator end() const
	{
		return const_iterator(nullptr, 0, this);
	}

	/**
	 * @brief Find an element with the given key.
	 *
	 * @param key Key to look up.
	 * @return Iterator to the element if found, end() otherwise.
	 */
	iterator find(const Tkey &key)
	{
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

		Leaf *leaf = this->FindLeaf(key);
		if (leaf == nullptr) {
			return this->end();
		}

		uint8_t i = this->LowerBound(leaf->keys, leaf->count, key);
		if (i < leaf->count && leaf->keys[i] == key) {
			return iterator(leaf, i, this);
		}

		return this->end();
	}

	/**
	 * @brief Find an element with the given key (const overload).
	 *
	 * @param key Key to look up.
	 * @return Const iterator to the element if found, end() otherwise.
	 */
	const_iterator find(const Tkey &key) const
	{
		assert(this->root != nullptr);

		if (this->empty()) {
			return this->end();
		}

		const Leaf *leaf = this->FindLeaf(key);
		if (leaf == nullptr) {
			return this->end();
		}

		uint8_t i = this->LowerBound(leaf->keys, leaf->count, key);
		if (i < leaf->count && leaf->keys[i] == key) {
			return const_iterator(leaf, i, this);
		}

		return this->end();
	}

	/**
	 * @brief Return iterator to the first element not less than @p key.
	 *
	 * Descends the tree to locate the appropriate leaf, then performs a
	 * linear search within that leaf.
	 *
	 * @param key Key to search for.
	 * @return Iterator to the first element with key >= @p key, or end().
	 */
	iterator lower_bound(const Tkey &key)
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend while we're in an internal node */
		while (!node->is_leaf) {
			Internal *internal = static_cast<Internal *>(node);
			uint8_t i = this->UpperBound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = internal->children[i].get();
		}

		/* At this point, node must be a leaf */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);

		uint8_t i = this->LowerBound(leaf->keys, leaf->count, key);
		return iterator(leaf, i, this);
	}

	/**
	 * @brief Return const_iterator to the first element not less than @p key.
	 *
	 * @param key Key to search for.
	 * @return Const iterator to the first element with key >= @p key, or end().
	 */
	const_iterator lower_bound(const Tkey &key) const
	{
		assert(this->root != nullptr);

		const Node *node = this->root.get();

		/* Descend while we're in an internal node */
		while (!node->is_leaf) {
			const Internal *internal = static_cast<const Internal *>(node);
			uint8_t i = this->UpperBound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = internal->children[i].get();
		}

		/* At this point, node must be a leaf */
		const Leaf *leaf = static_cast<const Leaf *>(node);
		assert(leaf != nullptr);

		uint8_t i = this->LowerBound(leaf->keys, leaf->count, key);
		return const_iterator(leaf, i, this);
	}

	/**
	 * @brief Emplace a key/value pair in map mode.
	 *
	 * If the key already exists, no insertion is performed and the iterator
	 * to the existing element is returned with a false flag. Otherwise, a
	 * new element is inserted and the iterator points to it with a true flag.
	 *
	 * @param key   Key to insert.
	 * @param value Value to insert.
	 * @return Pair of iterator to element and a bool indicating insertion.
	 */
	template <typename U = Tvalue> requires (!std::is_void_v<U>)
		std::pair<iterator, bool> emplace(const Tkey &key, const U &value)
	{
		return this->InsertCommon(key, [&](Leaf *leaf, uint8_t idx) {
			this->Insert(leaf, idx, key, value);
		});
	}

	/**
	 * @brief Emplace a key in set mode.
	 *
	 * If the key already exists, no insertion is performed and the iterator
	 * to the existing element is returned with a false flag. Otherwise, a
	 * new key is inserted and the iterator points to it with a true flag.
	 *
	 * @param key Key to insert.
	 * @return Pair of iterator to element and a bool indicating insertion.
	 */
	template <typename U = Tvalue> requires (std::is_void_v<U>)
		std::pair<iterator, bool> emplace(const Tkey &key)
	{
		return this->InsertCommon(key, [&](Leaf *leaf, uint8_t idx) {
			this->Insert(leaf, idx, key);
		});
	}

	/**
	 * @brief Erase the element pointed to by @p pos.
	 *
	 * Removes the key (and associated value in map mode) at @p pos and
	 * returns an iterator to the logical successor, if any.
	 *
	 * Structural changes (borrows/merges) may occur; the returned iterator
	 * is checked in debug builds to ensure it still points to @p succ_key.
	 *
	 * @param pos Iterator to the element to erase.
	 * @return Iterator to the successor element, or end() if none.
	 */
	iterator erase(iterator pos)
	{
		if (pos == this->end()) {
			return this->end();
		}

		Leaf *leaf = pos.leaf;
		uint8_t i = pos.index;

		/* Erase locally (shift left) */
		std::copy(leaf->keys.begin() + i + 1, leaf->keys.begin() + leaf->count, leaf->keys.begin() + i);

		if constexpr (!std::is_void_v<Tvalue>) {
			std::copy(leaf->values.begin() + i + 1, leaf->values.begin() + leaf->count, leaf->values.begin() + i);
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
			assert(leaf->index_in_parent == this->FindChildIndex(parent, leaf));
			uint8_t child_idx = leaf->index_in_parent;

			if (parent->count > 0 && child_idx > 0) {
				this->MaintainBoundaryUpward(parent, child_idx - 1);
			} else {
				this->MaintainBoundaryUpward(parent, 0);
			}
		}

		/* Fix underflow, passing iterator by reference */
		if (leaf->parent != nullptr && leaf->count < 32) {
			Internal *parent = leaf->parent;
			assert(leaf->index_in_parent == this->FindChildIndex(parent, leaf));
			uint8_t child_idx = leaf->index_in_parent;

			if (child_idx <= parent->count) {
				this->FixUnderflow(parent, child_idx, succ_it);
			}
		}

		VALIDATE_NODES();

		if (!has_succ) {
			return this->end();
		}

		assert(this->VerifyReturnIterator(succ_it, succ_key));
		return succ_it;
	}

private:
	bool VerifyReturnIterator(const iterator &a, const Tkey &succ_key)
	{
		Leaf *succ_leaf = this->FindLeaf(succ_key);
		if (succ_leaf == nullptr) {
			return false; // defensive: no leaf found for succ_key
		}

		uint8_t succ_idx = this->LowerBound(succ_leaf->keys, succ_leaf->count, succ_key);

		return (a.leaf == succ_leaf && a.index == succ_idx);
	}

	Leaf *LeftmostLeaf() const
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend leftmost children while internal */
		while (!node->is_leaf) {
			Internal *internal = static_cast<Internal *>(node);
			assert(internal->children[0] != nullptr);
			node = internal->children[0].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	Leaf *RightmostLeaf() const
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend rightmost children while internal */
		while (!node->is_leaf) {
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
	 * @brief Find the index of a child in its parent.
	 *
	 * Works for both leaf and internal children and uses the child's
	 * @c index_in_parent field as the expected index. Debug builds
	 * verify that parent->children[index_in_parent] refers to @p child.
	 *
	 * @tparam T      Node type (Leaf or Internal).
	 * @param parent  Parent internal node.
	 * @param child   Child node whose index to retrieve.
	 * @return Index in @p parent->children[] where @p child resides.
	 */
	template <typename T>
	uint8_t FindChildIndex(Internal *parent, T *child) const
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
	 * @brief Common insert logic for map/set emplace.
	 *
	 * Performs a lookup of @p key, checks for existing key, and invokes
	 * @p do_insert to perform the actual insertion into a leaf. After the
	 * operation, it computes and returns an iterator to the inserted key.
	 *
	 * @tparam Tfunc   Callable type accepting (Leaf*, uint8_t).
	 * @param key      Key to insert.
	 * @param do_insert Callable that performs the insertion in the leaf.
	 * @return Pair of iterator to the key and a bool indicating insertion.
	 */
	template <typename Tfunc>
	std::pair<iterator, bool> InsertCommon(const Tkey &key, Tfunc &&do_insert)
	{
		iterator it = this->lower_bound(key);
		Leaf *leaf = it.leaf;
		uint8_t idx = it.index;

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
		assert(this->VerifyReturnIterator(it, key));
		return std::pair<iterator, bool>(it, true);
	}

	/**
	 * @brief Core leaf insert logic shared by map and set modes.
	 *
	 * Shifts keys to make room at index @p i, inserts @p key, and updates
	 * separators and split handling as needed.
	 *
	 * @param leaf Leaf node to insert into.
	 * @param i    Index where the new key should be placed.
	 * @param key  Key to insert.
	 */
	void InsertCommon(Leaf *leaf, uint8_t i, const Tkey &key)
	{
		/* Shift keys */
		std::copy_backward(leaf->keys.begin() + i, leaf->keys.begin() + leaf->count, leaf->keys.begin() + leaf->count + 1);

		leaf->keys[i] = key;
		leaf->count++;

		/* Centralized separator refresh */
		if (i == 0 && leaf->parent != nullptr) {
			Internal *parent = leaf->parent;
			assert(leaf->index_in_parent == this->FindChildIndex(parent, leaf));
			uint8_t child_idx = leaf->index_in_parent;
			if (child_idx > 0) {
				this->MaintainBoundaryLocal(parent, child_idx - 1);
			}
		}

		/* Split if leaf is full */
		if (leaf->count == 64) {
			this->SplitLeaf(leaf);
		}
	}

	/**
	 * @brief Map mode leaf insert.
	 *
	 * Inserts @p key and @p value into @p leaf at index @p i, shifting
	 * existing keys and values as needed.
	 *
	 * @param leaf   Leaf node to insert into.
	 * @param i      Index where the new key/value should be placed.
	 * @param key    Key to insert.
	 * @param value  Value to insert.
	 */
	template <typename U = Tvalue> requires (!std::is_void_v<U>)
		void Insert(Leaf *leaf, uint8_t i, const Tkey &key, const U &value)
	{
		/* Shift values */
		std::copy_backward(leaf->values.begin() + i, leaf->values.begin() + leaf->count, leaf->values.begin() + leaf->count + 1);

		leaf->values[i] = value;

		this->InsertCommon(leaf, i, key);
	}

	/**
	 * @brief Set mode leaf insert.
	 *
	 * Inserts @p key into @p leaf at index @p i, shifting existing keys
	 * as needed. No value array is present in set mode.
	 *
	 * @param leaf Leaf node to insert into.
	 * @param i    Index where the new key should be placed.
	 * @param key  Key to insert.
	 */
	template <typename U = Tvalue> requires (std::is_void_v<U>)
		void Insert(Leaf *leaf, uint8_t i, const Tkey &key)
	{
		this->InsertCommon(leaf, i, key);
	}

	/**
	 * @brief Descend the tree to find the leaf for the given key.
	 *
	 * Follows internal separators using UpperBound until a leaf is reached.
	 * Returns the leaf where @p key is stored or should be inserted.
	 *
	 * @param key Key to locate.
	 * @return Leaf node containing or suitable for @p key.
	 */
	Leaf *FindLeaf(const Tkey &key) const
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend while internal */
		while (!node->is_leaf) {
			Internal *internal = static_cast<Internal *>(node);
			uint8_t i = this->UpperBound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = internal->children[i].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = static_cast<Leaf *>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	/**
	 * @brief Verify structural invariants for an internal node.
	 *
	 * Checks that:
	 * - children[0..count] are non-null,
	 * - children[count+1..max-1] are null,
	 * - each child->parent == this internal,
	 * - each child->index_in_parent == its index.
	 *
	 * @param node Node that is expected to be internal.
	 * @return Always true in non-asserting builds.
	 */
	bool VerifyChildrenParent(const Node *node) const
	{
		if (node == nullptr) {
			return true;
		}

		assert(!node->is_leaf);
		const Internal *internal = static_cast<const Internal *>(node);

		/* 1. Active children must be non-null */
		for (uint8_t i = 0; i <= internal->count; ++i) {
			const Node *child = static_cast<const Node *>(internal->children[i].get());
			assert(child != nullptr);

			/* Parent pointer must match */
			assert(child->parent == internal);

			/* index_in_parent must match */
			assert(child->index_in_parent == i);
		}

		/* 2. Inactive children must be null */
		for (uint8_t i = internal->count + 1; i < 65; ++i) {
			assert(internal->children[i] == nullptr);
		}

		return true;
	}

	/**
	 * @brief Split a full leaf into two halves and insert separator into parent.
	 *
	 * The left leaf keeps the lower half of the keys, and a new right leaf
	 * receives the upper half. The first key of the new leaf is promoted
	 * to the parent as a separator. The leaf chain links are updated.
	 *
	 * @param leaf Full leaf to split.
	 */
	void SplitLeaf(Leaf *leaf)
	{
		assert(leaf != nullptr);
		assert(leaf->count == 64);

		uint8_t mid = leaf->count / 2;

		/* Create a new Leaf node */
		NodePtr new_leaf_node = this->AllocateLeaf();
		Leaf *new_leaf = static_cast<Leaf *>(new_leaf_node.get());

		/* Move half of the keys/values into the new leaf */
		std::copy(leaf->keys.begin() + mid, leaf->keys.begin() + leaf->count, new_leaf->keys.begin());

		if constexpr (!std::is_void_v<Tvalue>) {
			std::copy(leaf->values.begin() + mid, leaf->values.begin() + leaf->count, new_leaf->values.begin());
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
		this->InsertIntoParent(leaf, separator, std::move(new_leaf_node));
	}

	/**
	 * @brief Insert a separator key and right child into the parent of @p left.
	 *
	 * If @p left has no parent, a new internal root is created and both the
	 * old root (@p left) and @p right_node become its children. Otherwise the
	 * separator and right child are inserted into the existing parent, splitting
	 * the parent if needed.
	 *
	 * @param left       Existing child node on the left side of the separator.
	 * @param separator  Key to insert into the parent.
	 * @param right_node New right child node produced by a split.
	 */
	void InsertIntoParent(Node *left, const Tkey &separator, NodePtr right_node)
	{
		/* Parent must be an internal node if it exists */
		Internal *parent = left->parent;
		assert(left->parent == nullptr || !left->parent->is_leaf);

		/* Root case: parent == nullptr -> create fresh internal root */
		if (parent == nullptr) {
			NodePtr new_root_ptr = this->AllocateInternal();
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
				Node *children_left = static_cast<Node *>(new_root->children[0].get());
				this->SetParent(children_left, new_root, 0);
			}

			if (new_root->children[1] != nullptr) {
				Node *children_right = static_cast<Node *>(new_root->children[1].get());
				this->SetParent(children_right, new_root, 1);
			}

			this->root = std::move(new_root_ptr);
			assert(this->VerifyChildrenParent(this->root.get()));
			return;
		}

		/* Parent exists: ensure space */
		if (parent->count == 64) {
			this->SplitInternal(parent);
			assert(left->parent != nullptr && !left->parent->is_leaf);
			parent = left->parent; // left may have moved
		}

		assert(left->index_in_parent == this->FindChildIndex(parent, left));
		uint8_t i = left->index_in_parent;

		/* Shift keys and children right */
		std::copy_backward(parent->keys.begin() + i, parent->keys.begin() + parent->count, parent->keys.begin() + parent->count + 1);

		std::move_backward(parent->children.begin() + i + 1, parent->children.begin() + parent->count + 1, parent->children.begin() + parent->count + 2);

		/* Refresh children's parent/index_in_parent after shift */
		for (uint8_t j = i + 1; j <= parent->count + 1; ++j) {
			if (parent->children[j] != nullptr) {
				Node *child = static_cast<Node *>(parent->children[j].get());
				this->SetParent(child, parent, j);
			}
		}

		/* Insert separator and right child */
		parent->keys[i] = separator;
		assert(parent->children[i + 1] == nullptr);
		parent->children[i + 1] = std::move(right_node);

		if (parent->children[i + 1] != nullptr) {
			Node *child = static_cast<Node *>(parent->children[i + 1].get());
			this->SetParent(child, parent, i + 1);
		}

		++parent->count;
		assert(this->VerifyChildrenParent(parent));
	}

	/**
	 * @brief Split a full internal node into two halves and promote a separator.
	 *
	 * The separator at the middle index is moved up into the parent of @p node.
	 * The left node keeps the lower half of the keys and children, and a new
	 * right node receives the upper half. Child parent metadata is updated.
	 *
	 * @param node Full internal node to split.
	 */
	void SplitInternal(Internal *node)
	{
		assert(node != nullptr);
		assert(node->count == 64 && "SplitInternal called on non-full internal");

		uint8_t mid = node->count / 2;
		assert(mid < node->count);

		/* Separator promoted to parent */
		Tkey separator = node->keys[mid];

		/* Create new right internal node */
		NodePtr right_node = this->AllocateInternal();
		Internal *right = static_cast<Internal *>(right_node.get());

		/* Move keys: left keeps [0..mid - 1], right gets [mid + 1..node->count - 1] */
		std::copy(node->keys.begin() + mid + 1, node->keys.begin() + node->count, right->keys.begin());

		right->count = node->count - mid - 1;

		/* Move children: left keeps [0..mid], right gets [mid + 1..node->count] */
		std::move(node->children.begin() + mid + 1, node->children.begin() + node->count + 1, right->children.begin());

		/* Fix parent/index_in_parent for moved children in right */
		this->ReindexChildren(right, 0, right->count);

		/* Left node keeps first mid keys and mid + 1 children */
		node->count = mid;

		/* Clear dangling child slots beyond mid in left */
		std::fill(node->children.begin() + mid + 1, node->children.begin() + 65, nullptr);

		/* Verify left children wiring */
		assert(this->VerifyChildrenParent(node));

		assert(node->count == mid);
		assert(right->count > 0);

		/* Insert separator and right child into parent */
		this->InsertIntoParent(node, separator, std::move(right_node));

		/* After insertion, parent's children changed; defensively rewire */
		assert(this->VerifyChildrenParent(node->parent));
	}

	/**
	 * @brief Refresh a separator and propagate changes upward if needed.
	 *
	 * Refreshes the separator at @p sep_idx in @p parent to match the
	 * minimum key in the right subtree, then walks upward along the tree
	 * to refresh affected ancestor separators on the same path.
	 *
	 * @param parent  Internal node containing the separator.
	 * @param sep_idx Index of the separator to refresh.
	 */
	void MaintainBoundaryUpward(Internal *parent, uint8_t sep_idx)
	{
		assert(parent != nullptr);

		/* Refresh at this parent only if there is a valid separator */
		if (sep_idx < parent->count) {
			this->MaintainBoundaryLocal(parent, sep_idx);
		}

		/* Propagate upward along the leftmost path */
		for (Internal *p = parent; p->parent != nullptr; p = p->parent) {
			Internal *gp = p->parent;
			assert(p->index_in_parent == this->FindChildIndex(gp, p));
			uint8_t idx_in_gp = p->index_in_parent;

			/* If this subtree sits to the right of some separator in gp,
			 * refresh that ancestor separator. */
			if (idx_in_gp > 0) {
				this->MaintainBoundaryLocal(gp, idx_in_gp - 1);
				break; // not on leftmost path anymore
			}
		}
	}

	/**
	 * @brief Refresh a separator at @p sep_idx to match right-subtree minimum.
	 *
	 * Looks at the subtree rooted at children[sep_idx + 1], descends to its
	 * leftmost leaf, and updates @p parent->keys[sep_idx] to the minimum key
	 * in that subtree.
	 *
	 * @param parent  Internal node containing the separator.
	 * @param sep_idx Index of the separator to refresh.
	 */
	void MaintainBoundaryLocal(Internal *parent, uint8_t sep_idx)
	{
		assert(parent != nullptr);

		/* If parent has no separators, nothing to refresh */
		if (parent->count == 0) {
			return;
		}

		/* If sep_idx is out of range (e.g. after merge), nothing to refresh */
		if (sep_idx >= parent->count) {
			return;
		}

		/* Right child at sep_idx + 1 must exist and be valid */
		Node *right = parent->children[sep_idx + 1].get();
		assert(right != nullptr);

		/* Descend to the leftmost leaf of the right subtree */
		Node *current = right;
		while (!current->is_leaf) {
			Internal *internal = static_cast<Internal *>(current);

			/* Invariant: children[0] must be non-null for any internal node */
			assert(internal->children[0] != nullptr);

			current = internal->children[0].get();
		}

		/* current is now a leaf */
		Leaf *leaf = static_cast<Leaf *>(current);
		assert(leaf->count > 0);

		/* Update separator */
		parent->keys[sep_idx] = leaf->keys[0];
	}

	/**
	 * @brief Borrow one key/value across leaf neighbours, symmetrically.
	 *
	 * If @p recipient_is_left is true, @p leaf borrows from its right sibling.
	 * Otherwise it borrows from its left sibling. Iterator @p succ_it is
	 * updated if it pointed into a shifted or donor leaf.
	 *
	 * @param leaf             Recipient leaf.
	 * @param succ_it          Iterator potentially impacted by the move.
	 * @param recipient_is_left True if borrows from right into @p leaf.
	 */
	void BorrowLeaf(Leaf *leaf, iterator &succ_it, bool recipient_is_left)
	{
		assert(leaf != nullptr);
		Internal *parent = leaf->parent;
		assert(parent != nullptr);

		assert(leaf->index_in_parent == this->FindChildIndex(parent, leaf));
		uint8_t idx = leaf->index_in_parent;

		if (recipient_is_left) {
			/* Borrow from right into leaf */
			Leaf *right = leaf->next_leaf;
			assert(right != nullptr);
			assert(right->parent == parent);
			assert(leaf->count < 64 && right->count > 32);

			/* 1. Insert donor extremum: right.min -> leaf[end] */
			leaf->keys[leaf->count] = right->keys[0];
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[leaf->count] = right->values[0];
			}

			/* 2. Retarget iterator if it was pointing into right */
			if (succ_it.leaf == right) {
				succ_it.leaf = leaf;
				succ_it.index = leaf->count;
			}

			/* 3. Update counts */
			++leaf->count;
			--right->count;
			assert(right->count >= 32);

			/* 4. Shift donor left */
			std::copy(right->keys.begin() + 1, right->keys.begin() + right->count + 1, right->keys.begin());
			if constexpr (!std::is_void_v<Tvalue>) {
				std::copy(right->values.begin() + 1, right->values.begin() + right->count + 1, right->values.begin());
			}

			/* 5. Refresh boundary (separator pointing to right changed) */
			assert(idx + 1 <= parent->count);
			this->MaintainBoundaryUpward(parent, idx);

		} else {
			/* Borrow from left into leaf */
			Leaf *left = leaf->prev_leaf;
			assert(left != nullptr);
			assert(left->parent == parent);
			assert(leaf->count < 64 && left->count > 32);

			/* 1. Prepare slot at front (shift leaf right) */
			std::copy_backward(leaf->keys.begin(), leaf->keys.begin() + leaf->count, leaf->keys.begin() + leaf->count + 1);
			if constexpr (!std::is_void_v<Tvalue>) {
				std::copy_backward(leaf->values.begin(), leaf->values.begin() + leaf->count, leaf->values.begin() + leaf->count + 1);
			}

			/* 2. Insert donor extremum: left.max -> leaf[0] */
			leaf->keys[0] = left->keys[left->count - 1];
			if constexpr (!std::is_void_v<Tvalue>) {
				leaf->values[0] = left->values[left->count - 1];
			}

			/* 3. Retarget iterator if it was pointing into leaf (indices shifted + 1) */
			if (succ_it.leaf == leaf) {
				++succ_it.index;
			}

			/* 4. Update counts */
			++leaf->count;
			--left->count;
			assert(left->count >= 32);

			/* 5. Refresh boundary (separator pointing to leaf changed) */
			assert(idx > 0); // leaf cannot be the 0th child if it has a left sibling
			this->MaintainBoundaryUpward(parent, idx - 1);
		}
	}

	/**
	 * @brief Merge right leaf into left leaf, keeping the left leaf.
	 *
	 * Moves keys (and values in map mode) from @p right into @p left, fixes
	 * the leaf chain, and removes the appropriate separator and child from
	 * the parent. Iterator @p succ_it is retargeted if it pointed into right.
	 *
	 * @param left    Left leaf to keep.
	 * @param succ_it Iterator potentially impacted by the merge.
	 */
	void MergeKeepLeftLeaf(Leaf *left, iterator &succ_it)
	{
		assert(left != nullptr);
		Leaf *right = left->next_leaf;
		assert(right != nullptr);

		/* Move only existing keys post-erase */
		std::copy(right->keys.begin(), right->keys.begin() + right->count, left->keys.begin() + left->count);

		if constexpr (!std::is_void_v<Tvalue>) {
			std::copy(right->values.begin(), right->values.begin() + right->count, left->values.begin() + left->count);
		}

		/* Retarget iterator if it was pointing into right */
		if (succ_it.leaf == right) {
			succ_it.leaf = left;
			succ_it.index += left->count;
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
		assert(left->index_in_parent == this->FindChildIndex(parent, left));
		uint8_t idx = left->index_in_parent;
		this->RemoveSeparatorAndChild(parent, idx);

		/* After removal, the separator at idx may now reflect a different right-min */
		if (idx < parent->count) {
			this->MaintainBoundaryUpward(parent, idx);
		}
	}

	/**
	 * @brief Remove separator at @p sep_idx and its right child from an internal node.
	 *
	 * Shifts keys and children left to close the gap, updates child parent
	 * metadata, and decrements the count of separators.
	 *
	 * @param parent  Internal node to remove from.
	 * @param sep_idx Index of the separator to remove.
	 */
	void RemoveSeparatorAndChild(Internal *parent, uint8_t sep_idx)
	{
		assert(parent != nullptr);
		assert(sep_idx < parent->count);

		/* 1. Shift keys left: [sep_idx + 1..count - 1] -> [sep_idx..count - 2] */
		std::copy(parent->keys.begin() + sep_idx + 1, parent->keys.begin() + parent->count, parent->keys.begin() + sep_idx);

		/* 2. Shift children left: [sep_idx + 2..count] -> [sep_idx + 1..count - 1] */
		std::move(parent->children.begin() + sep_idx + 2, parent->children.begin() + parent->count + 1, parent->children.begin() + sep_idx + 1);

		/* 3. Fix parent/index_in_parent pointers for shifted children */
		if (parent->count > sep_idx + 1) {
			this->ReindexChildren(parent, sep_idx + 1, parent->count - 1);
		}

		/* 4. Clear the last child slot that is now out of range */
		parent->children[parent->count].reset();

		/* 5. Decrement count (number of separators) */
		--parent->count;

		/* 6. Safety: ensure all children are valid and wired */
		assert(this->VerifyChildrenParent(parent));
	}

	/**
	 * @brief Check if two leaf siblings can be merged without overflow.
	 *
	 * @param left  Left leaf.
	 * @param right Right leaf.
	 * @return True if merging @p right into @p left fits within capacity.
	 */
	inline bool CanMergeLeaf(const Leaf *left, const Leaf *right)
	{
		assert(left != nullptr && right != nullptr);
		return left->count + right->count <= 64; // leaf capacity
	}

	/**
	 * @brief Fix underflow of a leaf child by borrowing or merging.
	 *
	 * Attempts to borrow from the right or left sibling if possible. If
	 * borrowing is not possible, tries to merge with a sibling. Iterator
	 * @p succ_it is updated when keys move across leaves.
	 *
	 * @param child   Leaf child that underflowed.
	 * @param succ_it Iterator potentially impacted by rebalancing.
	 */
	void FixUnderflowLeafChild(Leaf *child, iterator &succ_it)
	{
		assert(child != nullptr);
		Internal *parent = child->parent;
		assert(parent != nullptr);

		assert(child->index_in_parent == this->FindChildIndex(parent, child));
		const uint8_t i = child->index_in_parent;

		/* Try borrow from right sibling */
		if (i < parent->count) {
			Leaf *right = child->next_leaf;
			assert((right == nullptr) == (i >= parent->count) && "Link/index mismatch");

			if (right != nullptr && right->count > 32) {
				this->BorrowLeaf(child, succ_it, /*recipient_is_left=*/true);
				this->FixUnderflowInternalCascade(parent);
				return;
			}
		}

		/* Try borrow from left sibling */
		if (i > 0) {
			Leaf *left = child->prev_leaf;
			assert((left == nullptr) == (i == 0) && "Link/index mismatch");

			if (left != nullptr && left->count > 32) {
				this->BorrowLeaf(child, succ_it, /*recipient_is_left=*/false);
				this->FixUnderflowInternalCascade(parent);
				return;
			}
		}

		/* Merge path (i < parent->count => try merging child with right) */
		if (i < parent->count) {
			Leaf *right = child->next_leaf;
			assert((right == nullptr) == (i >= parent->count) && "Link/index mismatch");

			if (right != nullptr && this->CanMergeLeaf(child, right)) {
				this->MergeKeepLeftLeaf(child, succ_it); // merge right into child (left)
				this->FixUnderflowInternalCascade(parent);
				return;
			}

			/* Fallback: borrow from left if possible (second chance) */
			if (i > 0) {
				Leaf *left = child->prev_leaf;
				if (left != nullptr && left->count > 32) {
					this->BorrowLeaf(child, succ_it, /*recipient_is_left=*/false);
					this->FixUnderflowInternalCascade(parent);
					return;
				}
			}

			/* Last resort: force merge into left */
			assert(i > 0 && "Right merge overflow and no left sibling to merge into");
			Leaf *left = child->prev_leaf;
			assert(left != nullptr);
			assert(left->next_leaf == child);
			this->MergeKeepLeftLeaf(left, succ_it); // merge child into left
			this->FixUnderflowInternalCascade(parent);
			return;

		} else {
			/* Rightmost child: must merge into left */
			assert(i > 0);
			Leaf *left = child->prev_leaf;
			assert(left != nullptr);
			assert(left->next_leaf == child);
			if (this->CanMergeLeaf(left, child)) {
				this->MergeKeepLeftLeaf(left, succ_it); // merge child into left
				this->FixUnderflowInternalCascade(parent);
				return;
			}

			/* Fallback: borrow from left */
			if (left->count > 32) {
				this->BorrowLeaf(child, succ_it, /*recipient_is_left=*/false);
				this->FixUnderflowInternalCascade(parent);
				return;
			}

			assert(false && "Rightmost leaf underflow: cannot merge or borrow");
		}
	}

	void FixUnderflow(Internal *parent, uint8_t i, iterator &succ_it)
	{
		assert(parent != nullptr);
		assert(i <= parent->count);

		Node *child_base = parent->children[i].get();
		assert(child_base != nullptr);

		if (child_base->is_leaf) {
			Leaf *leaf = static_cast<Leaf *>(child_base);
			this->FixUnderflowLeafChild(leaf, succ_it);
		} else {
			this->FixUnderflowInternalChild(parent, i);
		}
	}

	/**
	 * @brief Helper to fetch an internal child node from an internal parent.
	 *
	 * Asserts that the child exists and is an internal node.
	 *
	 * @param parent Parent internal node.
	 * @param index  Index in parent->children[].
	 * @return Internal child at the given index.
	 */
	Internal *GetChildInternal(Internal *parent, uint8_t index)
	{
		assert(parent != nullptr);
		assert(index <= parent->count);

		Node *child_base = parent->children[index].get();
		assert(child_base != nullptr);
		assert(!child_base->is_leaf);

		Internal *internal = static_cast<Internal *>(child_base);
		assert(internal != nullptr);

		return internal;
	}

	/**
	 * @brief Borrow across a boundary between two consecutive internal siblings.
	 *
	 * If @p recipient_is_left is true, the left child receives a key and child
	 * from the right sibling. Otherwise, the right child receives from the left.
	 * Parent separator is updated accordingly and child metadata is refreshed.
	 *
	 * @param parent          Parent internal node.
	 * @param left_idx        Index of the left sibling in parent->children.
	 * @param recipient_is_left True if left receives from right.
	 */
	void BorrowInternal(Internal *parent, uint8_t left_idx, bool recipient_is_left)
	{
		assert(parent != nullptr);
		assert(left_idx < parent->count);

		Internal *left = this->GetChildInternal(parent, left_idx);
		Internal *right = this->GetChildInternal(parent, left_idx + 1);
		assert(left != nullptr && right != nullptr);

		if (recipient_is_left) {
			/* Left receives from right */

			/* 1. Insert parent key into left[end] */
			left->keys[left->count] = parent->keys[left_idx];

			/* 2. Move donor's first child into left[end + 1] */
			left->children[left->count + 1] = std::move(right->children[0]);
			this->SetParent(left->children[left->count + 1].get(), left, left->count + 1);

			/* 3. Update recipient count */
			++left->count;

			/* 4. Move donor's first key up into parent */
			parent->keys[left_idx] = right->keys[0];

			/* 5. Shift donor left to close gap */
			std::copy(right->keys.begin() + 1, right->keys.begin() + right->count, right->keys.begin());
			std::move(right->children.begin() + 1, right->children.begin() + right->count + 1, right->children.begin());

			/* 6. Fix parent/index_in_parent pointers for shifted donor children */
			if (right->count > 0) {
				this->ReindexChildren(right, 0, right->count - 1);
			}

			/* 7. Update donor count */
			--right->count;

		} else {
			/* Right receives from left */

			/* 1. Prepare recipient slot (shift right's keys/children right) */
			std::copy_backward(right->keys.begin(), right->keys.begin() + right->count, right->keys.begin() + right->count + 1);
			std::move_backward(right->children.begin(), right->children.begin() + right->count + 1, right->children.begin() + right->count + 2);

			/* 2. Fix parent/index_in_parent pointers for shifted recipient children */
			if (right->count > 0) {
				this->ReindexChildren(right, 1, right->count + 1);
			}

			/* 3. Insert parent key into right[0] */
			right->keys[0] = parent->keys[left_idx];

			/* 4. Move donor's last child into right[0] */
			right->children[0] = std::move(left->children[left->count]);
			this->SetParent(right->children[0].get(), right, 0);

			/* 5. Update recipient count */
			++right->count;

			/* 6. Move donor's last key up into parent */
			parent->keys[left_idx] = left->keys[left->count - 1];

			/* 7. Update donor count */
			--left->count;
		}

		/* Defensive rewiring (shared) */
		assert(this->VerifyChildrenParent(left));
		assert(this->VerifyChildrenParent(right));
		assert(this->VerifyChildrenParent(parent));

		/* Refresh boundary separator at left_idx */
		this->MaintainBoundaryUpward(parent, left_idx);
	}

	/**
	 * @brief Check if two internal siblings can be merged without overflow.
	 *
	 * The merged node will contain left.count + 1 (parent separator)
	 * + right.count keys.
	 *
	 * @param left  Left internal node.
	 * @param right Right internal node.
	 * @return True if merge fits within internal node capacity.
	 */
	inline bool CanMergeInternal(const Internal *left, const Internal *right)
	{
		assert(left != nullptr && right != nullptr);
		return left->count + 1 + right->count <= 64;
	}

	/**
	 * @brief Merge internal at i+1 into internal at i, keeping the left node.
	 *
	 * Moves the separator @p i from @p parent into the left node, appends all
	 * keys and children from the right node, and removes the separator and
	 * right child from @p parent.
	 *
	 * @param parent Parent internal node.
	 * @param i      Index of the left child in parent->children.
	 */
	void MergeKeepLeftInternal(Internal *parent, uint8_t i)
	{
		assert(parent != nullptr);
		assert(i < parent->count);

		Internal *left = this->GetChildInternal(parent, i);
		Internal *right = this->GetChildInternal(parent, i + 1);

		/* Guard if merge would overflow */
		assert(this->CanMergeInternal(left, right) && "MergeKeepLeftInternal called when merge is not possible");

		/* 1. Append separator i */
		left->keys[left->count] = parent->keys[i];

		/* 2. Move right's keys into left */
		std::copy(right->keys.begin(), right->keys.begin() + right->count, left->keys.begin() + left->count + 1);

		/* 3. Move right's children into left */
		std::move(right->children.begin(), right->children.begin() + right->count + 1, left->children.begin() + left->count + 1);

		/* 4. Fix parent/index_in_parent pointers for moved children */
		this->ReindexChildren(left, left->count + 1, left->count + 1 + right->count);

		/* 5 Update count */
		left->count += 1 + right->count;

		/* 6. Remove separator i and child i + 1 from parent */
		this->RemoveSeparatorAndChild(parent, i);

		/* 7. Defensive checks */
		assert(this->VerifyChildrenParent(left));
		assert(this->VerifyChildrenParent(parent));

		/* 8. Boundary refresh: separator at i now points to the merged right-min,
		 * or if i is out of range, refresh the last separator. */
		if (i < parent->count) {
			this->MaintainBoundaryUpward(parent, i);
		} else if (parent->count > 0) {
			this->MaintainBoundaryUpward(parent, parent->count - 1);
		}
	}

	/**
	 * @brief Fix underflow when parent's child at @p i is an internal node.
	 *
	 * Chooses borrow (from right or left sibling) if possible; otherwise
	 * performs a merge and triggers an upward underflow cascade if needed.
	 *
	 * @param parent Parent internal node.
	 * @param i      Index of the child in parent->children.
	 */
	void FixUnderflowInternalChild(Internal *parent, uint8_t i)
	{
		assert(parent != nullptr);
		assert(i <= parent->count);

		/* Current internal child */
		Internal *child = this->GetChildInternal(parent, i);

		/* Borrow from right if possible */
		if (i < parent->count) {
			Internal *right = this->GetChildInternal(parent, i + 1);

			if (right->count > 31) {
				this->BorrowInternal(parent, i, /*recipient_is_left=*/true);
				this->FixUnderflowInternalCascade(parent);
				return;
			}
		}

		/* Borrow from left if possible */
		if (i > 0) {
			Internal *left = this->GetChildInternal(parent, i - 1);

			if (left->count > 31) {
				this->BorrowInternal(parent, i - 1, /*recipient_is_left=*/false);
				this->FixUnderflowInternalCascade(parent);
				return;
			}
		}

		/* Merge logic */
		if (i < parent->count) {
			Internal *right = this->GetChildInternal(parent, i + 1);

			if (this->CanMergeInternal(child, right)) {
				this->MergeKeepLeftInternal(parent, i);
				this->FixUnderflowInternalCascade(parent);
				return;
			}

			/* Fallback: borrow-left if available (second chance) */
			if (i > 0) {
				Internal *left = this->GetChildInternal(parent, i - 1);

				if (left->count > 31) {
					this->BorrowInternal(parent, i - 1, /*recipient_is_left=*/false);
					this->FixUnderflowInternalCascade(parent);
					return;
				}
			}

			/* Last resort: mirror merge into left */
			assert(i > 0 && "Internal underflow at i=0 with no feasible borrow/merge");
			this->MergeKeepLeftInternal(parent, i - 1);
			this->FixUnderflowInternalCascade(parent);
			return;

		} else {
			/* Rightmost child: must merge into left */
			assert(i > 0);
			Internal *left = this->GetChildInternal(parent, i - 1);

			if (this->CanMergeInternal(left, child)) {
				this->MergeKeepLeftInternal(parent, i - 1);
				this->FixUnderflowInternalCascade(parent);
				return;
			}

			/* Fallback: borrow-left */
			if (left->count > 31) {
				this->BorrowInternal(parent, i - 1, /*recipient_is_left=*/false);
				this->FixUnderflowInternalCascade(parent);
				return;
			}

			assert(false && "Rightmost internal underflow: cannot merge or borrow");
		}
	}

	/**
	 * @brief Shrink tree height if root is an empty internal node.
	 *
	 * If the root is an internal node with zero separators, its single child
	 * is promoted as the new root. If the root is a leaf, nothing happens.
	 */
	void MaybeShrinkHeight()
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
			this->SetParent(child.get(), nullptr, 0);

			this->root = std::move(child);
			return;
		}

		/* Ensure all children of root are wired correctly */
		assert(this->VerifyChildrenParent(root_internal));
	}

	/**
	 * @brief Cascade underflow handling upward from an internal node.
	 *
	 * If @p node is the root, possibly shrinks the tree height and stops.
	 * Otherwise, its parent is examined and rebalanced if @p node falls
	 * below the minimum occupancy.
	 *
	 * @param node Internal node that may have underflowed.
	 */
	void FixUnderflowInternalCascade(Internal *node)
	{
		assert(node != nullptr);

		/* Is the given internal node the root? */
		if (node == this->root.get()) {
			/* Stop at root: shrink height if needed and exit */
			this->MaybeShrinkHeight();
			return;
		}

		assert(node->parent != nullptr);

		Internal *parent = static_cast<Internal *>(node->parent);
		assert(!parent->is_leaf);

		/* Find node's index in parent */
		assert(node->index_in_parent == this->FindChildIndex(parent, node));
		uint8_t i = node->index_in_parent;

		/* If node is below minimum, fix it (internal child path) */
		if (node->count < 31) {
			this->FixUnderflowInternalChild(parent, i);
		}

		/* Defensive note: if parent becomes empty and isn't root,
		 * its own parent will handle it when reached. */
	}

	/**
	 * @brief Linear lower_bound over a fixed-size key array.
	 *
	 * Searches keys[0..count) for the first index where keys[i] >= key.
	 *
	 * @param keys  Array of keys.
	 * @param count Number of valid keys.
	 * @param key   Key to search for.
	 * @return Index of first key >= @p key, or @p count if none.
	 */
	uint8_t LowerBound(const std::array<Tkey, 64> &keys, uint8_t count, const Tkey &key) const
	{
		for (uint8_t i = 0; i < count; ++i) {
			if (!(keys[i] < key)) { // i.e. keys[i] >= key
				return i;
			}
		}
		return count;
	}

	/**
	 * @brief Linear upper_bound over a fixed-size key array.
	 *
	 * Searches keys[0..count) for the first index where keys[i] > key.
	 *
	 * @param keys  Array of keys.
	 * @param count Number of valid keys.
	 * @param key   Key to search for.
	 * @return Index of first key > @p key, or @p count if none.
	 */
	uint8_t UpperBound(const std::array<Tkey, 64> &keys, uint8_t count, const Tkey &key) const
	{
		for (uint8_t i = 0; i < count; ++i) {
			if (key < keys[i]) { // i.e. keys[i] > key
				return i;
			}
		}
		return count;
	}

	/**
	 * @brief Set the parent pointer and index for a child node.
	 *
	 * @param child  Child node whose metadata is updated.
	 * @param parent Parent internal node (or nullptr for root).
	 * @param idx    Index in parent->children[] where child resides.
	 */
	inline void SetParent(Node *child, Internal *parent, uint8_t idx)
	{
		child->parent = parent;
		child->index_in_parent = idx;
	}

	/**
	 * @brief Reassign parent/index metadata for a range of children.
	 *
	 * Updates @c parent and @c index_in_parent for all non-null children in
	 * the range [start, end] of @p parent->children[].
	 *
	 * @param parent Internal node whose children are updated.
	 * @param start  First child index to update.
	 * @param end    Last child index to update (inclusive).
	 */
	void ReindexChildren(Internal *parent, uint8_t start, uint8_t end)
	{
		for (uint8_t i = start; i <= end; ++i) {
			Node *child = parent->children[i].get();
			assert(child != nullptr);
			this->SetParent(child, parent, i);
		}
	}

#if BPLUSTREE_CHECK
	/**
	 * @brief Run all validation checks on the tree.
	 *
	 * Combines root invariants, recursive node validation, separator consistency,
	 * leaf chain linkage, and parent/child invariants. Only enabled when
	 * BPLUSTREE_CHECK is non-zero.
	 */
	void ValidateAll() const
	{
		assert(this->root != nullptr);

		Node *root_base = this->root.get();

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
		this->ValidateNode(root_base);

		/* Leaf chain validation (ordering, linkage, duplicates) */
		this->ValidateLeafChain();
	}

	/**
	 * @brief Recursive node validation.
	 *
	 * Checks capacity bounds, ordering, range constraints, separator consistency,
	 * and recurses into children with updated key ranges.
	 *
	 * @param node Node to validate.
	 * @param min  Optional lower bound for keys in this subtree.
	 * @param max  Optional upper bound for keys in this subtree.
	 */
	void ValidateNode(const Node *node, const Tkey *min = nullptr, const Tkey *max = nullptr) const
	{
		assert(node != nullptr);

		if (node->is_leaf) {
			const Leaf *leaf = static_cast<const Leaf *>(node);

			/* Capacity bounds */
			assert(leaf->count <= 64);

			if (leaf->count > 0) {
				/* Keys strictly ascending */
				this->AssertStrictlyAscending(leaf->keys, leaf->count);

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
			this->AssertNonDecreasing(internal->keys, internal->count);
		}

		/* Children count = keys + 1, all non-null and correctly parented */
		this->AssertChildInvariants(internal);

		/* Separator consistency: parent key == min of right child */
		for (uint8_t i = 0; i < internal->count; ++i) {
			this->AssertSeparatorConsistency(internal, i);
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

			this->ValidateNode(internal->children[i].get(), child_min, child_max);
		}
	}

	template <typename T>
	void AssertStrictlyAscending([[maybe_unused]] const T &keys, uint8_t count) const
	{
		for (uint8_t i = 1; i < count; ++i) {
			assert(keys[i - 1] < keys[i]);
		}
	}

	template <typename T>
	void AssertNonDecreasing([[maybe_unused]] const T &keys, uint8_t count) const
	{
		for (uint8_t i = 1; i < count; ++i) {
			assert(keys[i - 1] <= keys[i]);
		}
	}

	void AssertChildInvariants(const Internal *internal) const
	{
		/* Active children: 0 .. count must be non-null and correctly wired */
		for (uint8_t i = 0; i <= internal->count; ++i) {
			[[maybe_unused]] Node *child = static_cast<Node *>(internal->children[i].get());
			assert(child != nullptr);
			assert(child->parent == internal);
			assert(child->index_in_parent == i);
		}

		/* Inactive children: count + 1 .. max - 1 must be null */
		for (uint8_t i = internal->count + 1; i < 65; ++i) {
			assert(internal->children[i] == nullptr);
		}
	}

	void AssertSeparatorConsistency(const Internal *internal, uint8_t i) const
	{
		Node *right = internal->children[i + 1].get();
		assert(right != nullptr);
		[[maybe_unused]] const Tkey &right_min = this->SubTreeMin(right);
		assert(internal->keys[i] == right_min && "Separator must equal min of right subtree");
	}

	/**
	 * @brief Validate leaf chain ordering and linkage.
	 *
	 * Ensures that:
	 * - keys are strictly ascending within each leaf,
	 * - keys are strictly ascending across the entire chain,
	 * - prev_leaf / next_leaf links are consistent.
	 */
	void ValidateLeafChain() const
	{
		Leaf *leaf = this->LeftmostLeaf();
		Leaf *prev = nullptr;
		Tkey prev_key{}; // default-constructed sentinel
		bool has_prev = false;

		while (leaf != nullptr) {
			/* Capacity bounds */
			assert(leaf->count <= 64);

			/* Keys strictly ascending within leaf */
			this->AssertStrictlyAscending(leaf->keys, leaf->count);

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
#endif /* BPLUSTREE_CHECK */

	/**
	 * @brief Return the minimum key in the subtree rooted at @p node.
	 *
	 * Descends to the leftmost leaf of the subtree and returns the first key.
	 *
	 * @param node Subtree root.
	 * @return Reference to the minimum key in the subtree.
	 */
	const Tkey &SubTreeMin(Node *node) const
	{
		assert(node != nullptr);

		Node *cur = node;

		/* Descend until we reach a leaf */
		while (!cur->is_leaf) {
			Internal *internal = static_cast<Internal *>(cur);
			assert(internal->children[0] != nullptr);
			cur = internal->children[0].get();
		}

		Leaf *leaf = static_cast<Leaf *>(cur);
		assert(leaf->count > 0);

		return leaf->keys[0];
	}

	/**
	 * @brief Generic sequence-to-string helper for debugging.
	 *
	 * Provides special handling for pair-like types by formatting them
	 * as "(first,second)"; otherwise relies on fmt::format for elements.
	 *
	 * @tparam T Sequence element type.
	 * @param sequence Element or pair-like type to format.
	 * @return String representation for debugging output.
	 */
	template <typename T>
	std::string SequenceToString(const T &sequence) const
	{
		if constexpr (requires { sequence.first; sequence.second; }) {
			return fmt::format("({},{})", this->SequenceToString(sequence.first), this->SequenceToString(sequence.second));
		} else {
			return fmt::format("{}", sequence);
		}
	}

	/**
	 * @brief Dump a sequence of keys or values to a formatted string.
	 *
	 * @tparam T      Sequence type supporting operator[].
	 * @param sequence Sequence of elements.
	 * @param count    Number of elements to include.
	 * @return Comma-separated string representation of the sequence.
	 */
	template <typename T>
	std::string DumpSequence(const T &sequence, uint8_t count) const
	{
		std::string out;
		out.reserve(count * 8); // small optimization

		for (uint8_t i = 0; i < count; ++i) {
			if (i > 0) out += ",";
			out += this->SequenceToString(sequence[i]);
		}
		return out;
	}

public:
	/**
	 * @brief Dump a node and its subtree for debugging.
	 *
	 * Prints a textual representation of the B+ tree structure starting
	 * at @p node (or at the root if @p node is nullptr and @p indent is 0).
	 * Uses the Debug() facility to output node type, key counts, keys, and
	 * recursively prints children for internal nodes.
	 *
	 * @param node   Node to dump, or nullptr to start at the root.
	 * @param indent Indentation level in spaces for nested output.
	 */
	void DumpNode(const Node *node = nullptr, int indent = 0) const
	{
		if (node == nullptr) {
			if (indent == 0) {
				node = this->root.get();
			} else {
				Debug(script, 0, "{:{}}null", "", indent);
				return;
			}
		}

		const std::string pad(indent, ' ');

		if (node->is_leaf) {
			const Leaf *leaf = static_cast<const Leaf *>(node);

			Debug(script, 0, "{}Leaf count={} keys=[{}]", pad, leaf->count, this->DumpSequence(leaf->keys, leaf->count));

			if constexpr (!std::is_void_v<Tvalue>) {
				Debug(script, 0, "{}  values=[{}]", pad, this->DumpSequence(leaf->values, leaf->count));
			}

		} else {
			const Internal *internal = static_cast<const Internal *>(node);

			Debug(script, 0, "{}Internal count={} keys=[{}]", pad, internal->count, this->DumpSequence(internal->keys, internal->count));

			/* Children */
			for (uint8_t i = 0; i <= internal->count; ++i) {
				Debug(script, 0, "{}  child[{}] ->", pad, i);
				this->DumpNode(internal->children[i].get(), indent + 4);
			}

			/* Separators */
			for (uint8_t i = 0; i < internal->count; ++i) {
				Node *right = internal->children[i + 1].get();
				if (right != nullptr) {
					Debug(script, 0, "{}  separator[{}]={} (right.min={})", pad, i, SequenceToString(internal->keys[i]), SequenceToString(SubTreeMin(right)));
				}
			}
		}
	}
};

template <typename Tkey, typename Tvalue, typename Allocator>
void BPlusTree<Tkey, Tvalue, Allocator>::NodeDeleter::operator()(Node *node) const noexcept
{
	if (node == nullptr) {
		return;
	}

	if (node->is_leaf) {
		Leaf *leaf = static_cast<Leaf *>(node);
		leaf->~Leaf();
		this->allocator->deallocate(reinterpret_cast<std::byte *>(leaf), sizeof(Leaf));
	} else {
		Internal *internal = static_cast<Internal *>(node);
		internal->~Internal();
		this->allocator->deallocate(reinterpret_cast<std::byte *>(internal), sizeof(Internal));
	}
}

#endif /* BPLUSTREE_TYPE_HPP */
