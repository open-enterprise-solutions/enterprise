///////////////////////////////////////////////////////////////////////////
// Name:        core/localization/dictionary.cpp
// Purpose:     the engine's own message catalogs - gettext .po files, one set per language, chosen per thread
// Author:      the plural-forms calculator is wxWidgets' (src/common/translation.cpp: Vadim Zeitlin, Vaclav
//              Slavik, Michael N. Filippov), renamed; the .po reader, the sets and the choice are ours
// Copyright:   (c) 1998 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr> for the parts taken from wxWidgets
// Licence:     wxWindows licence for those parts - NOTICE.md
///////////////////////////////////////////////////////////////////////////

#include "core/localization/dictionary.h"

#include "core/diagnostics/journal.h"
#include "core/fileSystem.h"   // ibProgramFolder, ibProgramSubfolder, ibGetCwd, ibPathOf — where the catalogs are

#include <algorithm>
#include <atomic>
#include <cctype>
#include <climits>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// THE CATALOGS' WORDS — said once. gettext's: the layout under a language, the extension, the key of a word in a
// context, the header's two fields read; the engine's: the folder the catalogs stand in, the journal's source.
const ibChar* const        kCatalogFolder    = ibT("lang");          // <prefix>/<language>/<domain>.po
const ibChar* const        kMessagesFolder   = ibT("LC_MESSAGES");   // gettext's <language>/LC_MESSAGES/<domain>.po
const ibChar* const        kCatalogExtension = ibT(".po");
const ibChar* const        kJournalSource    = ibT("i18n");
constexpr char             kContextSeparator = '\x04';               // a key in a context: <context>\x04<text>
constexpr unsigned char    kByteOrderMark[]  = { 0xEF, 0xBB, 0xBF };   // UTF-8's, as bytes (codePage.cpp)
constexpr std::string_view kCharsetField     = "Content-Type: text/plain; charset=";
constexpr std::string_view kPluralFormsField = "Plural-Forms:";

typedef std::unordered_map<ibString, ibString> ibTranslationsHashMap;

class ibPluralFormsCalculator;
using ibPluralFormsCalculatorPtr = std::unique_ptr<ibPluralFormsCalculator>;

// ============================================================================
// Plural forms parser - wxWidgets' own (src/common/translation.cpp), renamed.
// ============================================================================
class ibPluralFormsToken
{
public:
    enum Type
    {
        T_ERROR, T_EOF, T_NUMBER, T_N, T_PLURAL, T_NPLURALS, T_EQUAL, T_ASSIGN,
        T_GREATER, T_GREATER_OR_EQUAL, T_LESS, T_LESS_OR_EQUAL,
        T_REMINDER, T_NOT_EQUAL,
        T_LOGICAL_AND, T_LOGICAL_OR, T_QUESTION, T_COLON, T_SEMICOLON,
        T_LEFT_BRACKET, T_RIGHT_BRACKET
    };
    Type type() const { return m_type; }
    void setType(Type t) { m_type = t; }
    // for T_NUMBER only
    typedef int Number;
    Number number() const { return m_number; }
    void setNumber(Number num) { m_number = num; }
private:
    Type m_type;
    Number m_number;
};


class ibPluralFormsScanner
{
public:
    ibPluralFormsScanner(const char* s);
    const ibPluralFormsToken& token() const { return m_token; }
    bool nextToken();  // returns false if error
private:
    const char* m_s;
    ibPluralFormsToken m_token;
};

ibPluralFormsScanner::ibPluralFormsScanner(const char* s) : m_s(s)
{
    nextToken();
}

bool ibPluralFormsScanner::nextToken()
{
    ibPluralFormsToken::Type type = ibPluralFormsToken::T_ERROR;
    while (isspace((unsigned char) *m_s))
    {
        ++m_s;
    }
    if (*m_s == 0)
    {
        type = ibPluralFormsToken::T_EOF;
    }
    else if (isdigit((unsigned char) *m_s))
    {
        ibPluralFormsToken::Number number = *m_s++ - '0';
        while (isdigit((unsigned char) *m_s))
        {
            number = number * 10 + (*m_s++ - '0');
        }
        m_token.setNumber(number);
        type = ibPluralFormsToken::T_NUMBER;
    }
    else if (isalpha((unsigned char) *m_s))
    {
        const char* begin = m_s++;
        while (isalnum((unsigned char) *m_s))
        {
            ++m_s;
        }
        size_t size = m_s - begin;
        if (size == 1 && memcmp(begin, "n", size) == 0)
        {
            type = ibPluralFormsToken::T_N;
        }
        else if (size == 6 && memcmp(begin, "plural", size) == 0)
        {
            type = ibPluralFormsToken::T_PLURAL;
        }
        else if (size == 8 && memcmp(begin, "nplurals", size) == 0)
        {
            type = ibPluralFormsToken::T_NPLURALS;
        }
    }
    else if (*m_s == '=')
    {
        ++m_s;
        if (*m_s == '=')
        {
            ++m_s;
            type = ibPluralFormsToken::T_EQUAL;
        }
        else
        {
            type = ibPluralFormsToken::T_ASSIGN;
        }
    }
    else if (*m_s == '>')
    {
        ++m_s;
        if (*m_s == '=')
        {
            ++m_s;
            type = ibPluralFormsToken::T_GREATER_OR_EQUAL;
        }
        else
        {
            type = ibPluralFormsToken::T_GREATER;
        }
    }
    else if (*m_s == '<')
    {
        ++m_s;
        if (*m_s == '=')
        {
            ++m_s;
            type = ibPluralFormsToken::T_LESS_OR_EQUAL;
        }
        else
        {
            type = ibPluralFormsToken::T_LESS;
        }
    }
    else if (*m_s == '%')
    {
        ++m_s;
        type = ibPluralFormsToken::T_REMINDER;
    }
    else if (*m_s == '!' && m_s[1] == '=')
    {
        m_s += 2;
        type = ibPluralFormsToken::T_NOT_EQUAL;
    }
    else if (*m_s == '&' && m_s[1] == '&')
    {
        m_s += 2;
        type = ibPluralFormsToken::T_LOGICAL_AND;
    }
    else if (*m_s == '|' && m_s[1] == '|')
    {
        m_s += 2;
        type = ibPluralFormsToken::T_LOGICAL_OR;
    }
    else if (*m_s == '?')
    {
        ++m_s;
        type = ibPluralFormsToken::T_QUESTION;
    }
    else if (*m_s == ':')
    {
        ++m_s;
        type = ibPluralFormsToken::T_COLON;
    } else if (*m_s == ';') {
        ++m_s;
        type = ibPluralFormsToken::T_SEMICOLON;
    }
    else if (*m_s == '(')
    {
        ++m_s;
        type = ibPluralFormsToken::T_LEFT_BRACKET;
    }
    else if (*m_s == ')')
    {
        ++m_s;
        type = ibPluralFormsToken::T_RIGHT_BRACKET;
    }
    m_token.setType(type);
    return type != ibPluralFormsToken::T_ERROR;
}

