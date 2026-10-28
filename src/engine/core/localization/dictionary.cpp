///////////////////////////////////////////////////////////////////////////
// Name:        core/localization/dictionary.cpp
// Purpose:     the engine's own message catalogs - gettext .mo files, one set per language, chosen per thread
// Author:      the .mo reader and the plural-forms calculator are wxWidgets' (src/common/translation.cpp:
//              Vadim Zeitlin, Vaclav Slavik, Michael N. Filippov), renamed; the sets and the choice are ours
// Copyright:   (c) 1998 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr> for the parts taken from wxWidgets
// Licence:     wxWindows licence for those parts - NOTICE.md
///////////////////////////////////////////////////////////////////////////

#include "core/localization/dictionary.h"

#include "core/diagnostics/journal.h"
#include "core/fileSystem.h"   // ibProgramFolder, ibGetCwd, ibExecutablePath, ibPathOf — where the catalogs are

#include <atomic>
#include <cctype>
#include <climits>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

typedef std::uint32_t size_t32;

// magic number identifying the .mo format file
const size_t32 MSGCATALOG_MAGIC    = 0x950412de;
const size_t32 MSGCATALOG_MAGIC_SW = 0xde120495;

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

// The length of a string inside a catalog, never past `max` — a corrupt catalog's string may have no NUL in it.
size_t Strnlen(const char* s, size_t max)
{
    const void* const nul = std::memchr(s, '\0', max);
    return nul != nullptr ? static_cast<const char*>(nul) - s : max;
}

// ============================================================================
// ibMsgCatalogFile - the .mo file itself: wxWidgets' reader (src/common/translation.cpp), renamed.
// ============================================================================
class ibMsgCatalogFile
{
public:
    typedef std::string DataBuffer;

    // ctor & dtor
    ibMsgCatalogFile();
    ~ibMsgCatalogFile();

    // load the catalog from disk
    bool LoadFile(const ibString& filename,
                  ibPluralFormsCalculatorPtr& rPluralFormsCalculator);
    bool LoadData(const DataBuffer& data,
                  ibPluralFormsCalculatorPtr& rPluralFormsCalculator);

    // fills the hash with string-translation pairs
    bool FillHash(ibTranslationsHashMap& hash) const;

    // return the charset of the strings in this catalog or empty string if
    // none/unknown
    ibString GetCharset() const { return m_charset; }

private:
    // this implementation is binary compatible with GNU gettext() version 0.10

    // an entry in the string table
    struct ibMsgTableEntry
    {
        size_t32   nLen;           // length of the string
        size_t32   ofsString;      // pointer to the string
    };

    // header of a .mo file
    struct ibMsgCatalogHeader
    {
        size_t32  magic,          // offset +00:  magic id
                  revision,       //        +04:  revision
                  numStrings;     //        +08:  number of strings in the file
        size_t32  ofsOrigTable,   //        +0C:  start of original string table
                  ofsTransTable;  //        +10:  start of translated string table
        size_t32  nHashSize,      //        +14:  hash table size
                  ofsHashTable;   //        +18:  offset of hash table start
    };

    // all data is stored here
    DataBuffer m_data;

    // data description
    size_t32          m_numStrings;   // number of strings in this domain
    const
    ibMsgTableEntry  *m_pOrigTable,   // pointer to original   strings
                     *m_pTransTable;  //            translated

    ibString m_charset;               // from the message catalog header


    // swap the 2 halves of 32 bit integer if needed
    size_t32 Swap(size_t32 ui) const
    {
        return m_bSwapped ? (ui << 24) | ((ui & 0xff00) << 8) |
                            ((ui >> 8) & 0xff00) | (ui >> 24)
                            : ui;
    }

    const char* StringAtOfs(const ibMsgTableEntry* pTable, size_t32 n) const
    {
        const ibMsgTableEntry * const ent = pTable + n;

        // this check could fail for a corrupt message catalog
        size_t32 ofsString = Swap(ent->ofsString);
        if ( ofsString + Swap(ent->nLen) > m_data.length())
        {
            return nullptr;
        }

        return m_data.data() + ofsString;
    }

    bool m_bSwapped;   // wrong endianness?

    ibMsgCatalogFile(const ibMsgCatalogFile&) = delete;
    ibMsgCatalogFile& operator=(const ibMsgCatalogFile&) = delete;
};

