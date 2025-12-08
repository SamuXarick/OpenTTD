/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file script_list.cpp Implementation of ScriptList. */

#include "../../stdafx.h"
#include "script_controller.hpp"
#include "script_list.hpp"
#include "../../debug.h"
#include "../../script/squirrel.hpp"

#include "../../safeguards.h"

/**
 * Base class for any ScriptList sorter.
 */
class ScriptListSorter {
protected:
	ScriptList *list;       ///< The list that's being sorted.
	bool has_no_more_items; ///< Whether we have more items to iterate over.
	std::optional<SQInteger> item_next{}; ///< The next item we will show, or std::nullopt if there are no more items to iterate over.

public:
	/**
	 * Virtual dtor, needed to mute warnings.
	 */
	virtual ~ScriptListSorter() = default;

	/**
	 * Get the first item of the sorter.
	 */
	virtual std::optional<SQInteger> Begin() = 0;

	/**
	 * Stop iterating a sorter.
	 */
	void End()
	{
		this->item_next = std::nullopt;
		this->has_no_more_items = true;
	}

	/**
	 * Get the next item of the sorter.
	 */
	std::optional<SQInteger> Next()
	{
		if (this->IsEnd()) return std::nullopt;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * See if the sorter has reached the end.
	 */
	bool IsEnd()
	{
		return this->list->items.empty() || this->has_no_more_items;
	}

	/**
	 * Callback from the list if an item gets removed.
	 */
	void Remove(SQInteger item)
	{
		if (this->IsEnd()) return;

		/* If we remove the 'next' item, skip to the next */
		if (item == this->item_next) this->FindNext();
	}

	/**
	 * Attach the sorter to a new list and update internal iterators so they remain valid
	 * in the context of the new list. This assumes the content of the old list has been
	 * moved to the new list.
	 * @param new_list New list to attach to and update internal iterators.
	 */
	virtual void Retarget(ScriptList *new_list) = 0;

private:
	/**
	 * Find the next item, and store that information.
	 */
	virtual void FindNext() = 0;
};

/**
 * Sort by value, ascending.
 */
class ScriptListSorterValueAscending : public ScriptListSorter {
private:
	ScriptList::ScriptListValueSet::iterator value_iter;  ///< The iterator over the value list.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterValueAscending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->values.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->value_iter = this->list->values.begin();
		this->item_next = this->value_iter->second;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		this->item_next = std::nullopt;
		if (this->value_iter == this->list->values.end()) {
			this->has_no_more_items = true;
			return;
		}
		++this->value_iter;
		if (this->value_iter != this->list->values.end()) this->item_next = this->value_iter->second;
	}

	void Retarget(ScriptList *new_list) override
	{
		this->list = new_list;
		if (this->item_next) {
			auto item_iter = this->list->items.find(*this->item_next);
			SQInteger value = item_iter->second;
			this->value_iter = this->list->values.find({value, *this->item_next});
		} else {
			this->value_iter = this->list->values.end();
		}
	}
};

/**
 * Sort by value, descending.
 */
class ScriptListSorterValueDescending : public ScriptListSorter {
private:
	/* Note: We cannot use reverse_iterator.
	 *       The iterators must only be invalidated when the element they are pointing to is removed.
	 *       This only holds for forward iterators. */
	ScriptList::ScriptListValueSet::iterator value_iter;  ///< The iterator over the value list.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterValueDescending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->values.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->value_iter = this->list->values.end();
		--this->value_iter;
		this->item_next = this->value_iter->second;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		this->item_next = std::nullopt;
		if (this->value_iter == this->list->values.end()) {
			this->has_no_more_items = true;
			return;
		}
		if (this->value_iter == this->list->values.begin()) {
			/* Use 'end' as marker for 'beyond begin' */
			this->value_iter = this->list->values.end();
		} else {
			--this->value_iter;
		}
		if (this->value_iter != this->list->values.end()) this->item_next = this->value_iter->second;
	}

	void Retarget(ScriptList *new_list) override
	{
		this->list = new_list;
		if (this->item_next) {
			auto item_iter = this->list->items.find(*this->item_next);
			SQInteger value = item_iter->second;
			this->value_iter = this->list->values.find({value, *this->item_next});
		} else {
			this->value_iter = this->list->values.end();
		}
	}
};