class ibPluralFormsNode;

using ibPluralFormsNodePtr = std::unique_ptr<ibPluralFormsNode>;

class ibPluralFormsNode
{
public:
    ibPluralFormsNode(const ibPluralFormsToken& t) : m_token(t) {}
    const ibPluralFormsToken& token() const { return m_token; }
    const ibPluralFormsNode* node(unsigned i) const
        { return m_nodes[i].get(); }
    void setNode(unsigned i, ibPluralFormsNode* n);
    ibPluralFormsNode* releaseNode(unsigned i);
    ibPluralFormsToken::Number evaluate(ibPluralFormsToken::Number n) const;

private:
    ibPluralFormsToken m_token;
    ibPluralFormsNodePtr m_nodes[3];
};

void ibPluralFormsNode::setNode(unsigned i, ibPluralFormsNode* n)
{
    m_nodes[i].reset(n);
}

ibPluralFormsNode*  ibPluralFormsNode::releaseNode(unsigned i)
{
    return m_nodes[i].release();
}

ibPluralFormsToken::Number
ibPluralFormsNode::evaluate(ibPluralFormsToken::Number n) const
{
    switch (token().type())
    {
        // leaf
        case ibPluralFormsToken::T_NUMBER:
            return token().number();
        case ibPluralFormsToken::T_N:
            return n;
        // 2 args
        case ibPluralFormsToken::T_EQUAL:
            return node(0)->evaluate(n) == node(1)->evaluate(n);
        case ibPluralFormsToken::T_NOT_EQUAL:
            return node(0)->evaluate(n) != node(1)->evaluate(n);
        case ibPluralFormsToken::T_GREATER:
            return node(0)->evaluate(n) > node(1)->evaluate(n);
        case ibPluralFormsToken::T_GREATER_OR_EQUAL:
            return node(0)->evaluate(n) >= node(1)->evaluate(n);
        case ibPluralFormsToken::T_LESS:
            return node(0)->evaluate(n) < node(1)->evaluate(n);
        case ibPluralFormsToken::T_LESS_OR_EQUAL:
            return node(0)->evaluate(n) <= node(1)->evaluate(n);
        case ibPluralFormsToken::T_REMINDER:
            {
                ibPluralFormsToken::Number number = node(1)->evaluate(n);
                if (number != 0)
                {
                    return node(0)->evaluate(n) % number;
                }
                else
                {
                    return 0;
                }
            }
        case ibPluralFormsToken::T_LOGICAL_AND:
            return node(0)->evaluate(n) && node(1)->evaluate(n);
        case ibPluralFormsToken::T_LOGICAL_OR:
            return node(0)->evaluate(n) || node(1)->evaluate(n);
        // 3 args
        case ibPluralFormsToken::T_QUESTION:
            return node(0)->evaluate(n)
                ? node(1)->evaluate(n)
                : node(2)->evaluate(n);
        default:
            return 0;
    }
}


class ibPluralFormsCalculator
{
public:
    ibPluralFormsCalculator() : m_nplurals(0), m_plural(nullptr) {}

    // input: number, returns msgstr index
    int evaluate(int n) const;

    // input: text after "Plural-Forms:" (e.g. "nplurals=2; plural=(n != 1);"),
    // if s == 0, creates default handler
    // returns 0 if error
    static ibPluralFormsCalculator* make(const char* s = nullptr);

    ~ibPluralFormsCalculator() {}

    void  init(ibPluralFormsToken::Number nplurals, ibPluralFormsNode* plural);

private:
    ibPluralFormsToken::Number m_nplurals;
    ibPluralFormsNodePtr m_plural;
};

void ibPluralFormsCalculator::init(ibPluralFormsToken::Number nplurals,
                                ibPluralFormsNode* plural)
{
    m_nplurals = nplurals;
    m_plural.reset(plural);
}

int ibPluralFormsCalculator::evaluate(int n) const
{
    if (m_plural.get() == nullptr)
    {
        return 0;
    }
    ibPluralFormsToken::Number number = m_plural->evaluate(n);
    if (number < 0 || number > m_nplurals)
    {
        return 0;
    }
    return number;
}


class ibPluralFormsParser
{
public:
    ibPluralFormsParser(ibPluralFormsScanner& scanner) : m_scanner(scanner) {}
    bool parse(ibPluralFormsCalculator& rCalculator);

private:
    ibPluralFormsNode* parsePlural();
    // stops at T_SEMICOLON, returns 0 if error
    ibPluralFormsScanner& m_scanner;
    const ibPluralFormsToken& token() const;
    bool nextToken();

