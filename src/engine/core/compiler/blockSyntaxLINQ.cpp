////////////////////////////////////////////////////////////////////////////
//	Description : A LINQ block as a thing that can be built (blockSyntaxLINQ.h)
////////////////////////////////////////////////////////////////////////////

#include "blockSyntaxLINQ.h"

#include "core/compiler/translateCode.h"   // GetKeyWord - the words are the lexer's
#include "core/compiler/codeDef.h"         // KEY_FROM ... KEY_RESTRICT, CODE_CES

#include "core/stringUtils.h"
#include "core/stringTokenizer.h"   // ibSplit — the names a clause lists

#include "core/localization/dictionary.h"   // _() — the refusals' words

#include <algorithm>
#include <functional>

ibString ibLinqTotalName(ibLinqTotal total)
{
	switch (total) {
	case ibLinqTotal::Count:   return ibT("Count");
	case ibLinqTotal::Min:     return ibT("Min");
	case ibLinqTotal::Max:     return ibT("Max");
	case ibLinqTotal::Average: return ibT("Average");
	default:                   return ibT("Sum");
	}
}

namespace {

ibString Word(int key)
{
	return ibTranslateCode::GetKeyWord(key);
}

// Whether `expression` names `name` as a word of its own - `o` in `Round(o.Price, 2)`, not in `total`,
// not after a dot (`x.o`), not inside a string. How a grouped block finds a column still reading a ROW.
bool Mentions(const ibString& expression, const ibString& name)
{
	if (name.IsEmpty())
		return false;
	const auto wordChar = [](ibChar c) { return ibIsAlnum(c) || c == ibT('_'); };
	bool quoted = false;
	for (size_t i = 0; i < expression.length(); ++i) {
		const ibChar c = expression[i];
		if (c == ibT('"')) {
			quoted = !quoted;
			continue;
		}
		if (quoted || (i > 0 && (wordChar(expression[i - 1]) || expression[i - 1] == ibT('.'))))
			continue;
		if (expression.length() - i < name.length())
			break;
		const size_t after = i + name.length();
		if ((after == expression.length() || !wordChar(expression[after]))
			&& stringUtils::IsSameAs(expression.Mid(i, name.length()), name))
			return true;
	}
	return false;
}

// Several values made into ONE, which a group compares by its contents (ibValueContainer::CompareValueLS)
// - `New Structure("Customer, Warehouse", o.Customer, o.Warehouse)`; its fields are read back by name.
ibString MadeOne(const std::vector<std::pair<ibString, ibString>>& parts)
{
	ibString names, values;
	for (const std::pair<ibString, ibString>& part : parts) {
		names += (names.IsEmpty() ? ibString() : ibString(ibT(", "))) + part.first;
		values += ibT(", ") + part.second;
	}
	return Word(KEY_NEW) + ibT(" Structure(\"") + names + ibT("\"") + values + ibT(")");
}

bool IsName(const ibString& text)
{
	if (text.IsEmpty() || ibIsDigit(text[0]))
		return false;
	for (const ibChar c : text)
		if (!ibIsAlnum(c) && c != ibT('_'))
			return false;
	return true;
}

// What a grouped block writes, worked out once and read by Render and Parse alike: the key, what each
// group keeps of its rows, and the words that reach a key and a total from the group.
struct ibGroupWords {
	ibString                                     m_key;       // `By …`
	ibString                                     m_kept;      // `Group … By` - what `Values` holds
	std::vector<std::pair<ibString, ibString>>   m_inputs;    // the fields a total reads: name, expression
	std::vector<ibString>                        m_keyAccess;
	std::vector<ibString>                        m_totalText;
};

ibGroupWords GroupWordsOf(const ibLinqBlock& block)
{
	ibGroupWords words;
	const ibString group = block.m_groupInto;

	// ONE KEY IS WRITTEN AS IT IS, several are made into one.
	if (block.m_groupKeys.size() == 1)
		words.m_key = block.m_groupKeys.front().m_expression;
	else {
		std::vector<std::pair<ibString, ibString>> keys;
		for (const ibLinqBlockField& key : block.m_groupKeys)
			keys.emplace_back(key.m_name, key.m_expression);
		words.m_key = MadeOne(keys);
	}
	for (const ibLinqBlockField& key : block.m_groupKeys)
		words.m_keyAccess.push_back(group + ibT(".Key") + (block.m_groupKeys.size() == 1 ? ibString() : ibT(".") + key.m_name));

	// ⭐ WHAT A GROUP KEEPS OF ITS ROWS IS WHAT ITS TOTALS READ - nothing else is read after grouping. One
	// field is kept as it is, and a total over it is the aggregate itself (`grp.Values.Sum()`); several
	// are made into one, and a total picks its own out of each row by name. With nothing to read, the
	// group keeps the rows themselves.
	for (const ibLinqBlockTotal& total : block.m_totals) {
		if (total.m_function == ibLinqTotal::Count)
			continue;
		const bool known = std::any_of(words.m_inputs.begin(), words.m_inputs.end(),
			[&](const std::pair<ibString, ibString>& input) { return stringUtils::IsSameAs(input.second, total.m_expression); });
		if (!known)
			words.m_inputs.emplace_back(total.m_name, total.m_expression);
	}
	if (words.m_inputs.empty())
		words.m_kept = block.m_sources.empty() ? ibString() : block.m_sources.front().m_alias;
	else if (words.m_inputs.size() == 1)
		words.m_kept = words.m_inputs.front().second;
	else
		words.m_kept = MadeOne(words.m_inputs);

	// A total over one of several kept fields takes it with a function - spelled in the module's syntax,
	// the one place a block has to know which it is.
	const ibString row = group + ibT("Row");
	const bool braces = block.GetCodeStyle() == CODE_CES;
	for (const ibLinqBlockTotal& total : block.m_totals) {
		ibString argument;
		if (total.m_function != ibLinqTotal::Count && words.m_inputs.size() > 1) {
			ibString field;
			for (const std::pair<ibString, ibString>& input : words.m_inputs)
				if (stringUtils::IsSameAs(input.second, total.m_expression))
					field = input.first;
			argument = Word(KEY_FUNCTION) + ibT("(") + row + ibT(") ")
				+ (braces
					? ibT("{ ") + Word(KEY_RETURN) + ibT(" ") + row + ibT(".") + field + ibT("; }")
					: Word(KEY_RETURN) + ibT(" ") + row + ibT(".") + field + ibT(" ") + Word(KEY_ENDFUNCTION));
		}
		words.m_totalText.push_back(group + ibT(".Values.") + ibLinqTotalName(total.m_function) + ibT("(") + argument + ibT(")"));
	}
	return words;
}

// Two spellings of one expression - `grp.Key.Customer` and `grp . Key.Customer` - are one.
bool SameWords(const ibString& a, const ibString& b)
{
	ibString x(a), y(b);
	x.Replace(ibT(" "), ibEmptyString);
	y.Replace(ibT(" "), ibEmptyString);
	return stringUtils::IsSameAs(x, y);
}

} // namespace