/**
 * Sort by item, ascending.
 */
class ScriptListSorterItemAscending : public ScriptListSorter {
private:
	ScriptList::ScriptListMap::iterator item_iter; ///< The iterator over the items in the map.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterItemAscending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->items.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->item_iter = this->list->items.begin();
		this->item_next = this->item_iter->first;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		this->item_next = std::nullopt;
		if (this->item_iter == this->list->items.end()) {
			this->has_no_more_items = true;
			return;
		}
		++this->item_iter;
		if (this->item_iter != this->list->items.end()) this->item_next = this->item_iter->first;
	}

	void Retarget(ScriptList *new_list) override
	{
		this->list = new_list;
		if (this->item_next) {
			this->item_iter = this->list->items.find(*this->item_next);
		} else {
			this->item_iter = this->list->items.end();
		}
	}
};

/**
 * Sort by item, descending.
 */
class ScriptListSorterItemDescending : public ScriptListSorter {
private:
	/* Note: We cannot use reverse_iterator.
	 *       The iterators must only be invalidated when the element they are pointing to is removed.
	 *       This only holds for forward iterators. */
	ScriptList::ScriptListMap::iterator item_iter; ///< The iterator over the items in the map.

public:
	/**
	 * Create a new sorter.
	 * @param list The list to sort.
	 */
	ScriptListSorterItemDescending(ScriptList *list)
	{
		this->list = list;
		this->End();
	}

	std::optional<SQInteger> Begin() override
	{
		if (this->list->items.empty()) {
			this->item_next = std::nullopt;
			return std::nullopt;
		}
		this->has_no_more_items = false;

		this->item_iter = this->list->items.end();
		--this->item_iter;
		this->item_next = this->item_iter->first;

		std::optional<SQInteger> item_current = this->item_next;
		this->FindNext();
		return item_current;
	}

	/**
	 * Find the next item, and store that information.
	 */
	void FindNext() override
	{
		this->item_next = std::nullopt;
		if (this->item_iter == this->list->items.end()) {
			this->has_no_more_items = true;
			return;
		}
		if (this->item_iter == this->list->items.begin()) {
			/* Use 'end' as marker for 'beyond begin' */
			this->item_iter = this->list->items.end();
		} else {
			--this->item_iter;
		}
		if (this->item_iter != this->list->items.end()) this->item_next = this->item_iter->first;
	}

	void Retarget(ScriptList *new_list) override
	{
		this->list = new_list;
		if (this->item_next) {
			this->item_iter = this->list->items.find(*this->item_next);
		} else {
			this->item_iter = this->list->items.end();
		}
	}
};



bool ScriptList::SaveObject(HSQUIRRELVM vm)
{
	sq_pushstring(vm, "List");
	sq_newarray(vm, 0);
	sq_pushinteger(vm, this->sorter_type);
	sq_arrayappend(vm, -2);
	sq_pushbool(vm, this->sort_ascending ? SQTrue : SQFalse);
	sq_arrayappend(vm, -2);
	sq_newtable(vm);
	for (const auto &item : this->items) {
		sq_pushinteger(vm, item.first);
		sq_pushinteger(vm, item.second);
		sq_rawset(vm, -3);
	}
	sq_arrayappend(vm, -2);
	return true;
}