    ibPluralFormsNode* expression();
    ibPluralFormsNode* logicalOrExpression();
    ibPluralFormsNode* logicalAndExpression();
    ibPluralFormsNode* equalityExpression();
    ibPluralFormsNode* multiplicativeExpression();
    ibPluralFormsNode* relationalExpression();
    ibPluralFormsNode* pmExpression();
};

bool ibPluralFormsParser::parse(ibPluralFormsCalculator& rCalculator)
{
    if (token().type() != ibPluralFormsToken::T_NPLURALS)
        return false;
    if (!nextToken())
        return false;
    if (token().type() != ibPluralFormsToken::T_ASSIGN)
        return false;
    if (!nextToken())
        return false;
    if (token().type() != ibPluralFormsToken::T_NUMBER)
        return false;
    ibPluralFormsToken::Number nplurals = token().number();
    if (!nextToken())
        return false;
    if (token().type() != ibPluralFormsToken::T_SEMICOLON)
        return false;
    if (!nextToken())
        return false;
    if (token().type() != ibPluralFormsToken::T_PLURAL)
        return false;
    if (!nextToken())
        return false;
    if (token().type() != ibPluralFormsToken::T_ASSIGN)
        return false;
    if (!nextToken())
        return false;
    ibPluralFormsNode* plural = parsePlural();
    if (plural == nullptr)
        return false;
    if (token().type() != ibPluralFormsToken::T_SEMICOLON)
        return false;
    if (!nextToken())
        return false;
    if (token().type() != ibPluralFormsToken::T_EOF)
        return false;
    rCalculator.init(nplurals, plural);
    return true;
}

ibPluralFormsNode* ibPluralFormsParser::parsePlural()
{
    ibPluralFormsNode* p = expression();
    if (p == nullptr)
    {
        return nullptr;
    }
    ibPluralFormsNodePtr n(p);
    if (token().type() != ibPluralFormsToken::T_SEMICOLON)
    {
        return nullptr;
    }
    return n.release();
}

const ibPluralFormsToken& ibPluralFormsParser::token() const
{
    return m_scanner.token();
}

bool ibPluralFormsParser::nextToken()
{
    if (!m_scanner.nextToken())
        return false;
    return true;
}

ibPluralFormsNode* ibPluralFormsParser::expression()
{
    ibPluralFormsNode* p = logicalOrExpression();
    if (p == nullptr)
        return nullptr;
    ibPluralFormsNodePtr n(p);
    if (token().type() == ibPluralFormsToken::T_QUESTION)
    {
        ibPluralFormsNodePtr qn(new ibPluralFormsNode(token()));
        if (!nextToken())
        {
            return nullptr;
        }
        p = expression();
        if (p == nullptr)
        {
            return nullptr;
        }
        qn->setNode(1, p);
        if (token().type() != ibPluralFormsToken::T_COLON)
        {
            return nullptr;
        }
        if (!nextToken())
        {
            return nullptr;
        }
        p = expression();
        if (p == nullptr)
        {
            return nullptr;
        }
        qn->setNode(2, p);
        qn->setNode(0, n.release());
        return qn.release();
    }
    return n.release();
}

ibPluralFormsNode*ibPluralFormsParser::logicalOrExpression()
{
    ibPluralFormsNode* p = logicalAndExpression();
    if (p == nullptr)
        return nullptr;
    ibPluralFormsNodePtr ln(p);
    if (token().type() == ibPluralFormsToken::T_LOGICAL_OR)
    {
        ibPluralFormsNodePtr un(new ibPluralFormsNode(token()));
        if (!nextToken())
        {
            return nullptr;
        }
        p = logicalOrExpression();
        if (p == nullptr)
        {
            return nullptr;
        }
        ibPluralFormsNodePtr rn(p);    // right
        if (rn->token().type() == ibPluralFormsToken::T_LOGICAL_OR)
        {
            // see logicalAndExpression comment
            un->setNode(0, ln.release());
            un->setNode(1, rn->releaseNode(0));
            rn->setNode(0, un.release());
            return rn.release();
        }


        un->setNode(0, ln.release());
        un->setNode(1, rn.release());
        return un.release();
    }
    return ln.release();
}

ibPluralFormsNode* ibPluralFormsParser::logicalAndExpression()
{
    ibPluralFormsNode* p = equalityExpression();
    if (p == nullptr)
        return nullptr;
    ibPluralFormsNodePtr ln(p);   // left
    if (token().type() == ibPluralFormsToken::T_LOGICAL_AND)
    {
        ibPluralFormsNodePtr un(new ibPluralFormsNode(token()));  // up
        if (!nextToken())
        {
            return nullptr;
        }
        p = logicalAndExpression();
        if (p == nullptr)
        {
            return nullptr;
        }
        ibPluralFormsNodePtr rn(p);    // right
        if (rn->token().type() == ibPluralFormsToken::T_LOGICAL_AND)
        {
// transform 1 && (2 && 3) -> (1 && 2) && 3
//     u                  r
// l       r     ->   u      3
//       2   3      l   2
            un->setNode(0, ln.release());
            un->setNode(1, rn->releaseNode(0));
            rn->setNode(0, un.release());
            return rn.release();
        }

        un->setNode(0, ln.release());
        un->setNode(1, rn.release());
        return un.release();
    }
    return ln.release();
}

ibPluralFormsNode* ibPluralFormsParser::equalityExpression()
{
    ibPluralFormsNode* p = relationalExpression();
    if (p == nullptr)
        return nullptr;
    ibPluralFormsNodePtr n(p);
    if (token().type() == ibPluralFormsToken::T_EQUAL
        || token().type() == ibPluralFormsToken::T_NOT_EQUAL)
    {
        ibPluralFormsNodePtr qn(new ibPluralFormsNode(token()));
        if (!nextToken())
        {
            return nullptr;
        }
        p = relationalExpression();
        if (p == nullptr)
        {
            return nullptr;
        }
        qn->setNode(1, p);
        qn->setNode(0, n.release());
        return qn.release();
    }
    return n.release();
}