std::vector<int> ibLinqClausesAfter(int lastKeyword)
{
	switch (lastKeyword) {
	// A source is being named - the language wants `In` and nothing else.
	case KEY_FROM:
	case KEY_JOIN:   return { KEY_IN };
	// After `On <key>` it wants `Equals`; after `Group <row>` it wants `By`.
	case KEY_ON:     return { KEY_EQUALS };
	case KEY_GROUP:  return { KEY_BY };
	// ⭐ A GROUP'S KEY IS FOLLOWED BY ITS NAME OR BY NOTHING. Without a name the group is the answer -
	// its groups, `Key` and `Values` - and the compiler reads no clause after it (compileCodeLINQ.cpp,
	// "mutually exclusive with SELECT"): a `Select` offered here was written and quietly not read.
	case KEY_BY:     return { KEY_INTO };
	// After `Into <name>` the query goes on over the GROUPS: filtered, ordered, cut and answered.
	case KEY_INTO:   return { KEY_WHERE, KEY_SELECT, KEY_ORDERBY, KEY_SKIP, KEY_TAKE };
	// `Select` closes the query; only `Distinct` remains.
	case KEY_SELECT: return { KEY_DISTINCT };
	default:
		// `From` again: a second source is how this language spells a cross product.
		return { KEY_WHERE, KEY_SELECT, KEY_ORDERBY, KEY_GROUP, KEY_JOIN, KEY_TAKE, KEY_SKIP, KEY_FROM };
	}
}

ibString ibLinqBlock::Render(const ibString& indent, ibString& refusal) const
{
	return Write(indent, refusal, /*draft*/ false);
}

ibString ibLinqBlock::RenderDraft(const ibString& indent, ibString& refusal) const
{
	return Write(indent, refusal, /*draft*/ true);
}