bool ScriptList::LoadObject(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, -1) != OT_ARRAY) return false;
	sq_pushnull(vm);
	if (SQ_FAILED(sq_next(vm, -2))) return false;
	if (sq_gettype(vm, -1) != OT_INTEGER) return false;
	SQInteger type;
	sq_getinteger(vm, -1, &type);
	sq_pop(vm, 2);
	if (SQ_FAILED(sq_next(vm, -2))) return false;
	if (sq_gettype(vm, -1) != OT_BOOL) return false;
	SQBool order;
	sq_getbool(vm, -1, &order);
	sq_pop(vm, 2);
	if (SQ_FAILED(sq_next(vm, -2))) return false;
	if (sq_gettype(vm, -1) != OT_TABLE) return false;
	sq_pushnull(vm);
	while (SQ_SUCCEEDED(sq_next(vm, -2))) {
		if (sq_gettype(vm, -2) != OT_INTEGER && sq_gettype(vm, -1) != OT_INTEGER) return false;
		SQInteger key, value;
		sq_getinteger(vm, -2, &key);
		sq_getinteger(vm, -1, &value);
		this->AddItem(key, value);
		sq_pop(vm, 2);
	}
	sq_pop(vm, 3);
	if (SQ_SUCCEEDED(sq_next(vm, -2))) return false;
	sq_pop(vm, 1);
	this->Sort(static_cast<SorterType>(type), order == SQTrue);
	return true;
}

ScriptObject *ScriptList::CloneObject()
{
	ScriptList *clone = new ScriptList();
	clone->CopyList(this);
	return clone;
}

void ScriptList::CopyList(const ScriptList *list)
{
	this->Sort(list->sorter_type, list->sort_ascending);
	this->items = list->items;
}

ScriptList::ScriptList()
{
	/* Default sorter */
	this->sorter_type    = SORT_BY_VALUE;
	this->sort_ascending = false;
	this->initialized    = false;
	this->values_inited  = false;
	this->modifications  = 0;
}

ScriptList::~ScriptList()
{
}

bool ScriptList::HasItem(SQInteger item)
{
	return this->items.contains(item);
}

void ScriptList::Clear()
{
	this->modifications++;

	this->items.clear();
	this->values.clear();
	this->values_inited = false;
	if (this->sorter != nullptr) this->sorter->End();
}

ScriptList::ScriptListMap::iterator ScriptList::AddOrSetItem(ScriptListMap::iterator hint, SQInteger item, SQInteger value)
{
	this->modifications++;

	hint = this->items.try_emplace(hint, item, value);
	if (hint->second != value) {
		/* Key was already present, insertion did not take place */
		this->SetIterValue(hint, value);
	} else if (this->values_inited) {
		this->values.emplace(value, item);
	}

	return hint;
}

void ScriptList::AddOrSetItem(SQInteger item, SQInteger value)
{
	this->modifications++;

	auto [item_iter, inserted] = this->items.try_emplace(item, value);
	if (!inserted) {
		/* Key was already present, insertion did not take place */
		this->SetIterValue(item_iter, value);
		return;
	}

	if (this->values_inited) this->values.emplace(value, item);
}

void ScriptList::AddItem(SQInteger item, SQInteger value)
{
	this->modifications++;

	bool inserted = this->items.try_emplace(item, value).second;

	if (inserted && this->values_inited) {
		this->values.emplace(value, item);
	}
}

ScriptList::ScriptListMap::iterator ScriptList::RemoveIter(ScriptListMap::iterator item_iter)
{
	SQInteger item = item_iter->first;
	SQInteger value = item_iter->second;

	if (this->initialized) this->sorter->Remove(item);

	if (this->values_inited) {
		auto value_iter = this->values.find({value, item});
		this->values.erase(value_iter);
	}

	return this->items.erase(item_iter);
}

void ScriptList::RemoveValueIter(ScriptListValueSet::iterator value_iter)
{
	SQInteger item = value_iter->second;

	if (this->initialized) this->sorter->Remove(item);

	auto item_iter = this->items.find(item);
	this->items.erase(item_iter);

	this->values.erase(value_iter);
}

