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
template <typename Tkey, typename Tvalue = void, uint8_t B = 64>
class BPlusTree;

/**
 * Leaf node for map mode
 * Forward declare Internal before Leaf
 */
template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusInternalMap;

template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusLeafMap {
	uint8_t count{};
	uint8_t index_in_parent{};

	std::array<Tkey, B> keys;
	std::array<Tvalue, B> values;

	BPlusLeafMap<Tkey, Tvalue, B> *next_leaf = nullptr;
	BPlusLeafMap<Tkey, Tvalue, B> *prev_leaf = nullptr;
	BPlusInternalMap<Tkey, Tvalue, B> *parent = nullptr;
};

/**
 * Internal node for map mode
 */
template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusInternalMap {
	uint8_t count{};
	uint8_t index_in_parent{};

	std::array<Tkey, B> keys;
	std::array<std::unique_ptr<std::variant<BPlusLeafMap<Tkey, Tvalue, B>, BPlusInternalMap<Tkey, Tvalue, B>>>, B + 1> children;

	BPlusInternalMap<Tkey, Tvalue, B> *parent = nullptr;
};

/**
 * Leaf node for set mode
 * Forward declare Internal before Leaf
 */
template <typename Tkey, uint8_t B>
struct BPlusInternalSet;

template <typename Tkey, uint8_t B>
struct BPlusLeafSet {
	uint8_t count{};
	uint8_t index_in_parent{};

	std::array<Tkey, B> keys;

	BPlusLeafSet<Tkey, B> *next_leaf = nullptr;
	BPlusLeafSet<Tkey, B> *prev_leaf = nullptr;
	BPlusInternalSet<Tkey, B> *parent = nullptr;
};

/**
 * Internal node for set mode
 */
template <typename Tkey, uint8_t B>
struct BPlusInternalSet {
	uint8_t count{};
	uint8_t index_in_parent{};

	std::array<Tkey, B> keys;
	std::array<std::unique_ptr<std::variant<BPlusLeafSet<Tkey, B>, BPlusInternalSet<Tkey, B>>>, B + 1> children;

	BPlusInternalSet<Tkey, B> *parent = nullptr;
};

template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusLeafSelector {
	using type = std::conditional_t<std::is_void_v<Tvalue>,
		BPlusLeafSet<Tkey, B>,
		BPlusLeafMap<Tkey, Tvalue, B>
	>;
};

template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusInternalSelector {
	using type = std::conditional_t<std::is_void_v<Tvalue>,
		BPlusInternalSet<Tkey, B>,
		BPlusInternalMap<Tkey, Tvalue, B>
	>;
};

template <typename Tkey, typename Tvalue, uint8_t B>
struct BPlusNodeTraits {
	using Leaf = typename BPlusLeafSelector<Tkey, Tvalue, B>::type;
	using Internal = typename BPlusInternalSelector<Tkey, Tvalue, B>::type;
	using Node = std::variant<Leaf, Internal>;
};

template <typename Tkey, typename Tvalue, uint8_t B>
class BPlusTree {
private:
	using Traits = BPlusNodeTraits<Tkey, Tvalue, B>;
	using Leaf = typename Traits::Leaf;
	using Internal = typename Traits::Internal;
	using Node = typename Traits::Node;

	static constexpr uint8_t MIN_LEAF = (B + 1) / 2; // ceil(B/2)
	static constexpr uint8_t MIN_INTERNAL = (B - 1) / 2; // ceil(B/2) - 1

	std::unique_ptr<Node> root;

public:
	BPlusTree() : root(std::make_unique<Node>(Leaf{}))
	{
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
		this->root = std::make_unique<Node>(Leaf{});
	}

	bool empty() const noexcept
	{
		assert(this->root != nullptr);

		return std::visit([](const auto &n) {
			return n.count == 0;
		}, *this->root);
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

				/* Root invariants: parent=nullptr, index_in_parent=0 */
				std::visit([](auto &n) {
					n.parent = nullptr;
					n.index_in_parent = 0;
				}, *this->root);

				this->rebuild_leaf_chain(leaves);
			}
		}
#if BPLUSTREE_CHECK
		auto *p = this->root.get();
		std::function<void(Node *)> verify = [&](Node *n) {
			if (n == nullptr) {
				return;
			}
			std::visit([&](auto &node) {
				using T = std::decay_t<decltype(node)>;
				if constexpr (std::is_same_v<T, Internal>) {
					for (uint8_t j = 0; j <= node.count; ++j) {
						if (node.children[j] != nullptr) {
							auto *child = node.children[j].get();
							std::visit([&]([[maybe_unused]] auto &c) {
								assert(c.parent == &node);
								assert(c.index_in_parent == j);
							}, *child);
							verify(child);
						}
					}
				}
			}, *n);
		};
		if (p != nullptr) {
			verify(p);
		}
#endif
		return *this;
	}