ibString ibLinqBlock::Write(const ibString& indent, ibString& refusal, bool draft) const
{
	refusal.clear();

	if (m_sources.empty()) {
		refusal = _("A query needs a source: add a table or a variable to read.");
		return ibString();
	}

	// ⭐ A REFUSAL IS SAID, AND THE WRITING GOES ON: the first thing wrong is what the refusal names, and
	// the text is still written the way the parts stand - a preview that went blank at the first wrong
	// cell showed nothing of the rest (Max: "it clears everything"). Only Render withholds it.
	const auto refuse = [&refusal](const ibString& why) {
		if (refusal.IsEmpty())
			refusal = why;
	};
	// ⭐ A DOT WITH NOTHING AFTER IT IS A FIELD NOT WRITTEN YET - the join keys start as `g.` for exactly
	// that. Written into a module it is worse than missing: the lexer reads the next word as the field,
	// `g. Equals c.` as `g.Equals`, the compiler stops there, and the query it brackets ends mid-way.
	const auto finished = [&refuse](const ibString& expression) {
		if (ibString(expression).Trim(true).EndsWith(ibT(".")))
			refuse(ibString::Format(_("'%s' is not finished: write the field after the dot."), ibString(expression).Trim(true).Trim(false)));
	};

	const auto word = [](int key) { return Word(key); };

	// ⭐ A NUMBER OF ROWS IS A NUMBER - written as one, or a variable that holds one (the compiler checks a
	// variable whose type it knows, compileCodeLINQ.cpp). A string or a fraction is not a count.
	for (const std::pair<int, const ibString*>& cut : { std::make_pair((int)KEY_SKIP, &m_skip), std::make_pair((int)KEY_TAKE, &m_take) }) {
		const ibString& text = *cut.second;
		if (text.IsEmpty())
			continue;
		bool count = text[0] != ibT('"') && text[0] != ibT('\'');
		if (count && ibIsDigit(text[0]))
			count = std::all_of(text.begin(), text.end(), [](ibChar c) { return ibIsDigit(c); });
		if (!count)
			refuse(ibString::Format(_("'%s' takes a number of rows: a whole number, or a variable that holds one."),
				word(cut.first)));
	}

	std::vector<ibString> lines;
	// Every query word written, in order, and whether it OPENS a clause - the openers are what the
	// order is checked by; the modifiers only move "the last word" along, as they do in the text.
	std::vector<std::pair<int, bool>> spoken;
	const auto say = [&spoken](int key, bool opens) { spoken.emplace_back(key, opens); };

	const ibString firstAlias = m_sources.front().m_alias;

	for (size_t i = 0; i < m_sources.size(); ++i) {
		const ibLinqBlockSource& source = m_sources[i];
		if (source.m_alias.IsEmpty() || source.m_expression.IsEmpty())
			refuse(_("A source is missing its name in the query or what it reads."));
		else if (!IsName(source.m_alias))
			refuse(ibString::Format(_("'%s' cannot name a source: a name is letters, digits and '_', not starting with a digit."),
				source.m_alias));
		finished(source.m_expression);
		// The first source is always a `From` - a join needs something to join to.
		if (source.m_join && i > 0) {
			if (source.m_leftKey.IsEmpty() || source.m_rightKey.IsEmpty())
				refuse(ibString::Format(_("The join of '%s' needs both keys: what it is joined on, and what that equals."),
					source.m_alias));
			finished(source.m_leftKey);
			finished(source.m_rightKey);
			lines.push_back(word(KEY_JOIN) + ibT(" ") + source.m_alias + ibT(" ") + word(KEY_IN) + ibT(" ")
				+ source.m_expression + ibT(" ") + word(KEY_ON) + ibT(" ") + source.m_leftKey + ibT(" ")
				+ word(KEY_EQUALS) + ibT(" ") + source.m_rightKey);
			say(KEY_JOIN, true); say(KEY_IN, false); say(KEY_ON, false); say(KEY_EQUALS, false);
		}
		else {
			lines.push_back(word(KEY_FROM) + ibT(" ") + source.m_alias + ibT(" ") + word(KEY_IN) + ibT(" ")
				+ source.m_expression);
			say(KEY_FROM, true); say(KEY_IN, false);
		}
	}

	const auto writeConditions = [&](const std::vector<ibString>& conditions) {
		for (const ibString& condition : conditions) {
			const ibString text = ibString(condition).Trim(true).Trim(false);
			if (text.IsEmpty())
				continue;
			finished(text);
			lines.push_back(word(KEY_WHERE) + ibT(" ") + text);
			say(KEY_WHERE, true);
		}
	};

	// The order, then the cut. ONE clause, each key followed by its own way when it runs descending
	// (`OrderBy g.Group, g.Price Descending, g.Name`). `spell` is how an order key is written where it stands.
	const auto writeOrderAndCut = [&](const std::function<ibString(const ibString&)>& spell) {
		ibString keys;
		size_t descendingKeys = 0;
		for (const ibLinqBlockOrder& order : m_order) {
			const ibString key = ibString(order.m_expression).Trim(true).Trim(false);
			if (key.IsEmpty())
				continue;
			finished(key);
			keys += (keys.IsEmpty() ? ibString() : ibString(ibT(", "))) + spell(key)
				+ (order.m_descending ? ibString(ibT(" ")) + word(KEY_DESCENDING) : ibString());
			if (order.m_descending)
				++descendingKeys;
		}
		if (!keys.IsEmpty()) {
			lines.push_back(word(KEY_ORDERBY) + ibT(" ") + keys);
			say(KEY_ORDERBY, true);
			for (size_t d = 0; d < descendingKeys; ++d)
				say(KEY_DESCENDING, false);
		}
		if (!m_skip.IsEmpty()) {
			lines.push_back(word(KEY_SKIP) + ibT(" ") + m_skip);
			say(KEY_SKIP, true);
		}
		if (!m_take.IsEmpty()) {
			lines.push_back(word(KEY_TAKE) + ibT(" ") + m_take);
			say(KEY_TAKE, true);
		}
	};
	const auto asWritten = [](const ibString& key) { return key; };

	// The columns inside the braces, `name = expression` each.
	const auto braced = [](const std::vector<std::pair<ibString, ibString>>& columns) {
		ibString text;
		for (const std::pair<ibString, ibString>& column : columns)
			text += (text.IsEmpty() ? ibString() : ibString(ibT(", "))) + column.first + ibT(" = ") + column.second;
		return ibT("{ ") + text + ibT(" }");
	};

	// The source a text still reads row by row - an alias of the block, named in it.
	const auto readsRow = [this](const ibString& text) {
		for (const ibLinqBlockSource& source : m_sources)
			if (Mentions(text, source.m_alias))
				return source.m_alias;
		return ibString();
	};

	writeConditions(m_conditions);

	// A total, or a condition on groups, with nothing grouped would be written nowhere - said instead.
	if (!IsGrouped() && (!m_totals.empty() || !m_groupConditions.empty()))
		refuse(_("Totals and conditions on groups are read over groups: add a key to group by."));

	// ⭐ A NAME IS A NAME, grouped or not: letters, digits and '_' - `Column1 ak` is two words, and the
	// compiler reads the first as the column and stops at the second. Every column once.
	std::vector<ibString> names;
	const auto claim = [&](const ibString& name) {
		if (!IsName(name)) {
			refuse(ibString::Format(_("'%s' cannot name a column: a name is letters, digits and '_', not starting with a digit."), name));
			return;
		}
		for (const ibString& taken : names)
			if (stringUtils::IsSameAs(taken, name)) {
				refuse(ibString::Format(_("Two columns of the answer are named '%s'."), name));
				return;
			}
		names.push_back(name);
	};

	if (!IsGrouped()) {
		writeConditions(m_groupConditions);   // written where they would stand, for the draft to show
		writeOrderAndCut(asWritten);

		// THE ANSWER'S COLUMNS, or the row itself when none are named. One column with no name of its
		// own is written bare, the way the language writes it.
		ibString select = word(KEY_SELECT) + ibT(" ");
		std::vector<std::pair<ibString, ibString>> columns;
		bool unnamed = false;
		for (const ibLinqBlockField& field : m_fields)
			if (!field.m_expression.IsEmpty()) {
				finished(field.m_expression);
				if (!field.m_name.IsEmpty())
					claim(field.m_name);
				columns.emplace_back(field.m_name.IsEmpty() ? field.m_expression : field.m_name, field.m_expression);
				unnamed = field.m_name.IsEmpty();
			}
		if (columns.size() == 1 && unnamed)
			select += columns.front().second;
		else if (!columns.empty())
			select += braced(columns);
		else
			select += firstAlias;
		lines.push_back(select);
		say(KEY_SELECT, true);
	}
	else {
		// EVERY KEY AND EVERY TOTAL NAMED, and every column once: a name is a column of the answer and,
		// for several keys or several fields read, a field of the value they are made into.
		for (const ibLinqBlockField& key : m_groupKeys) {
			if (key.m_expression.IsEmpty())
				refuse(ibString::Format(_("The key '%s' of the grouping does not say what it groups by."), key.m_name));
			finished(key.m_expression);
			claim(key.m_name);
		}
		for (const ibLinqBlockTotal& total : m_totals) {
			if (total.m_function != ibLinqTotal::Count && total.m_expression.IsEmpty())
				refuse(ibString::Format(_("The total '%s' does not say which field it reads."), total.m_name));
			finished(total.m_expression);
			claim(total.m_name);
		}

		if (m_groupInto.IsEmpty()) {
			// ⭐ A GROUP WITHOUT A NAME IS THE ANSWER - the groups themselves, `Key` and `Values` - and
			// nothing is read after it. So the rows are ordered and cut BEFORE they are grouped, which is
			// the only place the compiler reads an order then; columns, totals and conditions on groups
			// need the group's name to be written at all.
			if (!m_totals.empty() || !m_fields.empty() || !m_groupConditions.empty() || m_distinct)
				refuse(_("A group without a name answers with the groups themselves - each one's key and its rows. "
					"Name the group to choose its columns, totals or conditions on groups."));
			writeOrderAndCut(asWritten);
			const ibGroupWords group = GroupWordsOf(*this);
			lines.push_back(word(KEY_GROUP) + ibT(" ") + group.m_kept + ibT(" ") + word(KEY_BY) + ibT(" ") + group.m_key);
			say(KEY_GROUP, true); say(KEY_BY, false);
		}
		else {
			if (!IsName(m_groupInto))
				refuse(ibString::Format(_("'%s' cannot name the group: a name is letters, digits and '_', not starting with a digit."),
					m_groupInto));
			const ibGroupWords group = GroupWordsOf(*this);
			lines.push_back(word(KEY_GROUP) + ibT(" ") + group.m_kept + ibT(" ") + word(KEY_BY) + ibT(" ") + group.m_key
				+ ibT(" ") + word(KEY_INTO) + ibT(" ") + m_groupInto);
			say(KEY_GROUP, true); say(KEY_BY, false); say(KEY_INTO, false);

			// ⭐ AFTER `Into` THE ROWS ARE GONE - what follows reads the groups. A condition on groups, an
			// order or a column that still reads a source by its alias reads nothing the compiler can
			// reach, and is said with the way out rather than written.
			for (const ibString& condition : m_groupConditions) {
				const ibString alias = readsRow(condition);
				if (!alias.IsEmpty())
					refuse(ibString::Format(_("A condition on groups reads '%s' row by row; after grouping it reads the group '%s'."),
						alias, m_groupInto));
			}
			writeConditions(m_groupConditions);

			// An order written by a key's expression or name, or by a total's name, is that key or that
			// total as the group reaches it.
			ibString rowOrder;
			writeOrderAndCut([&](const ibString& key) -> ibString {
				for (size_t k = 0; k < m_groupKeys.size(); ++k)
					if (SameWords(key, m_groupKeys[k].m_expression) || SameWords(key, m_groupKeys[k].m_name))
						return group.m_keyAccess[k];
				for (size_t t = 0; t < m_totals.size(); ++t)
					if (SameWords(key, m_totals[t].m_name))
						return group.m_totalText[t];
				if (rowOrder.IsEmpty() && !readsRow(key).IsEmpty())
					rowOrder = key;
				return key;
			});
			if (!rowOrder.IsEmpty())
				refuse(ibString::Format(_("The order by '%s' reads a row, but the answer is groups: order by a key or a total."), rowOrder));

			// THE COLUMNS: the keys, the totals, then whatever else is written over the group.
			std::vector<std::pair<ibString, ibString>> columns;
			for (size_t k = 0; k < m_groupKeys.size(); ++k)
				columns.emplace_back(m_groupKeys[k].m_name, group.m_keyAccess[k]);
			for (size_t t = 0; t < m_totals.size(); ++t)
				columns.emplace_back(m_totals[t].m_name, group.m_totalText[t]);
			for (const ibLinqBlockField& field : m_fields) {
				if (field.m_expression.IsEmpty())
					continue;
				const ibString name = field.m_name.IsEmpty() ? field.m_expression : field.m_name;
				const ibString alias = readsRow(field.m_expression);
				if (!alias.IsEmpty())
					refuse(ibString::Format(_("The column '%s' reads '%s' row by row, but the query groups its rows: "
						"make it a key or a total on the Grouping tab, or write it over the group '%s'."), name, alias, m_groupInto));
				finished(field.m_expression);
				claim(name);
				columns.emplace_back(name, field.m_expression);
			}
			lines.push_back(word(KEY_SELECT) + ibT(" ") + braced(columns));
			say(KEY_SELECT, true);
		}
	}

	if (m_distinct) {
		lines.push_back(word(KEY_DISTINCT));
		say(KEY_DISTINCT, true);
	}

	// ⭐ THE ORDER, CHECKED BY THE TABLE THE COMPLETION OFFERS FROM. A block that table would not have
	// offered is a defect in this writer, said rather than written.
	int last = -1;
	for (const std::pair<int, bool>& entry : spoken) {
		if (entry.second && last != -1) {
			const std::vector<int> allowed = ibLinqClausesAfter(last);
			if (std::find(allowed.begin(), allowed.end(), entry.first) == allowed.end())
				refuse(ibString::Format(_("'%s' cannot follow '%s' in a query."), word(entry.first), word(last)));
		}
		last = entry.first;
	}

	if (!refusal.IsEmpty() && !draft)
		return ibString();

	ibString text;
	for (size_t i = 0; i < lines.size(); ++i)
		text += (i == 0 ? ibString() : ibString(ibT("\n")) + indent) + lines[i];
	return text;
}