void ScriptList::RemoveItem(SQInteger item)
{
	this->modifications++;

	auto item_iter = this->items.find(item);
	if (item_iter != this->items.end()) this->RemoveIter(item_iter);
}

void ScriptList::InitValues()
{
	this->values.clear();

	/* Build buffer of (value,item) pairs directly */
	std::vector<ScriptListValueSet::value_type> insertion_buffer;
	insertion_buffer.reserve(this->items.size());

	for (auto &[item, value] : this->items) {
		insertion_buffer.emplace_back(value, item);
	}

	/* Sort by (value,item) */
	std::ranges::sort(insertion_buffer, [](auto &a, auto &b) { return a < b; });

	/* Bulk insert */
	this->values.insert(insertion_buffer.begin(), insertion_buffer.end());

	this->values_inited = true;
}

void ScriptList::InitSorter()
{
	if (this->sorter == nullptr) {
		switch (this->sorter_type) {
			case SORT_BY_ITEM:
				if (this->sort_ascending) {
					this->sorter = std::make_unique<ScriptListSorterItemAscending>(this);
				} else {
					this->sorter = std::make_unique<ScriptListSorterItemDescending>(this);
				}
				break;

			case SORT_BY_VALUE:
				if (this->sort_ascending) {
					this->sorter = std::make_unique<ScriptListSorterValueAscending>(this);
				} else {
					this->sorter = std::make_unique<ScriptListSorterValueDescending>(this);
				}
				break;
			default: NOT_REACHED();
		}
	}
	if (!this->values_inited && this->sorter_type == SORT_BY_VALUE) {
		this->InitValues();
	}
	this->initialized = true;
}

SQInteger ScriptList::Begin()
{
	this->InitSorter();
	return this->sorter->Begin().value_or(0);
}

SQInteger ScriptList::Next()
{
	if (!this->initialized) {
		Debug(script, 0, "Next() is invalid as Begin() is never called");
		return 0;
	}
	return this->sorter->Next().value_or(0);
}

bool ScriptList::IsEmpty()
{
	return this->items.empty();
}

bool ScriptList::IsEnd()
{
	if (!this->initialized) {
		Debug(script, 0, "IsEnd() is invalid as Begin() is never called");
		return true;
	}
	return this->sorter->IsEnd();
}

SQInteger ScriptList::Count()
{
	return this->items.size();
}

SQInteger ScriptList::GetValue(SQInteger item)
{
	auto item_iter = this->items.find(item);
	return item_iter == this->items.end() ? 0 : item_iter->second;
}

void ScriptList::SetIterValue(ScriptListMap::iterator item_iter, SQInteger value)
{
	SQInteger value_old = item_iter->second;
	if (value_old == value) return;

	SQInteger item = item_iter->first;

	if (this->initialized) this->sorter->Remove(item);

	item_iter->second = value;

	if (this->values_inited) {
		auto value_iter = this->values.find({value_old, item});
		this->values.erase(value_iter);
		this->values.emplace(value, item);
	}
}

bool ScriptList::SetValue(SQInteger item, SQInteger value)
{
	this->modifications++;

	auto item_iter = this->items.find(item);
	if (item_iter == this->items.end()) return false;

	this->SetIterValue(item_iter, value);

	return true;
}

void ScriptList::Sort(SorterType sorter, bool ascending)
{
	this->modifications++;

	if (sorter != SORT_BY_VALUE && sorter != SORT_BY_ITEM) return;
	if (sorter == this->sorter_type && ascending == this->sort_ascending) return;

	this->sorter.reset();
	this->sorter_type    = sorter;
	this->sort_ascending = ascending;
	this->initialized    = false;
}