private:
	size_t count_recursive(const Node *node) const
	{
		if (node == nullptr) {
			return 0;
		}

		return std::visit([this](auto const &n) -> size_t {
			using T = std::decay_t<decltype(n)>;

			if constexpr (std::is_same_v<T, Leaf>) {
				/* Leaf: just return number of elements */
				return n.count;
			} else {
				/* Internal: recurse into children */
				size_t total = 0;
				for (uint8_t i = 0; i <= n.count; ++i) {
					total += this->count_recursive(n.children[i].get());
				}
				return total;
			}
		}, *node);
	}

	std::unique_ptr<Node> clone_node(const Node *src, Internal *parent, uint8_t slot, std::vector<Leaf *> &leaves)
	{
		/* Create an empty Node variant we will fill */
		auto dst = std::make_unique<Node>();

		std::visit([&](const auto &src_alt) {
			using T = std::decay_t<decltype(src_alt)>;

			if constexpr (std::is_same_v<T, Leaf>) {
				/* Emplace Leaf alternative and get a reference */
				dst->template emplace<Leaf>();
				auto &dst_leaf = std::get<Leaf>(*dst);

				dst_leaf.count = src_alt.count;
				dst_leaf.keys = src_alt.keys;
				if constexpr (!std::is_void_v<Tvalue>) {
					dst_leaf.values = src_alt.values;
				}
				dst_leaf.parent = parent;
				dst_leaf.index_in_parent = (parent != nullptr ? slot : 0);

				/* Collect pointer to the DESTINATION leaf (address inside variant) */
				leaves.push_back(&dst_leaf);

			} else {
				/* Emplace Internal alternative and get a reference */
				dst->template emplace<Internal>();
				auto &dst_internal = std::get<Internal>(*dst);

				dst_internal.count = src_alt.count;
				dst_internal.keys = src_alt.keys;
				dst_internal.parent = parent;
				dst_internal.index_in_parent = (parent != nullptr ? slot : 0);

				for (uint8_t i = 0; i <= src_alt.count; ++i) {
					if (src_alt.children[i] != nullptr) {
						auto child_clone = this->clone_node(src_alt.children[i].get(), &dst_internal, i, leaves);
						dst_internal.children[i] = std::move(child_clone);

						/* Fix child's parent and index_in_parent */
						if (dst_internal.children[i]) {
							std::visit([&](auto &child_alt) {
								child_alt.parent = &dst_internal;
								child_alt.index_in_parent = i;
							}, *dst_internal.children[i]);
						}
					} else {
						dst_internal.children[i].reset();
					}
				}
			}
		}, *src);

		return dst;
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
				return { this->leaf_->keys[this->index_], this->leaf_->values[this->index_] }; // map mode (const V &)
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

		Node *node = root.get();

		/* Descend while we’re in an internal node */
		while (Internal *internal = std::get_if<Internal>(node)) {
			uint8_t i = this->upper_bound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = internal->children[i].get();
		}

		/* At this point, node must be a leaf */
		Leaf *leaf = std::get_if<Leaf>(node);
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
			return { it, false }; // already exists
		}

		/* Perform insert (may split) */
		this->insert(leaf, idx, key, value);

		VALIDATE_NODES();

		/* Iterator mapping:
		 * - If no split: new element sits at idx in leaf.
		 * - If split happened: leaf may be left; new right leaf is leaf->next_leaf or via parent rewiring.
		 *   Use key compare against split pivot to choose side deterministically. */

		/* Fast path: still in same leaf */
		if (idx < leaf->count && leaf->keys[idx] == key) {
			return { it, true };
		}

		/* Split-aware fallback: check right neighbour */
		Leaf *r = leaf->next_leaf;
		uint8_t j = idx - leaf->count;

		it = iterator(r, j, this);
		assert(this->verify_return_iterator(it, key));

		return { it, true };
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
			return { it, false }; // already exists
		}

		/* Perform insert (may split) */
		this->insert(leaf, idx, key);

		VALIDATE_NODES();

		/* Iterator mapping:
		 * - If no split: new element sits at idx in leaf.
		 * - If split happened: leaf may be left; new right leaf is leaf->next_leaf or via parent rewiring.
		 *   Use key compare against split pivot to choose side deterministically. */

		/* Fast path: still in same leaf */
		if (idx < leaf->count && leaf->keys[idx] == key) {
			return { it, true };
		}

		/* Split-aware fallback: check right neighbour */
		Leaf *r = leaf->next_leaf;
		uint8_t j = idx - leaf->count;

		it = iterator(r, j, this);
		assert(this->verify_return_iterator(it, key));

		return { it, true };
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
		--leaf->count;

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
			uint8_t child_idx = this->find_child_index(leaf->parent, leaf);
			if (leaf->parent->count > 0) {
				if (child_idx > 0) {
					this->refresh_boundary_upward(leaf->parent, child_idx - 1);
				} else {
					this->refresh_boundary_upward(leaf->parent, 0);
				}
			} else {
				this->refresh_boundary_upward(leaf->parent, 0);
			}
		}

		/* Fix underflow, passing iterator by reference */
		if (leaf->parent != nullptr && leaf->count < MIN_LEAF) {
			uint8_t child_idx = this->find_child_index(leaf->parent, leaf);
			if (child_idx <= leaf->parent->count) {
				this->fix_underflow(leaf->parent, child_idx, succ_it);
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
		uint8_t succ_idx = this->lower_bound(succ_leaf->keys, succ_leaf->count, succ_key);

		return a.leaf_ == succ_leaf && a.index_ == succ_idx;
	}

	Leaf *leftmost_leaf() const
	{
		assert(this->root != nullptr);

		Node *node = root.get();

		/* Descend leftmost children while internal */
		while (Internal *internal = std::get_if<Internal>(node)) {
			assert(internal->children[0] != nullptr);
			node = internal->children[0].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = std::get_if<Leaf>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	Leaf *rightmost_leaf() const
	{
		assert(this->root != nullptr);

		Node *node = root.get();

		/* Descend rightmost children while internal */
		while (Internal *internal = std::get_if<Internal>(node)) {
			assert(internal->children[internal->count] != nullptr);
			node = internal->children[internal->count].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = std::get_if<Leaf>(node);
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

		Node *node = parent->children[child->index_in_parent].get();
		assert(node != nullptr);

		/* Verify that the variant at that slot really holds the same child */
		[[maybe_unused]] auto *in_slot = std::get_if<T>(node);
		assert(in_slot == child);

		return child->index_in_parent;
	}

	/**
	 * Map mode insert: enabled only if Tvalue is not void
	 */
	template <typename U = Tvalue>
	std::enable_if_t<!std::is_void_v<U>, void> insert(Leaf *leaf, uint8_t i, const Tkey &key, const U &value)
	{
		/* Shift right */
		std::move_backward(leaf->keys.begin() + i, leaf->keys.begin() + leaf->count, leaf->keys.begin() + leaf->count + 1);
		std::move_backward(leaf->values.begin() + i, leaf->values.begin() + leaf->count, leaf->values.begin() + leaf->count + 1);

		leaf->keys[i] = key;
		leaf->values[i] = value;
		++leaf->count;

		/* Centralized separator refresh */
		if (i == 0 && leaf->parent != nullptr) {
			uint8_t child_idx = this->find_child_index(leaf->parent, leaf);
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
	std::enable_if_t<std::is_void_v<U>, void> insert(Leaf *leaf, uint8_t i, const Tkey &key)
	{
		/* Shift right */
		std::move_backward(leaf->keys.begin() + i, leaf->keys.begin() + leaf->count, leaf->keys.begin() + leaf->count + 1);

		leaf->keys[i] = key;
		++leaf->count;

		/* Centralized separator refresh */
		if (i == 0 && leaf->parent != nullptr) {
			uint8_t child_idx = this->find_child_index(leaf->parent, leaf);
			if (child_idx > 0) {
				this->maintain_parent_boundary(leaf->parent, child_idx - 1);
			}
		}

		if (leaf->count == B) {
			this->split_leaf(leaf);
		}
	}

	Leaf *find_leaf(const Tkey &key) const
	{
		assert(this->root != nullptr);

		Node *node = this->root.get();

		/* Descend while internal */
		while (Internal *internal = std::get_if<Internal>(node)) {
			uint8_t i = this->upper_bound(internal->keys, internal->count, key);
			assert(internal->children[i] != nullptr);
			node = internal->children[i].get();
		}

		/* Must be a leaf now */
		Leaf *leaf = std::get_if<Leaf>(node);
		assert(leaf != nullptr);
		return leaf;
	}

	void rewire_children_parent(Internal *parent)
	{
		if (parent == nullptr) {
			return;
		}

		for (uint8_t i = 0; i <= parent->count; ++i) {
			if (parent->children[i] != nullptr) {
				Node *child = parent->children[i].get();

				std::visit([&]([[maybe_unused]] auto &child_alt) {
					/* Both Leaf and Internal have parent + index_in_parent */
					assert(child_alt.parent == parent);
					assert(child_alt.index_in_parent == i);
				}, *child);
			}
		}
	}

	void split_leaf(Leaf *leaf)
	{
		assert(leaf != nullptr);
		uint8_t mid = leaf->count / 2;

		/* Create a new Leaf node */
		auto new_leaf_node = std::make_unique<Node>(Leaf{});
		Leaf *new_leaf = std::get_if<Leaf>(new_leaf_node.get());
		assert(new_leaf != nullptr);

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

	template <typename T>
	void insert_into_parent(T *left, const Tkey &separator, std::unique_ptr<Node> right_node)
	{
		static_assert(std::is_same_v<T, Leaf> || std::is_same_v<T, Internal>);
		Internal *parent = left->parent;

		/* Root case: parent == nullptr → create fresh internal root */
		if (parent == nullptr) {
			auto new_root_ptr = std::make_unique<Node>(Internal{});
			Internal *new_root = std::get_if<Internal>(new_root_ptr.get());
			assert(new_root != nullptr);

			/* Promote old root (which contains 'left') and right_node under new_root */
			std::unique_ptr<Node> old_root = std::move(this->root);

			/* Assert that old_root holds the same alternative as 'left' */
			if constexpr (std::is_same_v<T, Leaf>) {
				assert(std::get_if<Leaf>(old_root.get()) == left);
			} else {
				assert(std::get_if<Internal>(old_root.get()) == left);
			}

			new_root->keys[0] = separator;
			new_root->children[0] = std::move(old_root);
			new_root->children[1] = std::move(right_node);
			new_root->count = 1;

			std::visit([&](auto &alt) {
				alt.parent = new_root;
				alt.index_in_parent = 0;
			}, *new_root->children[0]);
			std::visit([&](auto &alt) {
				alt.parent = new_root;
				alt.index_in_parent = 1;
			}, *new_root->children[1]);

			this->root = std::move(new_root_ptr);
			this->rewire_children_parent(std::get_if<Internal>(this->root.get()));
			return;
		}

		/* Parent exists: ensure space */
		while (parent->count == (B - 1)) {
			this->split_internal(parent);
			parent = left->parent; // left may have moved
			assert(parent != nullptr);
		}

		uint8_t i = this->find_child_index(parent, left);

		std::move_backward(parent->keys.begin() + i, parent->keys.begin() + parent->count, parent->keys.begin() + parent->count + 1);
		std::move_backward(parent->children.begin() + i + 1, parent->children.begin() + parent->count + 1, parent->children.begin() + parent->count + 2);

		for (uint8_t j = i + 1; j <= parent->count + 1; ++j) {
			if (parent->children[j] != nullptr) {
				std::visit([&](auto &alt) {
					alt.parent = parent;
					alt.index_in_parent = j;
				}, *parent->children[j]);
			}
		}

		parent->keys[i] = separator;
		assert(parent->children[i + 1] == nullptr);
		parent->children[i + 1] = std::move(right_node);
		std::visit([&](auto &alt) {
			alt.parent = parent;
			alt.index_in_parent = i + 1;
		}, *parent->children[i + 1]);

		++parent->count;
		this->rewire_children_parent(parent);
	}

	void split_internal(Internal *node) {
		assert(node != nullptr);
		uint8_t old_count = node->count;
		uint8_t mid = old_count / 2;
		assert(mid < old_count);

		/* Separator promoted to parent */
		Tkey separator = node->keys[mid];

		/* Create new right internal node (as variant Node holding Internal) */
		auto right_node = std::make_unique<Node>(Internal{});
		Internal *right = std::get_if<Internal>(right_node.get());
		assert(right != nullptr);

		/* Move keys: everything after mid goes into right
		 * Left keeps keys [0..mid - 1], right gets keys [mid + 1..old_count - 1] */
		std::move(node->keys.begin() + mid + 1, node->keys.begin() + old_count, right->keys.begin());
		right->count = old_count - mid - 1;

		/* Move children: everything after mid goes into right
		 * Left keeps children [0..mid], right gets children [mid + 1..old_count] */
		std::move(node->children.begin() + mid + 1, node->children.begin() + old_count + 1, right->children.begin());

		/* Fix parent/index_in_parent for moved children in right */
		for (uint8_t j = 0; j <= right->count; ++j) {
			if (right->children[j] != nullptr) {
				std::visit([&](auto &alt) {
					alt.parent = right;
					alt.index_in_parent = j;
				}, *right->children[j]);
			}
		}

		/* Left node keeps first mid keys and mid + 1 children */
		node->count = mid;

		/* Clear any dangling child slots beyond mid in left */
		std::fill(node->children.begin() + mid + 1, node->children.begin() + B + 1, nullptr);

		/* Ensure remaining children in left are wired correctly */
		this->rewire_children_parent(node);

		assert(node->count == mid);
		assert(right->count > 0);

		/* Insert separator and right child into parent (unique_ptr-based API) */
		this->insert_into_parent(node, separator, std::move(right_node));

		/* After insertion, parent’s children changed; defensively rewire */
		if (node->parent != nullptr) {
			this->rewire_children_parent(node->parent);
		}
	}

	/**
	 * Refresh the separator at sep_idx in 'parent' to match right subtree min,
	 * then, if that changed the ancestor view, propagate the change upward.
	 */
	void refresh_boundary_upward(Internal *parent, uint8_t sep_idx)
	{
		if (parent == nullptr) {
			return;
		}

		/* 1) Refresh at this parent only if there is a valid separator */
		if (sep_idx < parent->count) {
			this->maintain_parent_boundary(parent, sep_idx);
		}

		/* 2) Propagate upward along the leftmost path */
		for (Internal *p = parent; p != nullptr && p->parent != nullptr; p = p->parent) {
			Internal *gp = p->parent;
			uint8_t idx_in_gp = this->find_child_index(gp, p);

			/* If this subtree sits to the right of some separator in gp,
			 * refresh that ancestor separator. */
			if (idx_in_gp > 0 && gp->count > 0) {
				this->maintain_parent_boundary(gp, idx_in_gp - 1);
				break; // not on leftmost path anymore
			}
		}
	}

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
		if (right == nullptr) {
			/* After a merge, the right child may have been removed explicitly */
			return;
		}

		/* Case 1: right child is a Leaf */
		if (Leaf *right_leaf = std::get_if<Leaf>(right); right_leaf != nullptr) {
			if (right_leaf->count > 0) {
				parent->keys[sep_idx] = right_leaf->keys[0];
			} else {
				/* Empty right leaf should not persist */
				assert(false && "Empty right leaf should have been removed in merge");
			}
		/* Case 2: right child is an Internal */
		} else if (Internal *right_internal = std::get_if<Internal>(right); right_internal != nullptr) {
			Node *cur = right;
			/* Descend to leftmost leaf of right subtree */
			while (Internal *internal = std::get_if<Internal>(cur)) {
				assert(internal->children[0] != nullptr);
				cur = internal->children[0].get();
			}
			Leaf *leaf = std::get_if<Leaf>(cur);
			assert(leaf != nullptr && leaf->count > 0);
			parent->keys[sep_idx] = leaf->keys[0];
		}
	}

	/**
	 * Borrow the first key of right into the end of leaf
	 */
	void borrow_from_right_leaf(Internal *parent, uint8_t child_idx, iterator &succ_it)
	{
		/* Get left and right children as Node * */
		Node *leaf_node = parent->children[child_idx].get();
		Node *right_node = parent->children[child_idx + 1].get();

		Leaf *leaf = std::get_if<Leaf>(leaf_node);
		Leaf *right = std::get_if<Leaf>(right_node);

		assert(leaf != nullptr && right != nullptr);
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
	 * Borrow the last key of left into the front of leaf
	 */
	void borrow_from_left_leaf(Internal *parent, uint8_t child_idx, iterator &succ_it)
	{
		assert(parent != nullptr);
		assert(child_idx > 0);

		Node *leaf_node = parent->children[child_idx].get();
		Node *left_node = parent->children[child_idx - 1].get();

		Leaf *leaf = std::get_if<Leaf>(leaf_node);
		Leaf *left = std::get_if<Leaf>(left_node);

		assert(leaf != nullptr && left != nullptr);
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

		Node *left_node = parent->children[i].get();
		Node *right_node = parent->children[i + 1].get();

		Leaf *left = std::get_if<Leaf>(left_node);
		Leaf *right = std::get_if<Leaf>(right_node);

		/* Defensive: both must be leaves */
		assert(left != nullptr && right != nullptr);

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
			if (parent->children[j] != nullptr) {
				std::visit([&](auto &alt) {
					alt.parent = parent;
					alt.index_in_parent = j;
				}, *parent->children[j]);
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

	/**
	 * Prefer a leaf-specific signature to avoid variant unwraps at call sites.
	 */
	inline bool can_merge_leaf(const Leaf *left, const Leaf *right)
	{
		assert(left != nullptr && right != nullptr);
		return (left->count + right->count) <= B; // leaf capacity
	}

	void fix_underflow_leaf_child(Internal *parent, uint8_t i, iterator &succ_it)
	{
		assert(parent != nullptr);
		assert(i <= parent->count);

		/* Current leaf child */
		Node *child_node = parent->children[i].get();
		Leaf *child = std::get_if<Leaf>(child_node);
		assert(child != nullptr);

		/* Try borrow from right sibling */
		if (i < parent->count) {
			Node *right_node = parent->children[i + 1].get();
			Leaf *right = std::get_if<Leaf>(right_node);
			if (right != nullptr && right->count > MIN_LEAF) {
				this->borrow_from_right_leaf(parent, i, succ_it);
				this->refresh_boundary_upward(parent, i);
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Try borrow from left sibling */
		if (i > 0) {
			Node *left_node = parent->children[i - 1].get();
			Leaf *left = std::get_if<Leaf>(left_node);
			if (left != nullptr && left->count > MIN_LEAF) {
				this->borrow_from_left_leaf(parent, i, succ_it);
				this->refresh_boundary_upward(parent, i - 1);
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Merge path */
		if (i < parent->count) {
			Node *right_node = parent->children[i + 1].get();
			Leaf *right = std::get_if<Leaf>(right_node);
			if (right != nullptr && this->can_merge_leaf(child, right)) {
				this->merge_leaf_keep_left(parent, i, succ_it);
				if (i < parent->count) {
					this->refresh_boundary_upward(parent, i);
				}
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow from left if possible */
			if (i > 0) {
				Node *left_node = parent->children[i - 1].get();
				Leaf *left = std::get_if<Leaf>(left_node);
				if (left != nullptr && left->count > MIN_LEAF) {
					this->borrow_from_left_leaf(parent, i, succ_it);
					this->refresh_boundary_upward(parent, i - 1);
					this->fix_internal_underflow_cascade(parent);
					return;
				}
			}

			/* Last resort: force merge into left */
			assert(i > 0 && "Right merge overflow and no left sibling to merge into");
			this->merge_leaf_keep_left(parent, i - 1, succ_it);
			if ((i - 1) < parent->count) {
				this->refresh_boundary_upward(parent, i - 1);
			}
			this->fix_internal_underflow_cascade(parent);
			return;
		} else {
			/* Rightmost child: must merge into left */
			assert(i > 0);
			Node *left_node = parent->children[i - 1].get();
			Leaf *left = std::get_if<Leaf>(left_node);
			if (left != nullptr && this->can_merge_leaf(left, child)) {
				this->merge_leaf_keep_left(parent, i - 1, succ_it);
				if ((i - 1) < parent->count) {
					this->refresh_boundary_upward(parent, i - 1);
				}
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow from left */
			if (left != nullptr && left->count > MIN_LEAF) {
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

		if (std::get_if<Leaf>(child)) {
			this->fix_underflow_leaf_child(parent, i, succ_it);
		} else {
			this->fix_underflow_internal_child(parent, i);
		}
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

		Node *child_node = parent->children[i].get();
		Node *right_node = parent->children[i + 1].get();

		Internal *child = std::get_if<Internal>(child_node);
		Internal *right = std::get_if<Internal>(right_node);

		assert(child != nullptr && right != nullptr);

		/* Move parent key down into child */
		child->keys[child->count] = std::move(parent->keys[i]);

		/* Move right’s first child into child */
		child->children[child->count + 1] = std::move(right->children[0]);
		if (child->children[child->count + 1] != nullptr) {
			std::visit([&](auto &alt) {
				alt.parent = child;
				alt.index_in_parent = child->count + 1;
			}, *child->children[child->count + 1]);
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
			if (right->children[c] != nullptr) {
				std::visit([&](auto &alt) {
					alt.parent = right;
					alt.index_in_parent = c;
				}, *right->children[c]);
			}
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

		Node *child_node = parent->children[i].get();
		Node *left_node = parent->children[i - 1].get();

		Internal *child = std::get_if<Internal>(child_node);
		Internal *left = std::get_if<Internal>(left_node);

		assert(child != nullptr && left != nullptr);

		/* Shift child’s keys right to open slot at 0 */
		std::move_backward(child->keys.begin(), child->keys.begin() + child->count, child->keys.begin() + child->count + 1);

		/* Shift child’s children right to open slot at 0 */
		std::move_backward(child->children.begin(), child->children.begin() + child->count + 1, child->children.begin() + child->count + 2);

		/* Fix parent/index_in_parent pointers for shifted children */
		for (uint8_t c = 1; c <= child->count + 1; ++c) {
			if (child->children[c] != nullptr) {
				std::visit([&](auto &alt) {
					alt.parent = child;
					alt.index_in_parent = c;
				}, *child->children[c]);
			}
		}

		/* Move parent key down into child[0] */
		child->keys[0] = std::move(parent->keys[i - 1]);

		/* Move left’s last child into child[0] */
		child->children[0] = std::move(left->children[left->count]);
		if (child->children[0] != nullptr) {
			std::visit([&](auto &alt) {
				alt.parent = child;
				alt.index_in_parent = 0;
			}, *child->children[0]);
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

		Node *left_node = parent->children[i].get();
		Node *right_node = parent->children[i + 1].get();

		Internal *left = std::get_if<Internal>(left_node);
		Internal *right = std::get_if<Internal>(right_node);
		assert(left != nullptr && right != nullptr);

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
			auto &slot = left->children[left->count + 1 + c];
			if (slot != nullptr) {
				std::visit([&](auto &alt) {
					alt.parent = left;
					alt.index_in_parent = left->count + 1 + c;
				}, *slot);
			}
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

		Node *left_node = parent->children[i - 1].get();
		Node *child_node = parent->children[i].get();

		Internal *left = std::get_if<Internal>(left_node);
		Internal *child = std::get_if<Internal>(child_node);
		assert(left != nullptr && child != nullptr);

		/* Guard: if merge would overflow, fall back to borrow-from-left */
		if ((left->count + 1 + child->count) > (B - 1)) {
			this->borrow_from_left_internal(parent, i);
			if ((i - 1) < parent->count) {
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
			auto &slot = left->children[left->count + 1 + c];
			if (slot != nullptr) {
				std::visit([&](auto &alt) {
					alt.parent = left;
					alt.index_in_parent = left->count + 1 + c;
				}, *slot);
			}
		}

		left->count += 1 + child->count;

		/* Remove separator i - 1 and child i from parent */
		this->remove_separator_and_right_child(parent, i - 1);

		/* Defensive rewiring */
		this->rewire_children_parent(left);
		this->rewire_children_parent(parent);

		/* Boundary refresh: separator at i - 1 may now reflect a different right-min,
		 * or if out of range, refresh the last separator. */
		if ((i - 1) < parent->count) {
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

		Node *child_node = parent->children[i].get();
		Internal *child = std::get_if<Internal>(child_node);
		assert(child != nullptr && "Child at i must be Internal for this path");

		/* Borrow from right if possible */
		if (i < parent->count) {
			Node *right_node = parent->children[i + 1].get();
			Internal *right = std::get_if<Internal>(right_node);
			if (right != nullptr && right->count > MIN_INTERNAL) {
				this->borrow_from_right_internal(parent, i);
				this->refresh_boundary_upward(parent, i);
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Borrow from left if possible */
		if (i > 0) {
			Node *left_node = parent->children[i - 1].get();
			Internal *left = std::get_if<Internal>(left_node);
			if (left != nullptr && left->count > MIN_INTERNAL) {
				this->borrow_from_left_internal(parent, i);
				this->refresh_boundary_upward(parent, i - 1);
				this->fix_internal_underflow_cascade(parent);
				return;
			}
		}

		/* Merge logic */
		if (i < parent->count) {
			Node *right_node = parent->children[i + 1].get();
			Internal *right = std::get_if<Internal>(right_node);
			if (right != nullptr && this->can_merge_internal(child, right)) {
				this->merge_keep_left_internal(parent, i);
				this->refresh_boundary_upward(parent, i);
				this->fix_internal_underflow_cascade(parent);
				return;
			}

			/* Fallback: borrow-left if available */
			if (i > 0) {
				Node *left_node = parent->children[i - 1].get();
				Internal *left = std::get_if<Internal>(left_node);
				if (left != nullptr && left->count > MIN_INTERNAL) {
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
			Node *left_node = parent->children[i - 1].get();
			Internal *left = std::get_if<Internal>(left_node);
			if (left != nullptr && this->can_merge_internal(left, child)) {
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

	void maybe_shrink_height()
	{
		assert(this->root != nullptr);

		/* Case 1: root is a Leaf → nothing to shrink */
		if (std::get_if<Leaf>(this->root.get())) {
			return;
		}

		/* Case 2: root is an Internal */
		Internal *root_internal = std::get_if<Internal>(this->root.get());
		assert(root_internal != nullptr);

		/* If root has no separators, promote its single child */
		if (root_internal->count == 0) {
			std::unique_ptr<Node> child = std::move(root_internal->children[0]);
			assert(child != nullptr);

			/* Reset child's parent to null (new root) */
			std::visit([&](auto &child_alt) {
				child_alt.parent = nullptr;
				child_alt.index_in_parent = 0;
			}, *child);

			this->root = std::move(child);
		} else {
			/* Ensure all children of root are wired correctly */
			this->rewire_children_parent(root_internal);
		}
	}

	/**
	 * If an internal node underflows, borrow/merge upward until root is handled.
	 * Root special case: if root becomes empty and has one child, promote the child.
	 */
	void fix_internal_underflow_cascade(Internal *node)
	{
		/* Stop at root: shrink height if needed and exit */
		if (node == std::get_if<Internal>(this->root.get())) {
			this->maybe_shrink_height();
			return;
		}

		Internal *parent = node->parent;
		assert(parent != nullptr);

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
		/* Linear scan */
		for (uint8_t i = 0; i < count; ++i) {
			if (keys[i] >= key) {
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
		/* Linear scan */
		for (uint8_t i = 0; i < count; ++i) {
			if (keys[i] > key) {
				return i;
			}
		}
		return count;
	}

#if BPLUSTREE_CHECK
	void validate() const
	{
		if (this->root == nullptr) {
			return;
		}

		/* Root invariants */
		if (Leaf *rleaf = std::get_if<Leaf>(this->root.get()); rleaf != nullptr) {
			assert(rleaf->count <= B);
		} else if (Internal *rint = std::get_if<Internal>(this->root.get()); rint != nullptr) {
			assert(rint->count <= B);

			/* Root must have at least one child unless the tree is empty */
			for (uint8_t i = 0; i <= rint->count; ++i) {
				auto &ch = rint->children[i];
				if (ch == nullptr) {
					assert(false && "null child");
				}
				std::visit([&](auto &alt) {
					if (alt.parent != rint) {
						std::cerr << "Child " << i << " has wrong parent "
							<< alt.parent << " expected " << rint << "\n";
						this->dump_node(ch.get(), 2);
						assert(false);
					}
				}, *ch);
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
		if (Leaf *leaf = std::get_if<Leaf>(node); leaf != nullptr) {
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

		Internal *internal = std::get_if<Internal>(node);
		assert(internal != nullptr && "validate_node: node must be Leaf or Internal");

		if (internal->count > 0) {
			/* Internal keys non-decreasing (B+ trees allow equal internal keys via redistribution) */
			for (uint8_t i = 1; i < internal->count; ++i) {
				assert(internal->keys[i - 1] <= internal->keys[i]);
			}
		}

		/* Children count = keys + 1, all non-null and correctly parented */
		for (uint8_t i = 0; i <= internal->count; ++i) {
			auto &ch = internal->children[i];
			assert(ch != nullptr);
			std::visit([&]([[maybe_unused]] auto &alt) {
				assert(alt.parent == internal);
			}, *ch);
		}

		/* Separator consistency: parent key == min of right child */
		for (uint8_t i = 0; i < internal->count; ++i) {
			Node *right = internal->children[i + 1].get();
			assert(right != nullptr);
			/* tolerate transient empty internal if you allow it */
			bool right_has_min = false;
			if (Leaf *rleaf = std::get_if<Leaf>(right); rleaf != nullptr) {
				right_has_min = (rleaf->count > 0);
			} else if (Internal *rint = std::get_if<Internal>(right); rint != nullptr) {
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
		Tkey prev{};
		while (leaf != nullptr) {
			for (uint8_t i = 0; i < leaf->count; ++i) {
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
		if (node == nullptr) {
			return;
		}

		if (Leaf *leaf = std::get_if<Leaf>(node); leaf != nullptr) {
			(void)leaf;
			return; // nothing to validate inside a leaf
		}

		Internal *internal = std::get_if<Internal>(node);
		assert(internal != nullptr);

		for (uint8_t i = 0; i < internal->count; ++i) {
			Node *right = internal->children[i + 1].get();
			assert(right != nullptr);
			const Tkey &right_min = this->subtree_min(right);
			assert(internal->keys[i] == right_min);
		}
		for (uint8_t i = 0; i <= internal->count; ++i) {
			this->validate_separators(internal->children[i].get());
		}
	}

	/* No duplicates across leaves (global check) */
	void assert_no_leaf_duplicates() const
	{
		std::vector<Tkey> leaves;
		for (Leaf *leaf = this->leftmost_leaf(); leaf != nullptr; leaf = leaf->next_leaf) {
			assert(leaf != nullptr && "leftmost_leaf must return a Leaf node");
			for (uint8_t i = 0; i < leaf->count; ++i) {
				leaves.push_back(leaf->keys[i]);
			}
		}
		for (size_t i = 1; i < leaves.size(); ++i) {
			assert(leaves[i - 1] < leaves[i]); // strictly increasing across leaf chain
		}
	}
#endif /* BPLUSTREE_CHECK */

	const Tkey &subtree_min(Node *node) const
	{
		assert(node != nullptr);

		Node *cur = node;
		/* descend until we reach a leaf */
		while (Internal *internal = std::get_if<Internal>(cur)) {
			assert(internal->children[0] != nullptr);
			cur = internal->children[0].get();
		}

		Leaf *leaf = std::get_if<Leaf>(cur);
		assert(leaf != nullptr && leaf->count > 0);
		return leaf->keys[0];
	}

	/**
	 * Generic key-to-string helper
	 */
	template <typename K>
	std::string key_to_string(const K &k) const
	{
		std::ostringstream oss;
		oss << k; // works if K has operator<<
		return oss.str();
	}

	/**
	 * Specialization for std::pair
	 */
	template <typename A, typename B>
	std::string key_to_string(const std::pair<A, B> &p) const
	{
		std::ostringstream oss;
		oss << "(" << p.first << "," << p.second << ")";
		return oss.str();
	}

	void assert_sep(Internal *parent, uint8_t sep_idx) const
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
				<< " parent.count=" << parent->count << "\n";
			this->dump_node(reinterpret_cast<Node *>(parent), 0);
			this->dump_node(right_node, 2);
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

		std::visit([&](auto const &alt) {
			using Alt = std::decay_t<decltype(alt)>;

			if constexpr (std::is_same_v<Alt, Leaf>) {
				std::cerr << pad << "Leaf count=" << alt.count << " keys=[";
				for (uint8_t i = 0; i < alt.count; ++i) {
					std::cerr << this->key_to_string(alt.keys[i]);
					if (i + 1 < alt.count) {
						std::cerr << ",";
					}
				}
				std::cerr << "]\n";

				if constexpr (!std::is_void_v<Tvalue>) {
					std::cerr << pad << "  values=[";
					for (uint8_t i = 0; i < alt.count; ++i) {
						std::cerr << alt.values[i];
						if (i + 1 < alt.count) {
							std::cerr << ",";
						}
					}
					std::cerr << "]\n";
				}

			} else if constexpr (std::is_same_v<Alt, Internal>) {
				std::cerr << pad << "Internal count=" << alt.count << " keys=[";
				for (uint8_t i = 0; i < alt.count; ++i) {
					std::cerr << this->key_to_string(alt.keys[i]);
					if (i + 1 < alt.count) {
						std::cerr << ",";
					}
				}
				std::cerr << "]\n";

				for (uint8_t i = 0; i <= alt.count; ++i) {
					std::cerr << pad << "  child[" << i << "] ->\n";
					this->dump_node(alt.children[i].get(), indent + 4);
				}

				/* Print separators with right.min */
				for (uint8_t i = 0; i < alt.count; ++i) {
					Node *right = alt.children[i + 1].get();
					if (right != nullptr) {
						std::cerr << pad << "  separator[" << i << "]="
							<< this->key_to_string(alt.keys[i])
							<< " (right.min=" << this->key_to_string(this->subtree_min(right)) << ")\n";
					}
				}
			}
		}, *node);
	}
};

#endif /* BPLUSTREE_TYPE_HPP */
