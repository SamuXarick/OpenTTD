/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file stringfilter_type.h Searching and filtering using a stringterm. */

#ifndef STRINGFILTER_TYPE_H
#define STRINGFILTER_TYPE_H

#include "strings_type.h"

/**
 * String filter and state.
 *
 * The filter takes a stringterm and parses it into words separated by whitespace.
 * The whitespace-separation can be avoided by quoting words in the searchterm using " or '.
 * The quotation characters can be nested or concatenated in a unix-shell style.
 *
 * When filtering an item, all words are checked for matches, and the filter matches if every word
 * matched. So, effectively this is a AND search for all entered words.
 *
 * Once the filter is set up using SetFilterTerm, multiple items can be filtered consecutively.
 *  1. For every item first call ResetState() which resets the matching-state.
 *  2. Pass all lines of the item via AddLine() to the filter.
 *  3. Check the matching-result for the item via GetState().
 */
struct StringFilter {
private:
	/** State of a single filter word */
	struct WordState {
		std::string word;                          ///< Word to filter for.
		bool match;                                ///< Already matched?
	};

	std::vector<WordState> word_index;             ///< Word index and filter state.
	uint word_matches = 0;                         ///< Summary of filter state: Number of words matched.

	const bool *case_sensitive;                    ///< Match case-sensitively (usually a static variable).
	bool locale_aware;                             ///< Match words using the current locale.

public:
	/**
	 * Constructor for filter.
	 * @param case_sensitive Pointer to a (usually static) variable controlling the case-sensitivity. nullptr means always case-insensitive.
	 */
	StringFilter(const bool *case_sensitive = nullptr, bool locale_aware = true) : case_sensitive(case_sensitive), locale_aware(locale_aware) {}

	void SetFilterTerm(std::string_view str);

	/**
	 * Check whether any filter words were entered.
	 * @return true if no words were entered.
	 */
	bool IsEmpty() const { return this->word_index.empty(); }

	void ResetState();
	void AddLine(const char *) = delete; // prevent implicit construction of string_view from potential nullptr
	void AddLine(std::string_view str);

	/**
	 * Get the matching state of the current item.
	 * @return true if matched.
	 */
	bool GetState() const { return this->word_matches == this->word_index.size(); }
};

#endif /* STRINGFILTER_TYPE_H */