ibPluralFormsNode* ibPluralFormsParser::relationalExpression()
{
    ibPluralFormsNode* p = multiplicativeExpression();
    if (p == nullptr)
        return nullptr;
    ibPluralFormsNodePtr n(p);
    if (token().type() == ibPluralFormsToken::T_GREATER
            || token().type() == ibPluralFormsToken::T_LESS
            || token().type() == ibPluralFormsToken::T_GREATER_OR_EQUAL
            || token().type() == ibPluralFormsToken::T_LESS_OR_EQUAL)
    {
        ibPluralFormsNodePtr qn(new ibPluralFormsNode(token()));
        if (!nextToken())
        {
            return nullptr;
        }
        p = multiplicativeExpression();
        if (p == nullptr)
        {
            return nullptr;
        }
        qn->setNode(1, p);
        qn->setNode(0, n.release());
        return qn.release();
    }
    return n.release();
}

ibPluralFormsNode* ibPluralFormsParser::multiplicativeExpression()
{
    ibPluralFormsNode* p = pmExpression();
    if (p == nullptr)
        return nullptr;
    ibPluralFormsNodePtr n(p);
    if (token().type() == ibPluralFormsToken::T_REMINDER)
    {
        ibPluralFormsNodePtr qn(new ibPluralFormsNode(token()));
        if (!nextToken())
        {
            return nullptr;
        }
        p = pmExpression();
        if (p == nullptr)
        {
            return nullptr;
        }
        qn->setNode(1, p);
        qn->setNode(0, n.release());
        return qn.release();
    }
    return n.release();
}

ibPluralFormsNode* ibPluralFormsParser::pmExpression()
{
    ibPluralFormsNodePtr n;
    if (token().type() == ibPluralFormsToken::T_N
        || token().type() == ibPluralFormsToken::T_NUMBER)
    {
        n.reset(new ibPluralFormsNode(token()));
        if (!nextToken())
        {
            return nullptr;
        }
    }
    else if (token().type() == ibPluralFormsToken::T_LEFT_BRACKET) {
        if (!nextToken())
        {
            return nullptr;
        }
        ibPluralFormsNode* p = expression();
        if (p == nullptr)
        {
            return nullptr;
        }
        n.reset(p);
        if (token().type() != ibPluralFormsToken::T_RIGHT_BRACKET)
        {
            return nullptr;
        }
        if (!nextToken())
        {
            return nullptr;
        }
    }
    else
    {
        return nullptr;
    }
    return n.release();
}

ibPluralFormsCalculator* ibPluralFormsCalculator::make(const char* s)
{
    ibPluralFormsCalculatorPtr calculator(new ibPluralFormsCalculator);
    if (s != nullptr)
    {
        ibPluralFormsScanner scanner(s);
        ibPluralFormsParser p(scanner);
        if (!p.parse(*calculator))
        {
            return nullptr;
        }
    }
    return calculator.release();
}

// A .po string, its quotes and its escapes taken off, added to `text`: `"Say \"no\"\n"` is `Say "no"` and a line
// break. False for what is no quoted string - a quote nobody escaped inside, an escape C has not. What stands between
// two escapes goes in at once: a character at a time was most of the parse in a Debug build.
bool Unquote(std::string_view line, std::string& text)
{
	while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
		line.remove_prefix(1);
	while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
		line.remove_suffix(1);
	if (line.size() < 2 || line.front() != '"' || line.back() != '"')
		return false;
	const std::string_view inside = line.substr(1, line.size() - 2);
	for (size_t i = 0; i < inside.size();) {
		const size_t stop = std::min(inside.find_first_of("\\\"", i), inside.size());
		text.append(inside.data() + i, stop - i);
		if (stop == inside.size())
			break;
		if (inside[stop] == '"')
			return false;
		if (stop + 1 == inside.size())
			return false;   // what it escapes is the closing quote
		i = stop + 1;
		const char escaped = inside[i++];
		switch (escaped) {
		case 'n': text += '\n'; break;
		case 't': text += '\t'; break;
		case 'r': text += '\r'; break;
		case 'a': text += '\a'; break;
		case 'b': text += '\b'; break;
		case 'f': text += '\f'; break;
		case 'v': text += '\v'; break;
		case '"': case '\\': case '\'': case '?': text += escaped; break;
		default: {
			if (escaped < '0' || escaped > '7')
				return false;
			int value = escaped - '0';   // octal, up to three digits
			for (int digit = 1; digit < 3 && i < inside.size() && inside[i] >= '0' && inside[i] <= '7'; ++digit)
				value = value * 8 + (inside[i++] - '0');
			text += static_cast<char>(value);
			break;
		}
		}
	}
	return true;
}

// The conversions a c-format string names, each by its length and its kind, in its arguments' order - `%d`, `%s`,
// `%.2f`, `%lld`; `%%` is a character, and `%2$s` stands at its position. A translation must name the same, or
// Format would be handed arguments it does not take.
std::string Conversions(const std::string& text)
{
	std::vector<std::pair<unsigned, std::string>> found;
	unsigned next = 0;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] != '%')
			continue;
		if (++i < text.size() && text[i] == '%')
			continue;
		unsigned position = ++next;
		size_t digits = i;
		unsigned written = 0;
		while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits])))
			written = written * 10 + static_cast<unsigned>(text[digits++] - '0');
		if (digits > i && digits < text.size() && text[digits] == '$') {
			position = written;
			i = digits + 1;
		}
		while (i < text.size() && std::string_view("-+ #0'").find(text[i]) != std::string_view::npos)
			++i;   // the flags
		while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.' || text[i] == '*'))
			++i;   // the width and the precision
		std::string conversion;
		while (i < text.size() && std::string_view("hlLqjzt").find(text[i]) != std::string_view::npos)
			conversion += text[i++];
		if (i < text.size())
			conversion += text[i] == 'i' ? 'd' : text[i];
		found.emplace_back(position, conversion);
	}
	std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	std::string signature;
	for (const auto& one : found)
		signature += one.second + ';';
	return signature;
}