// ---------------------------------------------------------------------------
//  Reading a block back
// ---------------------------------------------------------------------------

bool ibLinqBlock::Parse(const ibString& text, ibLinqBlock& block, ibString& refusal, size_t* consumed)
{
	block = ibLinqBlock(block.GetCodeStyle());   // emptied, read in the syntax it was made for
	refusal.clear();

	// THE LEXER READS IT, in the mode the editor reads a text being written: strings, comments and
	// brackets are its business, and a keyword after a dot (`o.Select`) is a name, as it decides.
	//
	// ⚠ A LEXEM NOTES WHERE ITS TOKEN STARTS - the lexer skips the whitespace and the comments in front
	// before it records the position (IsEnd -> SkipSpaces). Where a token ENDS is recorded nowhere, and
	// the first cut of this reader took it for "where the next one starts": every condition then carried
	// the comment after it, and a dot followed by a space looked like a dot followed by a name. CI on
	// 991d4d53, Linux and macOS alike - so the end is read here (tokenEnd). A `;` is added so the last
	// clause has a token after it too.
	const ibString source = text + ibT("\n;");
	ibTranslateCode lexer(block.GetCodeStyle());
	lexer.SetLexemMode(ibLexemMode::Editing);
	lexer.Load(source);
	try {
		lexer.Translate();
	}
	catch (...) {
		refusal = _("The query's text could not be read.");
		return false;
	}
	if (lexer.GetLexCount() == 0)
		return false;

	const size_t end = lexer.GetLexCount() - 1;   // the ENDPROGRAM marker

	// WHERE TOKEN `i` ENDS: a string at its closing quote (a doubled one is part of it), a date at its
	// closing apostrophe, anything else at the first whitespace or comment - and never past the start of
	// the token after it.
	const auto tokenEnd = [&](size_t i) -> unsigned int {
		const unsigned int from = lexer.GetLexAt(i).m_numString;
		const unsigned int limit = i + 1 < lexer.GetLexCount() ? lexer.GetLexAt(i + 1).m_numString : (unsigned int)source.length();
		if (from >= source.length())
			return from;
		const ibChar open = source[from];
		if (lexer.GetLexAt(i).m_lexType == CONSTANT && (open == ibT('"') || open == ibT('\''))) {
			for (unsigned int at = from + 1; at < source.length(); ++at) {
				if (source[at] != open)
					continue;
				if (open == ibT('"') && at + 1 < source.length() && source[at + 1] == ibT('"')) {
					++at;       // a doubled quote is a quote inside the string
					continue;
				}
				return at + 1;
			}
			return limit;
		}
		unsigned int at = from;
		while (at < limit && !ibIsSpace(source[at])
			&& !(source[at] == ibT('/') && at + 1 < limit && source[at + 1] == ibT('/')))
			++at;
		return at > from ? at : std::min(from + 1, limit);
	};

	// The text of tokens [a, b): from where token `a` starts to where token `b - 1` ends - no whitespace
	// and no comment on either side.
	const auto skipGap = [&source](unsigned int at) {
		while (at < source.length()) {
			const ibChar c = source[at];
			if (c == ibT(' ') || c == ibT('\t') || c == ibT('\r') || c == ibT('\n'))
				++at;
			else if (c == ibT('/') && at + 1 < source.length() && source[at + 1] == ibT('/'))
				while (at < source.length() && source[at] != ibT('\n'))
					++at;
			else
				break;
		}
		return at;
	};
	const auto span = [&](size_t a, size_t b) {
		if (b <= a || b > end + 1)
			return ibString();
		const unsigned int from = skipGap(lexer.GetLexAt(a).m_numString);
		const unsigned int to = tokenEnd(std::min(b, end) - 1);
		return ibString(source.Mid(from, to > from ? to - from : 0)).Trim(true).Trim(false);
	};
	const auto isDelimiter = [&](size_t i, ibChar c) {
		return i < end && lexer.GetLexAt(i).m_lexType == DELIMITER && lexer.GetLexAt(i).m_numData == (int)c;
	};
	// ⭐ THE QUERY WORD AT A TOKEN - including one the lexer took for a NAME because a dot stood before
	// it: `On g. Equals c.` reads as `g.Equals`, and the compiler stops right there. A dot with a SPACE
	// after it and a query word next is a field left unwritten, and the word is the query's; `o.Select`,
	// with no space, stays a name, as the lexer says.
	const auto keyAt = [&](size_t i) -> int {
		if (i >= end)
			return -1;
		if (lexer.GetLexAt(i).m_lexType == KEYWORD)
			return lexer.GetLexAt(i).m_numData;
		if (lexer.GetLexAt(i).m_lexType != IDENTIFIER || i == 0 || !isDelimiter(i - 1, ibT('.')))
			return -1;
		if (lexer.GetLexAt(i).m_numString <= tokenEnd(i - 1))
			return -1;   // the name touches its dot: `o.Select` is a name, as the lexer says
		for (const int key : { KEY_FROM, KEY_JOIN, KEY_IN, KEY_ON, KEY_EQUALS, KEY_WHERE, KEY_GROUP, KEY_BY, KEY_INTO,
				KEY_ORDERBY, KEY_ASCENDING, KEY_DESCENDING, KEY_SKIP, KEY_TAKE, KEY_SELECT, KEY_DISTINCT })
			if (lexer.GetLexAt(i).m_valData.GetString().IsSameAs(Word(key), false))
				return key;
		return -1;
	};
	const auto isKey = [&](size_t i, int key) {
		return keyAt(i) == key;
	};
	const auto name = [&](size_t i) {
		return i < end && lexer.GetLexAt(i).m_lexType == IDENTIFIER ? lexer.GetLexAt(i).m_valData.GetString() : ibString();
	};
	const auto opens = [](int key) {
		switch (key) {
		case KEY_FROM: case KEY_JOIN: case KEY_WHERE: case KEY_GROUP: case KEY_ORDERBY:
		case KEY_SKIP: case KEY_TAKE: case KEY_SELECT: case KEY_DISTINCT:
			return true;
		default:
			return false;
		}
	};

	// THE CLAUSES: every query word that opens one, at depth zero. A query nested in brackets is the
	// inside of an expression here, and is kept whole as its text.
	//
	// ⭐ AND WHERE THE QUERY ENDS: at a `;`, or where its answer is complete - the brace closing
	// `Select { … }` (and a `Distinct` after it), or the line of a bare `Select`. So a text handed over
	// past the query - the compiler's bracket ends where it STOPPED, which in a broken query is mid-way -
	// is read to the query's end and no further.
	std::vector<size_t> starts;
	int depth = 0;
	size_t stop = end;
	size_t selectAt = end;   // the `Select` at depth zero, once met
	for (size_t i = 0; i < end; ++i) {
		const ibLexem& l = lexer.GetLexAt(i);
		if (l.m_lexType == DELIMITER) {
			if (l.m_numData == '(' || l.m_numData == '[' || l.m_numData == '{') ++depth;
			else if (l.m_numData == ')' || l.m_numData == ']' || l.m_numData == '}') {
				--depth;
				if (depth == 0 && l.m_numData == '}' && selectAt + 1 < end && isDelimiter(selectAt + 1, ibT('{'))) {
					stop = isKey(i + 1, KEY_DISTINCT) ? i + 2 : i + 1;
					if (stop == i + 2)
						starts.push_back(i + 1);
					break;
				}
			}
			else if (l.m_numData == ';' && depth == 0) { stop = i; break; }
			continue;
		}
		if (depth == 0 && selectAt < end && !isDelimiter(selectAt + 1, ibT('{')) && l.GetLine() != lexer.GetLexAt(selectAt).GetLine()
			&& !isKey(i, KEY_DISTINCT)) {
			stop = i;   // a bare `Select` ends with its line
			break;
		}
		const int key = keyAt(i);
		if (depth != 0 || key < 0)
			continue;
		if (key == KEY_VAR) {
			refusal = ibString::Format(_("The query has a '%s' clause, which the constructor does not build yet."),
				ibTranslateCode::GetKeyWord(KEY_VAR));
			return false;
		}
		if (opens(key))
			starts.push_back(i);
		if (key == KEY_SELECT)
			selectAt = i;
	}
	if (starts.empty() || !isKey(starts.front(), KEY_FROM)) {
		refusal = _("There is no query here to open.");
		return false;
	}
	if (consumed != nullptr)
		*consumed = std::min<size_t>(stop > 0 ? tokenEnd(std::min(stop, end) - 1) : 0, text.length());   // where its last token ends

	// The positions of `keys` inside [from, to), at the clause's own depth zero - the modifiers
	// (`In`, `On`, `Equals`, `By`, `Into`) and the commas between keys and columns.
	const auto findAtTop = [&](size_t from, size_t to, const std::function<bool(size_t)>& match) {
		int level = 0;
		for (size_t i = from; i < to; ++i) {
			if (lexer.GetLexAt(i).m_lexType == DELIMITER) {
				if (lexer.GetLexAt(i).m_numData == '(' || lexer.GetLexAt(i).m_numData == '[' || lexer.GetLexAt(i).m_numData == '{') { ++level; continue; }
				if (lexer.GetLexAt(i).m_numData == ')' || lexer.GetLexAt(i).m_numData == ']' || lexer.GetLexAt(i).m_numData == '}') { --level; continue; }
			}
			if (level == 0 && match(i))
				return i;
		}
		return to;
	};
	const auto splitAtTop = [&](size_t from, size_t to, ibChar comma) {
		std::vector<std::pair<size_t, size_t>> parts;
		size_t at = from;
		while (at < to) {
			const size_t next = findAtTop(at, to, [&](size_t i) { return isDelimiter(i, comma); });
			parts.emplace_back(at, next);
			at = next + 1;
		}
		return parts;
	};

	// `New Structure("A, B", a, b)` standing exactly on [from, to): its names, and the text of each
	// value - how several keys, or the several fields a group keeps, are written (GroupWordsOf).
	const auto madeOne = [&](size_t from, size_t to, std::vector<std::pair<ibString, ibString>>& parts) {
		parts.clear();
		if (to <= from + 5 || !isKey(from, KEY_NEW) || !stringUtils::IsSameAs(name(from + 1), ibT("Structure"))
			|| !isDelimiter(from + 2, ibT('(')) || lexer.GetLexAt(from + 3).m_lexType != CONSTANT || !isDelimiter(from + 4, ibT(',')))
			return false;
		// The bracket that closes the one after `Structure` has to be the last token.
		size_t close = from + 2;
		for (int level = 0; close < to; ++close) {
			if (isDelimiter(close, ibT('(')) || isDelimiter(close, ibT('[')) || isDelimiter(close, ibT('{'))) ++level;
			else if ((isDelimiter(close, ibT(')')) || isDelimiter(close, ibT(']')) || isDelimiter(close, ibT('}'))) && --level == 0) break;
		}
		if (close != to - 1)
			return false;
		const std::vector<ibString> names = ibSplit(lexer.GetLexAt(from + 3).m_valData.GetString(), ibT(','), ibT('\0'));
		const auto values = splitAtTop(from + 5, to - 1, ibT(','));
		if (names.size() != values.size())
			return false;
		for (size_t i = 0; i < values.size(); ++i)
			parts.emplace_back(ibString(names[i]).Trim(true).Trim(false), span(values[i].first, values[i].second));
		return true;
	};

	bool grouped = false;
	bool rowsCut = false;                                   // ordered, skipped or taken before a group
	std::vector<std::pair<ibString, ibString>> keptParts;   // what a group keeps, when it keeps several fields…
	ibString keptOne;                                       // …or the one it keeps

	// ⭐ A COLUMN OF A GROUPED ANSWER, READ BACK AS WHAT IT WAS BUILT FROM: `<group>.Key` or
	// `<group>.Key.<name>` is a key, `<group>.Values.Sum(…)` a total over a field the group keeps. False
	// for anything else, which stays a column written over the group, as it is.
	const auto groupColumn = [&](const ibString& column, size_t a, size_t b) {
		const ibString& group = block.m_groupInto;
		if (b <= a + 2 || !name(a).IsSameAs(group, false) || !isDelimiter(a + 1, ibT('.')))
			return false;
		const ibString member = name(a + 2);
		if (stringUtils::IsSameAs(member, ibT("Key"))) {
			if (b == a + 3 && block.m_groupKeys.size() == 1) {
				block.m_groupKeys.front().m_name = column;
				return true;
			}
			if (b == a + 5 && isDelimiter(a + 3, ibT('.')))
				for (ibLinqBlockField& key : block.m_groupKeys)
					if (name(a + 4).IsSameAs(key.m_name, false)) {
						key.m_name = column;   // the key's field and its column are one name
						return true;
					}
			return false;
		}
		if (!stringUtils::IsSameAs(member, ibT("Values")) || !isDelimiter(a + 3, ibT('.'))
			|| !isDelimiter(a + 5, ibT('(')) || !isDelimiter(b - 1, ibT(')')))
			return false;

		ibLinqBlockTotal total;
		total.m_name = column;
		bool known = false;
		for (const ibLinqTotal candidate : { ibLinqTotal::Sum, ibLinqTotal::Count, ibLinqTotal::Min, ibLinqTotal::Max, ibLinqTotal::Average })
			if (name(a + 4).IsSameAs(ibLinqTotalName(candidate), false)) {
				total.m_function = candidate;
				known = true;
			}
		if (!known)
			return false;
		if (total.m_function == ibLinqTotal::Count) {
			if (b != a + 7)
				return false;
		}
		else if (b == a + 7) {
			// No argument: the group keeps one field - unless what it keeps is a row itself.
			if (keptOne.IsEmpty() || std::any_of(block.m_sources.begin(), block.m_sources.end(),
					[&](const ibLinqBlockSource& from) { return stringUtils::IsSameAs(from.m_alias, keptOne); }))
				return false;
			total.m_expression = keptOne;
		}
		else {
			// A function handing back one field of what the group keeps: `… Return <row>.<field> …`.
			size_t at = a + 6;
			while (at < b && !isKey(at, KEY_RETURN))
				++at;
			if (at + 3 >= b || !isDelimiter(at + 2, ibT('.')))
				return false;
			const ibString field = name(at + 3);
			for (const std::pair<ibString, ibString>& part : keptParts)
				if (stringUtils::IsSameAs(part.first, field))
					total.m_expression = part.second;
			if (total.m_expression.IsEmpty())
				return false;
		}
		block.m_totals.push_back(total);
		return true;
	};

	for (size_t c = 0; c < starts.size(); ++c) {
		const size_t s = starts[c];
		const size_t e = c + 1 < starts.size() ? starts[c + 1] : stop;
		const int key = keyAt(s);

		switch (key) {
		case KEY_FROM:
		case KEY_JOIN: {
			ibLinqBlockSource read;
			read.m_alias = name(s + 1);
			if (read.m_alias.IsEmpty() || !isKey(s + 2, KEY_IN)) {
				refusal = _("A source of the query is not written as the constructor reads one.");
				return false;
			}
			if (key == KEY_FROM) {
				read.m_expression = span(s + 3, e);
			}
			else {
				const size_t on = findAtTop(s + 3, e, [&](size_t i) { return isKey(i, KEY_ON); });
				const size_t equals = findAtTop(on, e, [&](size_t i) { return isKey(i, KEY_EQUALS); });
				read.m_join = true;
				read.m_expression = span(s + 3, on);
				read.m_leftKey = span(on + 1, equals);
				read.m_rightKey = span(equals + 1, e);
			}
			block.m_sources.push_back(read);
			break;
		}
		case KEY_WHERE:
			// Before a group a condition reads the rows; after it, the groups.
			(grouped ? block.m_groupConditions : block.m_conditions).push_back(span(s + 1, e));
			break;
		case KEY_GROUP: {
			if (grouped) {
				refusal = _("The query groups twice, which the constructor does not build yet.");
				return false;
			}
			grouped = true;
			const size_t by = findAtTop(s + 1, e, [&](size_t i) { return isKey(i, KEY_BY); });
			const size_t into = findAtTop(by, e, [&](size_t i) { return isKey(i, KEY_INTO); });

			// The keys: several made into one, or one as it is - named later, by its column.
			std::vector<std::pair<ibString, ibString>> keys;
			if (madeOne(by + 1, into, keys))
				for (const std::pair<ibString, ibString>& part : keys)
					block.m_groupKeys.push_back({ part.first, part.second });
			else
				block.m_groupKeys.push_back({ ibString(), span(by + 1, into) });

			// What each group keeps of its rows - what its totals read.
			if (!madeOne(s + 1, by, keptParts))
				keptOne = span(s + 1, by);

			if (into < e) {
				block.m_groupInto = name(into + 1);
				// Rows ordered or cut before a NAMED group: the constructor writes an order over the
				// groups, after it, and reading this one as that would change what the query answers.
				if (rowsCut) {
					refusal = _("The query orders or cuts its rows before grouping them, which the constructor does not build yet.");
					return false;
				}
			}
			break;
		}
		case KEY_ORDERBY: {
			rowsCut = rowsCut || !grouped;
			// Each key may close with its own way: `OrderBy a, b Descending, c`.
			for (const auto& part : splitAtTop(s + 1, e, ibT(','))) {
				size_t keyEnd = part.second;
				bool descending = false;
				if (part.second > part.first + 1
					&& (isKey(part.second - 1, KEY_DESCENDING) || isKey(part.second - 1, KEY_ASCENDING))) {
					descending = isKey(part.second - 1, KEY_DESCENDING);
					keyEnd = part.second - 1;
				}
				block.m_order.push_back({ span(part.first, keyEnd), descending });
			}
			break;
		}
		case KEY_SKIP:
			rowsCut = rowsCut || !grouped;
			block.m_skip = span(s + 1, e);
			break;
		case KEY_TAKE:
			rowsCut = rowsCut || !grouped;
			block.m_take = span(s + 1, e);
			break;
		case KEY_SELECT: {
			if (isDelimiter(s + 1, ibT('{'))) {
				// The columns inside the braces, a comma between each, `name = expression` each. The
				// closing brace is the one that brings the depth back to where the opening one left it.
				const size_t inner = s + 2;
				size_t closing = inner;
				for (int level = 0; closing < e; ++closing) {
					if (isDelimiter(closing, ibT('{')) || isDelimiter(closing, ibT('(')) || isDelimiter(closing, ibT('['))) ++level;
					else if (isDelimiter(closing, ibT(')')) || isDelimiter(closing, ibT(']'))) --level;
					else if (isDelimiter(closing, ibT('}'))) { if (level == 0) break; --level; }
				}
				const bool overGroups = grouped && !block.m_groupInto.IsEmpty();
				for (const auto& part : splitAtTop(inner, closing, ibT(','))) {
					const size_t assign = findAtTop(part.first, part.second, [&](size_t i) { return isDelimiter(i, ibT('=')); });
					ibLinqBlockField field;
					size_t valueFrom = part.first;
					if (assign < part.second) {
						field.m_name = span(part.first, assign);
						valueFrom = assign + 1;
					}
					field.m_expression = span(valueFrom, part.second);
					if (field.m_expression.IsEmpty())
						continue;
					if (overGroups && groupColumn(field.m_name, valueFrom, part.second))
						continue;
					block.m_fields.push_back(field);
				}
			}
			else {
				// `Select <row>` is the row itself; anything else is one column the language names.
				const ibString selected = span(s + 1, e);
				const ibString row = block.m_groupInto.IsEmpty()
					? (block.m_sources.empty() ? ibString() : block.m_sources.front().m_alias) : block.m_groupInto;
				if (!stringUtils::IsSameAs(selected, row)
					&& !(grouped && !block.m_groupInto.IsEmpty() && groupColumn(ibString(), s + 1, e)))
					block.m_fields.push_back({ ibString(), selected });
			}
			break;
		}
		case KEY_DISTINCT:
			block.m_distinct = true;
			break;
		default:
			break;
		}
	}

	if (grouped) {
		// A key no column named takes the name its expression ends with - `o.Customer` is `Customer`.
		for (ibLinqBlockField& key : block.m_groupKeys) {
			if (!key.m_name.IsEmpty())
				continue;
			const ibString last = key.m_expression.AfterLast(ibT('.'));
			key.m_name = IsName(last) ? last : ibString(ibT("Key"));
		}
		// An order over the groups written in the group's words is read as what Render writes it from:
		// a key by its expression, a total by its name.
		if (!block.m_groupInto.IsEmpty()) {
			const ibGroupWords words = GroupWordsOf(block);
			for (ibLinqBlockOrder& order : block.m_order) {
				for (size_t k = 0; k < block.m_groupKeys.size(); ++k)
					if (SameWords(order.m_expression, words.m_keyAccess[k]))
						order.m_expression = block.m_groupKeys[k].m_expression;
				for (size_t t = 0; t < block.m_totals.size(); ++t)
					if (SameWords(order.m_expression, words.m_totalText[t]))
						order.m_expression = block.m_totals[t].m_name;
			}
		}
	}
	return true;
}