void ScriptList::AddList(ScriptList *list)
{
	if (list == this || list->IsEmpty()) return;

	this->modifications++;

	if (this->IsEmpty()) {
		/* If this is empty, we can just take the items of the other list as is. */
		this->items = list->items;
		this->values = list->values;
		this->values_inited = list->values_inited;
		if (!this->values_inited && this->initialized && this->sorter_type == SORT_BY_VALUE) {
			assert(this->values.empty());
			this->InitValues();
		}
		return;
	}

	auto item_iter2 = list->items.begin();
	auto item_iter1 = this->items.lower_bound(item_iter2->first);

	while (item_iter1 != this->items.end() && item_iter2 != list->items.end()) {
		if (item_iter1->first < item_iter2->first) {
			/* key1 < key2 => advance 'this' */
			++item_iter1;
		} else if (item_iter1->first > item_iter2->first) {
			/* key1 > key2 => add 'list' key/val to 'this', advance both 'this' and 'list' */
			item_iter1 = this->AddOrSetItem(item_iter1, item_iter2->first, item_iter2->second);
			++item_iter1;
			++item_iter2;
		} else {
			/* key1 == key2 => set 'this' with'list' value, advance both 'this' and 'list' */
			this->SetIterValue(item_iter1, item_iter2->second);
			++item_iter1;
			++item_iter2;
		}
	}

	while (item_iter2 != list->items.end()) {
		/* Add remaining items from 'list' */
		item_iter1 = this->AddOrSetItem(item_iter1, item_iter2->first, item_iter2->second);
		++item_iter1;
		++item_iter2;
	}
}

void ScriptList::SwapList(ScriptList *list)
{
	if (list == this) return;

	this->items.swap(list->items);
	this->values.swap(list->values);
	std::swap(this->sorter, list->sorter);
	std::swap(this->sorter_type, list->sorter_type);
	std::swap(this->sort_ascending, list->sort_ascending);
	std::swap(this->initialized, list->initialized);
	std::swap(this->values_inited, list->values_inited);
	std::swap(this->modifications, list->modifications);
	if (this->sorter != nullptr) this->sorter->Retarget(this);
	if (list->sorter != nullptr) list->sorter->Retarget(list);
}

void ScriptList::RemoveAboveValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v > value; });
}

void ScriptList::RemoveBelowValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v < value; });
}

void ScriptList::RemoveBetweenValue(SQInteger start, SQInteger end)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v > start && v < end; });
}

void ScriptList::RemoveValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v == value; });
}

void ScriptList::RemoveTop(SQInteger count)
{
	this->modifications++;

	if (!this->sort_ascending) {
		this->Sort(this->sorter_type, !this->sort_ascending);
		this->RemoveBottom(count);
		this->Sort(this->sorter_type, !this->sort_ascending);
		return;
	}

	switch (this->sorter_type) {
		default: NOT_REACHED();
		case SORT_BY_VALUE:
			if (!this->values_inited) this->InitValues();
			for (auto iter = this->values.begin(); iter != this->values.end(); iter = this->values.begin()) {
				if (--count < 0) return;
				this->RemoveValueIter(iter);
			}
			break;

		case SORT_BY_ITEM:
			for (auto iter = this->items.begin(); iter != this->items.end(); iter = this->items.begin()) {
				if (--count < 0) return;
				this->RemoveIter(iter);
			}
			break;
	}
}

void ScriptList::RemoveBottom(SQInteger count)
{
	this->modifications++;

	if (!this->sort_ascending) {
		this->Sort(this->sorter_type, !this->sort_ascending);
		this->RemoveTop(count);
		this->Sort(this->sorter_type, !this->sort_ascending);
		return;
	}

	switch (this->sorter_type) {
		default: NOT_REACHED();
		case SORT_BY_VALUE:
			if (!this->values_inited) this->InitValues();
			for (auto iter = this->values.end(); iter != this->values.begin(); iter = this->values.end()) {
				if (--count < 0) return;
				--iter;
				this->RemoveValueIter(iter);
			}
			break;

		case SORT_BY_ITEM:
			for (auto iter = this->items.end(); iter != this->items.begin(); iter = this->items.end()) {
				if (--count < 0) return;
				--iter;
				this->RemoveIter(iter);
			}
			break;
	}
}