// ============================================================================
// ibMsgCatalogFile - the catalog's file: gettext's .po as the translators write it (lang/<language>/<domain>.po),
// read as it is - nothing compiles it (2026-11-23; until then wxWidgets' reader of the compiled .mo). Its header
// entry (msgid "") says the charset and the plural rule, as a .mo's did. An entry still `fuzzy`, an empty msgstr and
// an obsolete entry (#~) are no translation - msgfmt left them out; a form of a c-format entry naming other
// conversions than its text is not taken - msgfmt --check refused it. A line no .po has refuses the whole file:
// half a catalog would be read as a whole one.
// ============================================================================
class ibMsgCatalogFile
{
public:
	// The file read whole and taken apart, each entry into the hash as it ends; false when it cannot be read or is no
	// catalog (the journal says where) - what the hash took by then is the caller's to drop.
	bool LoadFile(const ibString& filename, ibPluralFormsCalculatorPtr& rPluralFormsCalculator, ibTranslationsHashMap& hash);

private:
	// One entry, its strings as written - UTF-8, the escapes undone. ONE serves the whole file, emptied between entries:
	// its strings keep their room, so an entry costs no allocation once the first ones have grown them. A list of them
	// was a second copy of the hash, and in a Debug build its allocations - five containers an entry, each with its
	// proxy - were most of the parse (2026-11-23).
	struct ibEntry
	{
		std::string              m_context;
		bool                     m_hasContext = false;
		std::string              m_id;
		std::string              m_idPlural;
		std::vector<std::string> m_texts;   // msgstr, or msgstr[0..n]; an empty one is no translation
		bool                     m_fuzzy = false;
		bool                     m_cFormat = false;
	};

	// The text taken apart, each entry into the hash, and the header read; false at the first line no .po has,
	// `badLine` naming it.
	bool LoadData(const std::string& data, ibPluralFormsCalculatorPtr& rPluralFormsCalculator, ibTranslationsHashMap& hash,
		size_t& badLine);

	// fills the hash with an entry's string-translation pairs
	void FillHash(const ibEntry& entry, ibTranslationsHashMap& hash) const;

	ibString m_filename;   // what the journal names
};

bool ibMsgCatalogFile::LoadFile(const ibString& filename, ibPluralFormsCalculatorPtr& rPluralFormsCalculator,
	ibTranslationsHashMap& hash)
{
	// Read whole at once: a catalog is a megabyte, and a character at a time is a call a byte, each one checked in a
	// Debug build - the read alone took 115 ms of the Russian catalog's load (2026-11-23).
	std::ifstream file(ibPathOf(filename), std::ios::binary | std::ios::ate);
	if (!file.is_open())
		return false;
	const std::streamoff size = file.tellg();
	if (size < 0)
		return false;
	std::string data(static_cast<size_t>(size), '\0');
	file.seekg(0);
	if (!file.read(data.data(), static_cast<std::streamsize>(size)))
		return false;

	m_filename = filename;
	size_t badLine = 0;
	if (!LoadData(data, rPluralFormsCalculator, hash, badLine)) {
		ibJournalWarning(kJournalSource, ibT("'%s' is not a valid message catalog: line %u"), filename, static_cast<unsigned>(badLine));
		return false;
	}
	return true;
}


