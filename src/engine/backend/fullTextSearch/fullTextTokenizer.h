#ifndef __FULL_TEXT_TOKENIZER_H__
#define __FULL_TEXT_TOKENIZER_H__

#include "backend/backend_core.h"

#include <vector>

// ⭐ WHAT A WORD IS, for the base's full-text search - one answer for the text that is indexed and for the text a
// person searches by, so the two always meet.
//
// A word is a run of letters and digits. A dot, a slash, a comma or a hyphen BETWEEN TWO DIGITS keeps them together,
// because articles and sizes are written that way (`1.6`, `12/5`, `205/55`); anywhere else it separates. Every word
// is lower-cased and `ё` is folded to `е`, so the case and the two spellings find each other. There is no stemming:
// an ending is found by the search, which matches each word as the beginning of an indexed one and fuzzily.
//
// The classes are the tokenizer's own, not the C library's: iswalpha and towlower answer by the process's locale,
// and a server running in the "C" locale would take every Cyrillic letter for a separator.
class BACKEND_API ibFullTextTokenizer {
public:

	// The longest word the index keeps; a longer run is cut there (sys_search_word.word is that wide).
	static constexpr std::size_t kMaxWordLength = 64;

	// The words of a text, normalised, in their order - a word that occurs twice is there twice.
	static std::vector<ibString> Split(const ibString& text);

	// The edit distance between two words (insertions, deletions, substitutions), or `limit + 1` as soon as it is known
	// to be more than `limit` - the search only asks whether a word is within its threshold.
	static std::size_t Distance(const ibString& lhs, const ibString& rhs, std::size_t limit);

	// A letter or a digit of any script the index keeps.
	static bool IsWordChar(ibChar c);

	// The character lower-cased, `ё` folded to `е`.
	static ibChar Fold(ibChar c);
};

#endif // !__FULL_TEXT_TOKENIZER_H__