void ScriptList::RemoveList(ScriptList *list)
{
	if (list == this) {
		this->Clear();
		return;
	}

	auto item_iter2 = list->items.begin();
	auto item_iter1 = this->items.lower_bound(item_iter2->first);

	while (item_iter1 != this->items.end() && item_iter2 != list->items.end()) {
		if (item_iter1->first < item_iter2->first) {
			/* key1 < key2 => advance 'this' */
			++item_iter1;
		} else if (item_iter1->first > item_iter2->first) {
			/* key1 > key2 => advance 'list' */
			++item_iter2;
		} else {
			/* key1 == key2 => erase from 'this', advance 'list' */
			item_iter1 = this->RemoveIter(item_iter1);
			++item_iter2;
		}
	}
}

void ScriptList::KeepAboveValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v <= value; });
}

void ScriptList::KeepBelowValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v >= value; });
}

void ScriptList::KeepBetweenValue(SQInteger start, SQInteger end)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v <= start && v >= end; });
}

void ScriptList::KeepValue(SQInteger value)
{
	this->RemoveItems([&](const SQInteger &, const SQInteger &v) { return v != value; });
}

void ScriptList::KeepTop(SQInteger count)
{
	this->modifications++;

	this->RemoveBottom(this->Count() - count);
}

void ScriptList::KeepBottom(SQInteger count)
{
	this->modifications++;

	this->RemoveTop(this->Count() - count);
}

void ScriptList::KeepList(ScriptList *list)
{
	if (list == this || this->IsEmpty()) return;

	this->modifications++;

	if (list->IsEmpty()) {
		/* If 'list' is empty, we can just clear 'this'. */
		this->Clear();
		return;
	}

	auto item_iter1 = this->items.begin();
	auto item_iter2 = list->items.begin();

	while (item_iter1 != this->items.end() && item_iter2 != list->items.end()) {
		if (item_iter1->first < item_iter2->first) {
			/* key1 < key2 => key1 not in 'list', erase from 'this' */
			item_iter1 = this->RemoveIter(item_iter1);
		} else if (item_iter1->first > item_iter2->first) {
			/* key1 > key2 => advance 'list' */
			++item_iter2;
		} else {
			/* key1 == key2 => keep, advance both 'this' and 'list' */
			++item_iter1;
			++item_iter2;
		}
	}

	/* 'list' exhausted: remaining keys in 'this' are not present in 'list' */
	while (item_iter1 != this->items.end()) {
		item_iter1 = this->RemoveIter(item_iter1);
	}
}

SQInteger ScriptList::_get(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) != OT_INTEGER) return SQ_ERROR;

	SQInteger idx;
	sq_getinteger(vm, 2, &idx);

	auto item_iter = this->items.find(idx);
	if (item_iter == this->items.end()) return SQ_ERROR;

	sq_pushinteger(vm, item_iter->second);
	return 1;
}

SQInteger ScriptList::_set(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) != OT_INTEGER) return SQ_ERROR;

	SQInteger idx;
	sq_getinteger(vm, 2, &idx);

	/* Retrieve the return value */
	SQInteger val;
	switch (sq_gettype(vm, 3)) {
		case OT_NULL:
			this->RemoveItem(idx);
			return 0;

		case OT_BOOL: {
			SQBool v;
			sq_getbool(vm, 3, &v);
			val = v ? 1 : 0;
			break;
		}

		case OT_INTEGER:
			sq_getinteger(vm, 3, &val);
			break;

		default:
			return sq_throwerror(vm, "you can only assign integers to this list");
	}

	this->AddOrSetItem(idx, val);
	return 0;
}

