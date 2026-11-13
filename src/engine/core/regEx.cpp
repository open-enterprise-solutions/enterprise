#include "core/regEx.h"

#include <string_view>

bool ibRegEx::Compile(const ibString& pattern, int flags)
{
	m_valid = false;
	try {
		std::regex_constants::syntax_option_type options = std::regex_constants::ECMAScript;
		if ((flags & ibRE_ICASE) != 0)
			options |= std::regex_constants::icase;
		const auto wide = pattern.wc_str();
		const std::wstring_view source(wide);
		m_regex.assign(source.begin(), source.end(), options);
		m_valid = true;
	}
	catch (const std::regex_error&) {
		// Not a pattern - an answer, as wx's Compile was.
	}
	return m_valid;
}

bool ibRegEx::Matches(const ibString& text) const
{
	if (!m_valid)
		return false;

	const auto wide = text.wc_str();
	const std::wstring_view view(wide);
	for (std::size_t from = 0; from <= view.size();) {
		std::size_t to = view.find(L'\n', from);
		if (to == std::wstring_view::npos)
			to = view.size();
		std::size_t end = to;
		if (end > from && view[end - 1] == L'\r')
			--end;
		if (std::regex_search(view.begin() + from, view.begin() + end, m_regex))
			return true;
		from = to + 1;
	}
	return false;
}