// ----------------------------------------------------------------------------
// ibMsgCatalogFile class
// ----------------------------------------------------------------------------

ibMsgCatalogFile::ibMsgCatalogFile()
{
}

ibMsgCatalogFile::~ibMsgCatalogFile()
{
}

// open disk file and read in its contents
bool ibMsgCatalogFile::LoadFile(const ibString& filename,
                                ibPluralFormsCalculatorPtr& rPluralFormsCalculator)
{
    // the whole file in memory
    std::ifstream fileMsg(ibPathOf(filename), std::ios::binary);
    if ( !fileMsg.is_open() )
        return false;
    DataBuffer filedata((std::istreambuf_iterator<char>(fileMsg)), std::istreambuf_iterator<char>());
    if ( fileMsg.bad() )
        return false;

    bool ok = LoadData(filedata, rPluralFormsCalculator);
    if ( !ok )
    {
        ibJournalWarning(ibT("i18n"), ibT("'%s' is not a valid message catalog"), filename);
        return false;
    }

    return true;
}


bool ibMsgCatalogFile::LoadData(const DataBuffer& data,
                                ibPluralFormsCalculatorPtr& rPluralFormsCalculator)
{
    // examine header
    bool bValid = data.length() > sizeof(ibMsgCatalogHeader);

    const ibMsgCatalogHeader* pHeader = reinterpret_cast<const ibMsgCatalogHeader*>(data.data());
    if ( bValid ) {
        // we'll have to swap all the integers if it's true
        m_bSwapped = pHeader->magic == MSGCATALOG_MAGIC_SW;

        // check the magic number
        bValid = m_bSwapped || pHeader->magic == MSGCATALOG_MAGIC;
    }

    if ( !bValid ) {
        // it's either too short or has incorrect magic number
        ibJournalWarning(ibT("i18n"), ibT("a message catalog is not a valid one"));
        return false;
    }

    // The tables point into OUR copy: wx's buffer was shared, a std::string is copied, and the caller's goes
    // with LoadFile's return - read through it, every catalog came out empty (2026-10-27).
    m_data = data;

    // initialize
    m_numStrings  = Swap(pHeader->numStrings);
    m_pOrigTable  = reinterpret_cast<const ibMsgTableEntry*>(m_data.data() +
                    Swap(pHeader->ofsOrigTable));
    m_pTransTable = reinterpret_cast<const ibMsgTableEntry*>(m_data.data() +
                    Swap(pHeader->ofsTransTable));

    // now parse catalog's header and try to extract catalog charset and
    // plural forms formula from it:

    const char* headerData = StringAtOfs(m_pOrigTable, 0);
    if ( headerData && headerData[0] == '\0' )
    {
        // Extract the charset:
        const char * const header = StringAtOfs(m_pTransTable, 0);
        const char *
            cset = strstr(header, "Content-Type: text/plain; charset=");
        if ( cset )
        {
            cset += 34; // strlen("Content-Type: text/plain; charset=")

            const char * const csetEnd = strchr(cset, '\n');
            if ( csetEnd )
            {
                m_charset = ibString::FromUTF8(cset, csetEnd - cset);
                if ( m_charset == ibT("CHARSET") )
                {
                    // "CHARSET" is not valid charset, but lazy translator
                    m_charset.clear();
                }
            }
        }
        // else: incorrectly filled Content-Type header

        // Extract plural forms:
        const char * plurals = strstr(header, "Plural-Forms:");
        if ( plurals )
        {
            plurals += 13; // strlen("Plural-Forms:")
            const char * const pluralsEnd = strchr(plurals, '\n');
            if ( pluralsEnd )
            {
                const size_t pluralsLen = pluralsEnd - plurals;
                const std::string buf(plurals, pluralsLen);
                ibPluralFormsCalculator * const
                    pCalculator = ibPluralFormsCalculator::make(buf.c_str());
                if ( pCalculator )
                {
                    rPluralFormsCalculator.reset(pCalculator);
                }
                else
                {
                    ibJournalWarning(ibT("i18n"), ibT("Plural-Forms could not be parsed: '%s'"), ibString::FromUTF8(buf));
                }
            }
        }

        if ( !rPluralFormsCalculator.get() )
            rPluralFormsCalculator.reset(ibPluralFormsCalculator::make());
    }

    // everything is fine
    return true;
}