bool ibMsgCatalogFile::LoadData(const std::string& data, ibPluralFormsCalculatorPtr& rPluralFormsCalculator,
	ibTranslationsHashMap& hash, size_t& badLine)
{
	ibEntry entry;
	std::string header;             // the header entry's msgstr, read when the file is
	std::string* field = nullptr;   // the string a line of "…" carries on
	bool started = false;           // the entry has its msgid
	bool translated = false;        // …and a msgstr: a keyword or a comment after it starts the next one
	const auto finish = [&]() {
		if (started) {
			if (!entry.m_hasContext && entry.m_id.empty()) {
				if (!entry.m_texts.empty())
					header = entry.m_texts[0];
			}
			else
				FillHash(entry, hash);
		}
		// Emptied, not made anew - its strings keep their room (ibEntry).
		entry.m_context.clear();
		entry.m_hasContext = false;
		entry.m_id.clear();
		entry.m_idPlural.clear();
		for (std::string& text : entry.m_texts)
			text.clear();
		entry.m_fuzzy = entry.m_cFormat = false;
		field = nullptr;
		started = translated = false;
	};

	size_t at = data.size() >= sizeof kByteOrderMark && std::memcmp(data.data(), kByteOrderMark, sizeof kByteOrderMark) == 0
		? sizeof kByteOrderMark : 0;
	for (size_t number = 1; at < data.size(); ++number) {
		size_t end = data.find('\n', at);
		if (end == std::string::npos)
			end = data.size();
		std::string_view line(data.data() + at, end - at);
		at = end + 1;
		if (!line.empty() && line.back() == '\r')
			line.remove_suffix(1);
		while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
			line.remove_prefix(1);

		if (line.empty()) {
			field = nullptr;
			continue;
		}

		// A comment of the next entry ends this one: `#,` says the flags, `#~` is an entry nobody uses any more, the
		// rest - the source's place, a note - are for the translator.
		if (line.front() == '#') {
			if (translated)
				finish();
			field = nullptr;
			if (line.size() > 1 && line[1] == ',') {
				for (std::string_view flags = line.substr(2); !flags.empty();) {
					const size_t comma = flags.find(',');
					std::string_view flag = flags.substr(0, comma);
					flags = comma == std::string_view::npos ? std::string_view() : flags.substr(comma + 1);
					while (!flag.empty() && flag.front() == ' ')
						flag.remove_prefix(1);
					while (!flag.empty() && flag.back() == ' ')
						flag.remove_suffix(1);
					if (flag == "fuzzy")
						entry.m_fuzzy = true;
					else if (flag == "c-format")
						entry.m_cFormat = true;
				}
			}
			continue;
		}

		// A string carried over to the next line.
		if (line.front() == '"') {
			if (field == nullptr || !Unquote(line, *field)) {
				badLine = number;
				return false;
			}
			continue;
		}

		// A keyword and its string: the field it names, then the string into it.
		const size_t space = line.find_first_of(" \t");
		const std::string_view keyword = line.substr(0, space);
		if (space == std::string_view::npos) {
			badLine = number;
			return false;
		}
		if (keyword == "msgctxt" || keyword == "msgid") {
			if (translated)
				finish();
			if (keyword == "msgctxt") {
				entry.m_hasContext = true;
				field = &entry.m_context;
			}
			else {
				started = true;
				field = &entry.m_id;
			}
		}
		else if (keyword == "msgid_plural" && started) {
			field = &entry.m_idPlural;
		}
		else if (keyword == "msgstr" && started) {
			if (entry.m_texts.empty())
				entry.m_texts.resize(1);
			field = &entry.m_texts[0];
			translated = true;
		}
		else if (keyword.starts_with("msgstr[") && keyword.ends_with(']') && started) {
			// The form between the brackets.
			const size_t open = keyword.find('[');
			const std::string_view digits = keyword.substr(open + 1, keyword.size() - open - 2);
			if (digits.empty()) {
				badLine = number;
				return false;
			}
			size_t form = 0;
			for (const char digit : digits) {
				if (digit < '0' || digit > '9' || form > 99) {
					badLine = number;
					return false;
				}
				form = form * 10 + static_cast<size_t>(digit - '0');
			}
			if (entry.m_texts.size() <= form)
				entry.m_texts.resize(form + 1);
			field = &entry.m_texts[form];
			translated = true;
		}
		else {
			badLine = number;
			return false;
		}
		field->clear();
		if (!Unquote(line.substr(space), *field)) {
			badLine = number;
			return false;
		}
	}
	finish();

	// THE HEADER — the entry with no msgid: the charset and the plural rule.
	if (!header.empty()) {
		const size_t charset = header.find(kCharsetField);
		if (charset != std::string::npos) {
			const size_t from = charset + kCharsetField.size();
			const size_t end = header.find('\n', from);
			if (end != std::string::npos) {
				const ibString named = ibString::FromUTF8(header.substr(from, end - from));
				// The engine's catalogs are UTF-8; one that names another charset is read as UTF-8 too, and the
				// journal says so. "CHARSET" is the template's word, left by a translator: no charset named.
				if (!named.IsEmpty() && !(named == ibT("CHARSET")) && named.CmpNoCase(ibT("UTF-8")) != 0)
					ibJournalWarning(kJournalSource, ibT("a message catalog says it is in %s; it is read as UTF-8"), named);
			}
		}

		const size_t plurals = header.find(kPluralFormsField);
		if (plurals != std::string::npos) {
			const size_t from = plurals + kPluralFormsField.size();
			const size_t end = header.find('\n', from);
			if (end != std::string::npos) {
				const std::string rule = header.substr(from, end - from);
				if (ibPluralFormsCalculator* const calculator = ibPluralFormsCalculator::make(rule.c_str()))
					rPluralFormsCalculator.reset(calculator);
				else
					ibJournalWarning(kJournalSource, ibT("Plural-Forms could not be parsed: '%s'"), ibString::FromUTF8(rule));
			}
		}

		if (!rPluralFormsCalculator)
			rPluralFormsCalculator.reset(ibPluralFormsCalculator::make());
	}
	return true;
}

void ibMsgCatalogFile::FillHash(const ibEntry& entry, ibTranslationsHashMap& hash) const
{
	if (entry.m_fuzzy)
		return;   // a guess is no translation yet
	// The keys are gettext's: a context before the text and kContextSeparator between them, a plural form after it by
	// number.
	const ibString msgid = entry.m_hasContext ? ibString::FromUTF8(entry.m_context + kContextSeparator + entry.m_id)
		: ibString::FromUTF8(entry.m_id);
	for (size_t index = 0; index < entry.m_texts.size(); ++index) {
		const std::string& text = entry.m_texts[index];
		if (text.empty())
			continue;   // nobody translated it: the text is said as written
		const std::string& source = index == 0 || entry.m_idPlural.empty() ? entry.m_id : entry.m_idPlural;
		if (entry.m_cFormat && Conversions(text) != Conversions(source)) {
			ibJournalWarning(kJournalSource, ibT("%s: the translation of '%s' names other conversions than the text; it is not taken"),
				m_filename, ibString::FromUTF8(entry.m_id));
			continue;
		}
		hash[index == 0 ? msgid : msgid + ibChar(index)] = ibString::FromUTF8(text);
	}
}

} // namespace

// ============================================================================
// ibMsgCatalog - one loaded catalog
// ============================================================================

class ibMsgCatalog {
public:
	static std::unique_ptr<ibMsgCatalog> CreateFromFile(const ibString& filename, const ibString& domain)
	{
		std::unique_ptr<ibMsgCatalog> catalog(new ibMsgCatalog(domain));
		ibMsgCatalogFile file;
		if (!file.LoadFile(filename, catalog->m_pluralFormsCalculator, catalog->m_messages))
			return nullptr;
		return catalog;
	}