SQInteger ScriptList::_nexti(HSQUIRRELVM vm)
{
	if (sq_gettype(vm, 2) == OT_NULL) {
		if (this->IsEmpty()) {
			sq_pushnull(vm);
			return 1;
		}
		sq_pushinteger(vm, this->Begin());
		return 1;
	}

	SQInteger idx;
	sq_getinteger(vm, 2, &idx);

	SQInteger val = this->Next();
	if (this->IsEnd()) {
		sq_pushnull(vm);
		return 1;
	}

	sq_pushinteger(vm, val);
	return 1;
}

SQInteger ScriptList::Valuate(HSQUIRRELVM vm)
{
	this->modifications++;

	/* The first parameter is the instance of ScriptList. */
	int nparam = sq_gettop(vm) - 1;

	if (nparam < 1) {
		return sq_throwerror(vm, "You need to give at least a Valuator as parameter to ScriptList::Valuate");
	}

	/* Make sure the valuator function is really a function, and not any
	 * other type. It's parameter 2 for us, but for the user it's the
	 * first parameter they give. */
	SQObjectType valuator_type = sq_gettype(vm, 2);
	if (valuator_type != OT_CLOSURE && valuator_type != OT_NATIVECLOSURE) {
		return sq_throwerror(vm, "parameter 1 has an invalid type (expected function)");
	}

	/* Don't allow docommand from a Valuator, as we can't resume in
	 * mid C++-code. */
	ScriptObject::DisableDoCommandScope disabler{};

	/* Limit the total number of ops that can be consumed by a valuate operation */
	SQOpsLimiter limiter(vm, MAX_VALUATE_OPS, "valuator function");

	/* Push the function to call */
	sq_push(vm, 2);

	auto begin = this->items.begin();
	if (disabler.GetOriginalValue() && this->resume_iter) {
		begin = *this->resume_iter;
	}

	for (auto iter = begin; iter != this->items.end(); ++iter) {
		if (disabler.GetOriginalValue() && iter != this->resume_iter && ScriptController::GetOpsTillSuspend() < 0) {
			this->resume_iter = iter;
			/* Pop the valuator function. */
			sq_poptop(vm);
			sq_pushbool(vm, SQTrue);
			return 1;
		}

		/* Check for changing of items. */
		int previous_modification_count = this->modifications;

		/* Push the root table as instance object, this is what squirrel does for meta-functions. */
		sq_pushroottable(vm);
		/* Push all arguments for the valuator function. */
		sq_pushinteger(vm, iter->first);
		for (int i = 0; i < nparam - 1; i++) {
			sq_push(vm, i + 3);
		}

		/* Call the function. Squirrel pops all parameters and pushes the return value. */
		if (SQ_FAILED(sq_call(vm, nparam + 1, SQTrue, SQFalse))) {
			return SQ_ERROR;
		}

		/* Retrieve the return value */
		SQInteger value;
		switch (sq_gettype(vm, -1)) {
			case OT_INTEGER: {
				sq_getinteger(vm, -1, &value);
				break;
			}

			case OT_BOOL: {
				SQBool v;
				sq_getbool(vm, -1, &v);
				value = v ? 1 : 0;
				break;
			}

			default: {
				/* Pop the valuator function and the return value. */
				sq_pop(vm, 2);

				return sq_throwerror(vm, "return value of valuator is not valid (not integer/bool)");
			}
		}

		/* Was something changed? */
		if (previous_modification_count != this->modifications) {
			/* Pop the valuator function and the return value. */
			sq_pop(vm, 2);

			return sq_throwerror(vm, "modifying valuated list outside of valuator function");
		}

		this->SetIterValue(iter, value);

		/* Pop the return value. */
		sq_poptop(vm);

		Squirrel::DecreaseOps(vm, 5);
	}

	/* Pop the valuator function from the squirrel stack. */
	sq_poptop(vm);

	this->resume_iter.reset();
	sq_pushbool(vm, SQFalse);
	return 1;
}