bool ibMsgCatalogFile::FillHash(ibTranslationsHashMap& hash) const
{
    // The engine's catalogs are UTF-8, and so is every one msgfmt writes from a .po that says so; a catalog that
    // names another charset is read as UTF-8 too, and the journal says so once.
    if ( !m_charset.empty() && m_charset.CmpNoCase(ibT("UTF-8")) != 0 )
        ibJournalWarning(ibT("i18n"), ibT("a message catalog says it is in %s; it is read as UTF-8"), m_charset);

    for (size_t32 i = 0; i < m_numStrings; i++)
    {
        const char *data = StringAtOfs(m_pOrigTable, i);
        if (!data)
            return false; // may happen for invalid MO files

        const ibString msgid = ibString::FromUTF8(data);
        data = StringAtOfs(m_pTransTable, i);
        if (!data)
            return false; // may happen for invalid MO files

        size_t length = Swap(m_pTransTable[i].nLen);
        size_t offset = 0;
        size_t index = 0;
        while (offset < length)
        {
            const char * const str = data + offset;

            const size_t strLength = Strnlen(str, length - offset);
            const ibString msgstr = ibString::FromUTF8(str, strLength);
            if ( !msgstr.empty() )
            {
                hash[index == 0 ? msgid : msgid + ibChar(index)] = msgstr;
            }

            // skip this string
            // IMPORTANT: accesses to the 'data' pointer are valid only for
            //            the first 'length+1' bytes (GNU specs says that the
            //            final NUL is not counted in length); using Strnlen()
            //            we make sure we don't access memory beyond the valid range
            //            (which otherwise may happen for invalid MO files):
            offset += strLength + 1;
            ++index;
        }
    }

    return true;
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
		if (!file.LoadFile(filename, catalog->m_pluralFormsCalculator))
			return nullptr;
		if (!file.FillHash(catalog->m_messages)) {
			ibJournalWarning(ibT("i18n"), ibT("'%s' is not a valid message catalog"), filename);
			return nullptr;
		}
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
				found = m_messages.find(ibString(context) + ibString('\x04') + ibString(text) + ibChar(index));
		}
		else {
			if (context.IsEmpty())
				found = m_messages.find(text);
			else
				found = m_messages.find(ibString(context) + ibString('\x04') + ibString(text));
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
	std::vector<ibString>                         m_prefixes;    // where <language>/<domain>.mo is looked for
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
	add(ibGetCwd() + ibFILE_SEP_PATH + ibT("lang"));
	add(ibProgramFolder() + ibFILE_SEP_PATH + ibT("lang"));
	std::filesystem::path walk = ibPathOf(ibExecutablePath()).parent_path();
	for (int level = 0; level < 6 && !walk.empty(); ++level) {
		std::error_code error;
		const std::filesystem::path candidate = walk / "lang";
		if (std::filesystem::is_directory(candidate, error)) {
			add(ibString(candidate.wstring()));
			break;
		}
		if (walk == walk.parent_path())
			break;
		walk = walk.parent_path();
	}
}

// <prefix>/<language>/LC_MESSAGES/<domain>.mo, then <prefix>/<language>/<domain>.mo, over every prefix;
// empty when none of them is a file.
ibString CatalogPath(const std::vector<ibString>& prefixes, const ibString& language, const ibString& domain)
{
	for (const ibString& prefix : prefixes) {
		const ibString folder = prefix + ibFILE_SEP_PATH + language;
		const ibString inMessages = folder + ibFILE_SEP_PATH + ibT("LC_MESSAGES") + ibFILE_SEP_PATH + domain + ibT(".mo");
		if (ibFileExists(inMessages))
			return inMessages;
		const ibString beside = folder + ibFILE_SEP_PATH + domain + ibT(".mo");
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
		ibJournalInfo(ibT("i18n"), ibT("the catalog %s for %s is %s"), domain, m_language, path);
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
		ibJournalWarning(ibT("i18n"), ibT("the domain %s is declared after the language %s was made; that set does not have it"),
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
	registry.m_languages.push_back(std::move(made));
	return registry.m_languages.back().get();
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