	const ibString& GetDomain() const { return m_domain; }

	// The translation, or null: `n` of kNoPlural asks for the singular entry, any other chooses the plural
	// form the catalog's rule gives it; the keys are gettext's, as the reader lays them out.
	static constexpr unsigned kNoPlural = UINT_MAX;
	const ibString* GetString(const ibString& text, unsigned n, const ibString& context) const
	{
		int index = 0;
		if (n != kNoPlural && m_pluralFormsCalculator)
			index = m_pluralFormsCalculator->evaluate(static_cast<int>(n));
		ibTranslationsHashMap::const_iterator found;
		if (index != 0) {
			if (context.IsEmpty())
				found = m_messages.find(ibString(text) + ibChar(index));
			else
				found = m_messages.find(ibString(context) + ibString(kContextSeparator) + ibString(text) + ibChar(index));
		}
		else {
			if (context.IsEmpty())
				found = m_messages.find(text);
			else
				found = m_messages.find(ibString(context) + ibString(kContextSeparator) + ibString(text));
		}
		return found != m_messages.end() ? &found->second : nullptr;
	}

private:
	explicit ibMsgCatalog(const ibString& domain) : m_domain(domain) {}

	ibTranslationsHashMap      m_messages;
	ibString                   m_domain;
	ibPluralFormsCalculatorPtr m_pluralFormsCalculator;
};

// ============================================================================
// ibDictionary - one language's catalogs, and the process's set of languages
// ============================================================================

namespace {

// THE PROCESS'S SET. Never destroyed: _() is asked during static destruction too (a registrar's teardown
// logs), and a registry that went first would be read after it was gone.
struct ibDictionaryRegistry {
	std::mutex                                    m_mutex;
	std::vector<std::unique_ptr<ibDictionary>>  m_languages;   // each made once, kept for the process
	std::vector<ibString>                         m_domains{ kEngineDomain };   // loaded for every language on its first use
	std::vector<ibString>                         m_prefixes;    // where <language>/<domain>.po is looked for
	bool                                          m_prefixesSettled = false;
	ibString                                      m_processLanguage;
	std::atomic<unsigned>                         m_processGeneration{ 0 };   // bumped by SetProcessLanguage: the threads' caches follow
	std::atomic<ibDictionary::Source>           m_source{ nullptr };
};

ibDictionaryRegistry& TheRegistry()
{
	static ibDictionaryRegistry* const registry = new ibDictionaryRegistry;
	return *registry;
}

// The folders the catalogs are looked for in, by default: the working folder's `lang`, the program folder's
// `lang` (programFolder.h), and - for a run from the build tree, where nothing copies the catalogs beside the
// binaries - the first `lang` found walking up from the executable. Under the registry's lock.
void SettleDefaultPrefixes(ibDictionaryRegistry& registry)
{
	if (registry.m_prefixesSettled)
		return;
	registry.m_prefixesSettled = true;
	const auto add = [&registry](const ibString& prefix) {
		for (const ibString& known : registry.m_prefixes)
			if (known == prefix)
				return;
		registry.m_prefixes.push_back(prefix);
	};
	add(ibGetCwd() + ibFILE_SEP_PATH + kCatalogFolder);
	add(ibProgramFolder() + ibFILE_SEP_PATH + kCatalogFolder);
	const ibString shipped = ibProgramSubfolder(kCatalogFolder);
	if (!shipped.IsEmpty())
		add(shipped);
}

// <prefix>/<language>/LC_MESSAGES/<domain>.po, then <prefix>/<language>/<domain>.po, over every prefix;
// empty when none of them is a file.
ibString CatalogPath(const std::vector<ibString>& prefixes, const ibString& language, const ibString& domain)
{
	for (const ibString& prefix : prefixes) {
		const ibString folder = prefix + ibFILE_SEP_PATH + language;
		const ibString inMessages = folder + ibFILE_SEP_PATH + kMessagesFolder + ibFILE_SEP_PATH + domain + kCatalogExtension;
		if (ibFileExists(inMessages))
			return inMessages;
		const ibString beside = folder + ibFILE_SEP_PATH + domain + kCatalogExtension;
		if (ibFileExists(beside))
			return beside;
	}
	return ibString();
}

// "uk_UA" is looked for as named, then as "uk": the catalogs are kept by language, and a region in the name
// is the process's way of saying it, not the translator's.
std::vector<ibString> LanguageCandidates(const ibString& language)
{
	std::vector<ibString> candidates{ language };
	ibString languageOnly = language;
	const size_t cut = languageOnly.find_first_of(ibT("_-"));
	if (cut != ibString::npos)
		languageOnly = languageOnly.Left(cut);
	if (!languageOnly.IsEmpty() && languageOnly != language)
		candidates.push_back(languageOnly);
	return candidates;
}

} // namespace

ibDictionary::ibDictionary(const ibString& language)
	: m_language(language)
{
}

ibDictionary::~ibDictionary() = default;

bool ibDictionary::LoadCatalogFrom(const std::vector<ibString>& prefixes, const ibString& domain)
{
	if (IsLoaded(domain))
		return true;
	for (const ibString& candidate : LanguageCandidates(m_language)) {
		const ibString path = CatalogPath(prefixes, candidate, domain);
		if (path.IsEmpty())
			continue;
		std::unique_ptr<ibMsgCatalog> catalog = ibMsgCatalog::CreateFromFile(path, domain);
		if (!catalog)
			return false;   // found and not readable: said by the reader; another folder's copy is not tried
		ibJournalInfo(kJournalSource, ibT("the catalog %s for %s is %s"), domain, m_language, path);
		m_catalogs.push_back(std::move(catalog));
		return true;
	}
	return false;
}

