////////////////////////////////////////////////////////////////////////////
//	Description : what a word is, for the base's full-text search
////////////////////////////////////////////////////////////////////////////

#include "fullTextTokenizer.h"

#include <algorithm>

namespace {

bool IsDigit(ibChar c)
{
	return c >= u'0' && c <= u'9';
}

// Joins two digits: `1.6`, `12/5`, `1,5`, `10-20`.
bool IsDigitJoiner(ibChar c)
{
	return c == u'.' || c == u'/' || c == u',' || c == u'-';
}

} // namespace

bool ibFullTextTokenizer::IsWordChar(ibChar c)
{
	if (c < 0x80)
		return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
	// Latin-1 and Latin Extended letters (not × and ÷), IPA and the modifier letters.
	if (c >= 0x00C0 && c <= 0x02AF)
		return c != 0x00D7 && c != 0x00F7;
	// Greek, Cyrillic, Armenian, Hebrew, Arabic and the other alphabetic scripts up to the general punctuation.
	if (c >= 0x0370 && c <= 0x1FFF)
		return c != 0x0387 && c != 0x055C && c != 0x055D && c != 0x0589 && c != 0x05BE && c != 0x060C;
	// Kana, CJK and Hangul; the compatibility ideographs and presentation forms.
	if ((c >= 0x3040 && c <= 0xD7FF) || (c >= 0xF900 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF))
		return true;
	// Full-width digits and Latin letters.
	return (c >= 0xFF10 && c <= 0xFF19) || (c >= 0xFF21 && c <= 0xFF3A) || (c >= 0xFF41 && c <= 0xFF5A);
}

ibChar ibFullTextTokenizer::Fold(ibChar c)
{
	if (c < 0x80)
		return (c >= u'A' && c <= u'Z') ? static_cast<ibChar>(c + 0x20) : c;
	if (c >= 0x00C0 && c <= 0x00DE && c != 0x00D7)                // À..Þ
		return static_cast<ibChar>(c + 0x20);
	if (c >= 0x0100 && c <= 0x017F) {                               // Latin Extended-A: upper even, lower odd
		if (c == 0x0130 || c == 0x0131) return u'i';
		if (c == 0x0178) return 0x00FF;                             // Ÿ -> ÿ, the one upper letter out of its pair
		if ((c >= 0x0139 && c <= 0x0148) || (c >= 0x0179 && c <= 0x017E))
			return (c % 2 == 1) ? static_cast<ibChar>(c + 1) : c;
		return (c % 2 == 0 && c != 0x0138) ? static_cast<ibChar>(c + 1) : c;
	}
	if (c >= 0x0391 && c <= 0x03AB && c != 0x03A2)                  // Α..Ϋ
		return static_cast<ibChar>(c + 0x20);
	if (c == 0x0401 || c == 0x0451)                                 // Ё ё -> е
		return 0x0435;
	if (c >= 0x0400 && c <= 0x040F)                                 // Ѐ..Џ
		return static_cast<ibChar>(c + 0x50);
	if (c >= 0x0410 && c <= 0x042F)                                 // А..Я
		return static_cast<ibChar>(c + 0x20);
	if ((c >= 0x0460 && c <= 0x0481) || (c >= 0x048A && c <= 0x04BF) || (c >= 0x04D0 && c <= 0x052F))
		return (c % 2 == 0) ? static_cast<ibChar>(c + 1) : c;       // Cyrillic pairs, Ґ among them
	if (c >= 0x04C1 && c <= 0x04CE)
		return (c % 2 == 1) ? static_cast<ibChar>(c + 1) : c;
	if (c == 0x04C0)
		return 0x04CF;
	if (c >= 0xFF21 && c <= 0xFF3A)                                 // full-width A..Z
		return static_cast<ibChar>(c + 0x20);
	return c;
}

std::vector<ibString> ibFullTextTokenizer::Split(const ibString& text)
{
	std::vector<ibString> words;
	const std::size_t length = text.Len();
	ibString word;
	auto flush = [&] {
		if (!word.IsEmpty())
			words.push_back(word.Len() > kMaxWordLength ? word.Left(kMaxWordLength) : word);
		word = ibString();
	};
	for (std::size_t i = 0; i < length; ++i) {
		const ibChar c = text[i];
		if (IsWordChar(c)) {
			word.Append(Fold(c));
			continue;
		}
		// A joiner between two digits stays inside the word.
		if (IsDigitJoiner(c) && !word.IsEmpty() && IsDigit(word[word.Len() - 1])
			&& i + 1 < length && IsDigit(text[i + 1])) {
			word.Append(c);
			continue;
		}
		flush();
	}
	flush();
	return words;
}

std::size_t ibFullTextTokenizer::Distance(const ibString& lhs, const ibString& rhs, std::size_t limit)
{
	const std::size_t n = lhs.Len(), m = rhs.Len();
	if ((n > m ? n - m : m - n) > limit)
		return limit + 1;
	// Two rows of the classic table; a row whose smallest cell is past the limit ends it.
	std::vector<std::size_t> previous(m + 1), current(m + 1);
	for (std::size_t j = 0; j <= m; ++j)
		previous[j] = j;
	for (std::size_t i = 1; i <= n; ++i) {
		current[0] = i;
		std::size_t best = current[0];
		for (std::size_t j = 1; j <= m; ++j) {
			const std::size_t substitution = previous[j - 1] + (lhs[i - 1] == rhs[j - 1] ? 0 : 1);
			current[j] = std::min({ previous[j] + 1, current[j - 1] + 1, substitution });
			best = std::min(best, current[j]);
		}
		if (best > limit)
			return limit + 1;
		std::swap(previous, current);
	}
	return std::min(previous[m], limit + 1);
}