bool ibDictionary::IsLoaded(const ibString& domain) const
{
	for (const auto& catalog : m_catalogs)
		if (catalog->GetDomain() == domain)
			return true;
	return false;
}

const ibString* ibDictionary::GetTranslatedString(const ibString& text, const ibString& context) const
{
	return GetTranslatedString(text, ibMsgCatalog::kNoPlural, context);
}

const ibString* ibDictionary::GetTranslatedString(const ibString& text, unsigned n, const ibString& context) const
{
	if (text.IsEmpty())
		return nullptr;
	for (const auto& catalog : m_catalogs)
		if (const ibString* const translated = catalog->GetString(text, n, context))
			return translated;
	return nullptr;
}

void ibDictionary::AddCatalogLookupPathPrefix(const ibString& prefix)
{
	ibDictionaryRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	SettleDefaultPrefixes(registry);
	for (const ibString& known : registry.m_prefixes)
		if (known == prefix)
			return;
	// Ahead of the defaults, as wx orders them: what is added on purpose outranks what is found.
	registry.m_prefixes.insert(registry.m_prefixes.begin(), prefix);
}

void ibDictionary::AddDomain(const ibString& domain)
{
	ibDictionaryRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	for (const ibString& known : registry.m_domains)
		if (known == domain)
			return;
	registry.m_domains.push_back(domain);
	// A set made before is not changed under its readers: said, for whoever declared the domain late.
	for (const auto& language : registry.m_languages)
		ibJournalWarning(kJournalSource, ibT("the domain %s is declared after the language %s was made; that set does not have it"),
			domain, language->GetLanguage());
}

void ibDictionary::SetProcessLanguage(const ibString& language)
{
	ibDictionaryRegistry& registry = TheRegistry();
	const ibString canonical = CanonicalLanguage(language);
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	registry.m_processLanguage = canonical;
	registry.m_processGeneration.fetch_add(1, std::memory_order_release);
}

ibString ibDictionary::GetProcessLanguage()
{
	ibDictionaryRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	return registry.m_processLanguage;
}

ibDictionary::Source ibDictionary::SetSource(Source source)
{
	return TheRegistry().m_source.exchange(source);
}

ibString ibDictionary::CanonicalLanguage(const ibString& asked)
{
	// The codeset off ("uk_UA.UTF-8") and BCP 47's hyphen read as POSIX's underscore; then two or three letters
	// of the language, and two of the region if there is one.
	const ibString name = asked.BeforeFirst(ibT('.'));
	const size_t cut = name.find_first_of(ibT("_-"));
	const ibString language = cut != ibString::npos ? name.Left(cut) : name;
	const ibString region = cut != ibString::npos ? name.Mid(cut + 1) : ibString();
	const auto letters = [](const ibString& part) {
		for (const wchar_t c : part)
			if (!((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')))
				return false;
		return true;
	};
	if (language.length() < 2 || language.length() > 3 || !letters(language))
		return ibString();
	if (cut != ibString::npos && (region.length() != 2 || !letters(region)))
		return ibString();
	return region.IsEmpty() ? language.Lower() : language.Lower() + ibT("_") + region.Upper();
}

const ibDictionary* ibDictionary::ForLanguage(const ibString& asked)
{
	// One set per language however it is spelled: "uk-UA" and "uk_UA.UTF-8" find the set uk_UA made.
	const ibString language = CanonicalLanguage(asked);
	if (language.IsEmpty())
		return nullptr;
	ibDictionaryRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	for (const auto& known : registry.m_languages)
		if (known->GetLanguage() == language)
			return known.get();
	// Made and loaded here, under the lock, once per language: the loading reads a file or two.
	SettleDefaultPrefixes(registry);
	std::unique_ptr<ibDictionary> made(new ibDictionary(language));
	for (const ibString& domain : registry.m_domains)
		made->LoadCatalogFrom(registry.m_prefixes, domain);
	return registry.m_languages.emplace_back(std::move(made)).get();
}

const ibDictionary* ibDictionary::ForThisThread()
{
	ibDictionaryRegistry& registry = TheRegistry();
	// The source's set, else the process's - the latter kept per thread by the process's generation. Both
	// are kept in what is trivially destroyed: _() is said from static destructors, after a thread's own
	// objects are gone.
	if (const Source source = registry.m_source.load(std::memory_order_acquire))
		if (const ibDictionary* const set = source())
			return set;
	thread_local unsigned t_processGeneration = ~0u;
	thread_local const ibDictionary* t_processSet = nullptr;
	const unsigned generation = registry.m_processGeneration.load(std::memory_order_acquire);
	if (t_processGeneration != generation) {
		t_processSet = ForLanguage(GetProcessLanguage());
		t_processGeneration = generation;
	}
	return t_processSet;
}

ibString ibGetTranslation(const ibString& text, const ibString& context)
{
	if (text.IsEmpty())
		return text;
	const ibDictionary* const set = ibDictionary::ForThisThread();
	if (set == nullptr)
		return text;
	const ibString* const translated = set->GetTranslatedString(text, context);
	return translated != nullptr ? *translated : text;
}

ibString ibGetTranslation(const ibDictionary* dictionary, const ibString& text, const ibString& context)
{
	if (dictionary == nullptr)
		return ibGetTranslation(text, context);
	const ibString* const translated = text.IsEmpty() ? nullptr : dictionary->GetTranslatedString(text, context);
	return translated != nullptr ? *translated : text;
}

ibString ibGetTranslation(const ibString& one, const ibString& many, int n, const ibString& context)
{
	const ibDictionary* const set = ibDictionary::ForThisThread();
	if (set != nullptr && n >= 0)
		if (const ibString* const translated = set->GetTranslatedString(one, static_cast<unsigned>(n), context))
			return *translated;
	return n == 1 ? one : many;
}
