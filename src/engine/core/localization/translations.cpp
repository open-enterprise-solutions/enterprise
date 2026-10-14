///////////////////////////////////////////////////////////////////////////
// Name:        core/localization/translations.cpp
// Purpose:     the engine's own message catalogs - gettext .mo files, one set per language, chosen per thread
// Author:      the .mo reader and the plural-forms calculator are wxWidgets' (src/common/translation.cpp:
//              Vadim Zeitlin, Vaclav Slavik, Michael N. Filippov), renamed; the sets and the choice are ours
// Copyright:   (c) 1998 Vadim Zeitlin <zeitlin@dptmaths.ens-cachan.fr> for the parts taken from wxWidgets
// Licence:     wxWindows licence for those parts - NOTICE.md
///////////////////////////////////////////////////////////////////////////

#include "core/localization/translations.h"

#include "core/diagnostics/journal.h"
#include "core/programFolder.h"

#include <wx/buffer.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/filefn.h>
#include <wx/hashmap.h>
#include <wx/stdpaths.h>
#include <wx/strconv.h>
#include <wx/uilocale.h>
#include <wx/wxcrt.h>

#include <atomic>
#include <cctype>
#include <climits>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

typedef wxUint32 size_t32;

// magic number identifying the .mo format file
const size_t32 MSGCATALOG_MAGIC    = 0x950412de;
const size_t32 MSGCATALOG_MAGIC_SW = 0xde120495;

typedef std::unordered_map<wxString, wxString, wxStringHash, wxStringEqual> ibTranslationsHashMap;

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

// ============================================================================
// ibMsgCatalogFile - the .mo file itself: wxWidgets' reader (src/common/translation.cpp), renamed.
// ============================================================================
class ibMsgCatalogFile
{
public:
    typedef wxScopedCharBuffer DataBuffer;

    // ctor & dtor
    ibMsgCatalogFile();
    ~ibMsgCatalogFile();

    // load the catalog from disk
    bool LoadFile(const wxString& filename,
                  ibPluralFormsCalculatorPtr& rPluralFormsCalculator);
    bool LoadData(const DataBuffer& data,
                  ibPluralFormsCalculatorPtr& rPluralFormsCalculator);

    // fills the hash with string-translation pairs
    bool FillHash(ibTranslationsHashMap& hash, const wxString& domain) const;

    // return the charset of the strings in this catalog or empty string if
    // none/unknown
    wxString GetCharset() const { return m_charset; }

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

    wxString m_charset;               // from the message catalog header


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

    wxDECLARE_NO_COPY_CLASS(ibMsgCatalogFile);
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
bool ibMsgCatalogFile::LoadFile(const wxString& filename,
                                ibPluralFormsCalculatorPtr& rPluralFormsCalculator)
{
    wxFile fileMsg(filename);
    if ( !fileMsg.IsOpened() )
        return false;

    // get the file size (assume it is less than 4GB...)
    wxFileOffset lenFile = fileMsg.Length();
    if ( lenFile == wxInvalidOffset )
        return false;

    size_t nSize = wx_truncate_cast(size_t, lenFile);
    wxASSERT_MSG( nSize == lenFile + size_t(0), wxS("message catalog bigger than 4GB?") );

    wxMemoryBuffer filedata;

    // read the whole file in memory
    if ( fileMsg.Read(filedata.GetWriteBuf(nSize), nSize) != lenFile )
        return false;

    filedata.UngetWriteBuf(nSize);

    bool ok = LoadData
              (
                  DataBuffer::CreateOwned((char*)filedata.release(), nSize),
                  rPluralFormsCalculator
              );
    if ( !ok )
    {
        ibJournalWarning(wxT("i18n"), wxT("'%s' is not a valid message catalog"), filename);
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
        ibJournalWarning(wxT("i18n"), wxT("a message catalog is not a valid one"));
        return false;
    }

    m_data = data;

    // initialize
    m_numStrings  = Swap(pHeader->numStrings);
    m_pOrigTable  = reinterpret_cast<const ibMsgTableEntry*>(data.data() +
                    Swap(pHeader->ofsOrigTable));
    m_pTransTable = reinterpret_cast<const ibMsgTableEntry*>(data.data() +
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
                m_charset = wxString(cset, csetEnd - cset);
                if ( m_charset == wxS("CHARSET") )
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
                wxCharBuffer buf(pluralsLen);
                strncpy(buf.data(), plurals, pluralsLen);
                ibPluralFormsCalculator * const
                    pCalculator = ibPluralFormsCalculator::make(buf);
                if ( pCalculator )
                {
                    rPluralFormsCalculator.reset(pCalculator);
                }
                else
                {
                    ibJournalWarning(wxT("i18n"), wxT("Plural-Forms could not be parsed: '%s'"), wxString(buf.data()));
                }
            }
        }

        if ( !rPluralFormsCalculator.get() )
            rPluralFormsCalculator.reset(ibPluralFormsCalculator::make());
    }

    // everything is fine
    return true;
}

bool ibMsgCatalogFile::FillHash(ibTranslationsHashMap& hash,
                                const wxString& domain) const
{
    wxUnusedVar(domain); // silence warning in Unicode build

    // conversion to use to convert catalog strings to the GUI encoding
    wxMBConv *inputConv = nullptr;

    std::unique_ptr<wxMBConv> inputConvPtr; // just to delete inputConv if needed

    if ( !m_charset.empty() )
    {
        inputConv = new wxCSConv(m_charset);

        // As we allocated it ourselves, we need to delete it, so ensure
        // this happens.
        inputConvPtr.reset(inputConv);
    }
    else // no need to convert the encoding
    {
        // we must somehow convert the narrow strings in the message catalog to
        // wide strings, so use the default conversion if we have no charset
        inputConv = &wxConvUTF8;   // a catalog that names no charset is UTF-8, as the engine's are
    }

    for (size_t32 i = 0; i < m_numStrings; i++)
    {
        const char *data = StringAtOfs(m_pOrigTable, i);
        if (!data)
            return false; // may happen for invalid MO files

        wxString msgid;
        msgid = wxString(data, *inputConv);
        data = StringAtOfs(m_pTransTable, i);
        if (!data)
            return false; // may happen for invalid MO files

        size_t length = Swap(m_pTransTable[i].nLen);
        size_t offset = 0;
        size_t index = 0;
        while (offset < length)
        {
            const char * const str = data + offset;

            wxString msgstr;
            msgstr = wxString(str, *inputConv);
            if ( !msgstr.empty() )
            {
                hash[index == 0 ? msgid : msgid + wxChar(index)] = msgstr;
            }

            // skip this string
            // IMPORTANT: accesses to the 'data' pointer are valid only for
            //            the first 'length+1' bytes (GNU specs says that the
            //            final NUL is not counted in length); using wxStrnlen()
            //            we make sure we don't access memory beyond the valid range
            //            (which otherwise may happen for invalid MO files):
            offset += wxStrnlen(str, length - offset) + 1;
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
	static std::unique_ptr<ibMsgCatalog> CreateFromFile(const wxString& filename, const wxString& domain)
	{
		std::unique_ptr<ibMsgCatalog> catalog(new ibMsgCatalog(domain));
		ibMsgCatalogFile file;
		if (!file.LoadFile(filename, catalog->m_pluralFormsCalculator))
			return nullptr;
		if (!file.FillHash(catalog->m_messages, domain))
			return nullptr;
		return catalog;
	}

	const wxString& GetDomain() const { return m_domain; }

	// The translation, or null: `n` of kNoPlural asks for the singular entry, any other chooses the plural
	// form the catalog's rule gives it; the keys are gettext's, as the reader lays them out.
	static constexpr unsigned kNoPlural = UINT_MAX;
	const wxString* GetString(const wxString& text, unsigned n, const wxString& context) const
	{
		int index = 0;
		if (n != kNoPlural && m_pluralFormsCalculator)
			index = m_pluralFormsCalculator->evaluate(static_cast<int>(n));
		ibTranslationsHashMap::const_iterator found;
		if (index != 0) {
			if (context.IsEmpty())
				found = m_messages.find(wxString(text) + wxChar(index));
			else
				found = m_messages.find(wxString(context) + wxString('\x04') + wxString(text) + wxChar(index));
		}
		else {
			if (context.IsEmpty())
				found = m_messages.find(text);
			else
				found = m_messages.find(wxString(context) + wxString('\x04') + wxString(text));
		}
		return found != m_messages.end() ? &found->second : nullptr;
	}

private:
	explicit ibMsgCatalog(const wxString& domain) : m_domain(domain) {}

	ibTranslationsHashMap      m_messages;
	wxString                   m_domain;
	ibPluralFormsCalculatorPtr m_pluralFormsCalculator;
};

// ============================================================================
// ibTranslations - one language's catalogs, and the process's set of languages
// ============================================================================

namespace {

// THE PROCESS'S SET. Never destroyed: _() is asked during static destruction too (a registrar's teardown
// logs), and a registry that went first would be read after it was gone.
struct ibTranslationRegistry {
	std::mutex                                    m_mutex;
	std::vector<std::unique_ptr<ibTranslations>>  m_languages;   // each made once, kept for the process
	std::vector<wxString>                         m_domains;     // loaded for every language on its first use
	std::vector<wxString>                         m_prefixes;    // where <language>/<domain>.mo is looked for
	bool                                          m_prefixesSettled = false;
	wxString                                      m_processLanguage;
	std::atomic<unsigned>                         m_processGeneration{ 0 };   // bumped by SetProcessLanguage: the threads' caches follow
	std::atomic<ibTranslations::Source>           m_source{ nullptr };
};

ibTranslationRegistry& TheRegistry()
{
	static ibTranslationRegistry* const registry = new ibTranslationRegistry;
	return *registry;
}

// The folders the catalogs are looked for in, by default: the working folder's `lang`, the program folder's
// `lang` (programFolder.h), and - for a run from the build tree, where nothing copies the catalogs beside the
// binaries - the first `lang` found walking up from the executable. Under the registry's lock.
void SettleDefaultPrefixes(ibTranslationRegistry& registry)
{
	if (registry.m_prefixesSettled)
		return;
	registry.m_prefixesSettled = true;
	const auto add = [&registry](const wxString& prefix) {
		for (const wxString& known : registry.m_prefixes)
			if (known == prefix)
				return;
		registry.m_prefixes.push_back(prefix);
	};
	add(wxGetCwd() + wxFILE_SEP_PATH + wxT("lang"));
	add(ibProgramFolder() + wxFILE_SEP_PATH + wxT("lang"));
	wxFileName walk(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath(), wxEmptyString);
	for (int level = 0; level < 6; ++level) {
		const wxString candidate = walk.GetPath() + wxFILE_SEP_PATH + wxT("lang");
		if (wxFileName::DirExists(candidate)) {
			add(candidate);
			break;
		}
		if (walk.GetDirCount() == 0)
			break;
		walk.RemoveLastDir();
	}
}

// <prefix>/<language>/LC_MESSAGES/<domain>.mo, then <prefix>/<language>/<domain>.mo, over every prefix;
// empty when none of them is a file.
wxString CatalogPath(const std::vector<wxString>& prefixes, const wxString& language, const wxString& domain)
{
	for (const wxString& prefix : prefixes) {
		const wxString folder = prefix + wxFILE_SEP_PATH + language;
		const wxString inMessages = folder + wxFILE_SEP_PATH + wxT("LC_MESSAGES") + wxFILE_SEP_PATH + domain + wxT(".mo");
		if (wxFileName::FileExists(inMessages))
			return inMessages;
		const wxString beside = folder + wxFILE_SEP_PATH + domain + wxT(".mo");
		if (wxFileName::FileExists(beside))
			return beside;
	}
	return wxString();
}

// "uk_UA" is looked for as named, then as "uk": the catalogs are kept by language, and a region in the name
// is the process's way of saying it, not the translator's.
std::vector<wxString> LanguageCandidates(const wxString& language)
{
	std::vector<wxString> candidates{ language };
	wxString languageOnly = language;
	const size_t cut = languageOnly.find_first_of(wxT("_-"));
	if (cut != wxString::npos)
		languageOnly = languageOnly.Left(cut);
	if (!languageOnly.IsEmpty() && languageOnly != language)
		candidates.push_back(languageOnly);
	return candidates;
}

} // namespace

ibTranslations::ibTranslations(const wxString& language)
	: m_language(language)
{
}

ibTranslations::~ibTranslations() = default;

bool ibTranslations::LoadCatalogFrom(const std::vector<wxString>& prefixes, const wxString& domain)
{
	if (IsLoaded(domain))
		return true;
	for (const wxString& candidate : LanguageCandidates(m_language)) {
		const wxString path = CatalogPath(prefixes, candidate, domain);
		if (path.IsEmpty())
			continue;
		std::unique_ptr<ibMsgCatalog> catalog = ibMsgCatalog::CreateFromFile(path, domain);
		if (!catalog)
			return false;   // found and not readable: said by the reader; another folder's copy is not tried
		ibJournalInfo(wxT("i18n"), wxT("the catalog %s for %s is %s"), domain, m_language, path);
		m_catalogs.push_back(std::move(catalog));
		return true;
	}
	return false;
}

bool ibTranslations::IsLoaded(const wxString& domain) const
{
	for (const auto& catalog : m_catalogs)
		if (catalog->GetDomain() == domain)
			return true;
	return false;
}

const wxString* ibTranslations::GetTranslatedString(const wxString& text, const wxString& context) const
{
	return GetTranslatedString(text, ibMsgCatalog::kNoPlural, context);
}

const wxString* ibTranslations::GetTranslatedString(const wxString& text, unsigned n, const wxString& context) const
{
	if (text.IsEmpty())
		return nullptr;
	for (const auto& catalog : m_catalogs)
		if (const wxString* const translated = catalog->GetString(text, n, context))
			return translated;
	return nullptr;
}

void ibTranslations::AddCatalogLookupPathPrefix(const wxString& prefix)
{
	ibTranslationRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	SettleDefaultPrefixes(registry);
	for (const wxString& known : registry.m_prefixes)
		if (known == prefix)
			return;
	// Ahead of the defaults, as wx orders them: what is added on purpose outranks what is found.
	registry.m_prefixes.insert(registry.m_prefixes.begin(), prefix);
}

void ibTranslations::AddDomain(const wxString& domain)
{
	ibTranslationRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	for (const wxString& known : registry.m_domains)
		if (known == domain)
			return;
	registry.m_domains.push_back(domain);
	// A set made before is not changed under its readers: said, for whoever declared the domain late.
	for (const auto& language : registry.m_languages)
		ibJournalWarning(wxT("i18n"), wxT("the domain %s is declared after the language %s was made; that set does not have it"),
			domain, language->GetLanguage());
}

void ibTranslations::SetProcessLanguage(const wxString& language)
{
	ibTranslationRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	registry.m_processLanguage = language;
	registry.m_processGeneration.fetch_add(1, std::memory_order_release);
}

wxString ibTranslations::GetProcessLanguage()
{
	ibTranslationRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	return registry.m_processLanguage;
}

ibTranslations::Source ibTranslations::SetSource(Source source)
{
	return TheRegistry().m_source.exchange(source);
}

wxString ibTranslations::CanonicalLanguage(const wxString& asked)
{
	const wxLanguageInfo* info = nullptr;
	if (asked.IsEmpty())
		info = wxUILocale::GetLanguageInfo(wxUILocale::GetSystemLanguage());
	else {
		// wx's lookup knows the underscore and the English name, not BCP 47's hyphen nor a codeset.
		wxString name = asked.BeforeFirst(wxT('.'));
		name.Replace(wxT("-"), wxT("_"));
		info = wxUILocale::FindLanguageInfo(name);
	}
	return info != nullptr ? info->GetCanonicalWithRegion() : wxString();
}

const ibTranslations* ibTranslations::ForLanguage(const wxString& language)
{
	if (language.IsEmpty())
		return nullptr;
	ibTranslationRegistry& registry = TheRegistry();
	std::lock_guard<std::mutex> lock(registry.m_mutex);
	for (const auto& known : registry.m_languages)
		if (known->GetLanguage() == language)
			return known.get();
	// Made and loaded here, under the lock, once per language: the loading reads a file or two.
	SettleDefaultPrefixes(registry);
	std::unique_ptr<ibTranslations> made(new ibTranslations(language));
	for (const wxString& domain : registry.m_domains)
		made->LoadCatalogFrom(registry.m_prefixes, domain);
	registry.m_languages.push_back(std::move(made));
	return registry.m_languages.back().get();
}

const ibTranslations* ibTranslations::ForThisThread()
{
	ibTranslationRegistry& registry = TheRegistry();
	// The source's set, else the process's - the latter kept per thread by the process's generation. Both
	// are kept in what is trivially destroyed: _() is said from static destructors, after a thread's own
	// objects are gone.
	if (const Source source = registry.m_source.load(std::memory_order_acquire))
		if (const ibTranslations* const set = source())
			return set;
	thread_local unsigned t_processGeneration = ~0u;
	thread_local const ibTranslations* t_processSet = nullptr;
	const unsigned generation = registry.m_processGeneration.load(std::memory_order_acquire);
	if (t_processGeneration != generation) {
		t_processSet = ForLanguage(GetProcessLanguage());
		t_processGeneration = generation;
	}
	return t_processSet;
}

wxString ibTranslations::Translate(const wxString& text)
{
	return Translate(text, wxString());
}

wxString ibTranslations::Translate(const wxString& text, const wxString& context)
{
	if (text.IsEmpty())
		return text;
	const ibTranslations* const set = ForThisThread();
	if (set == nullptr)
		return text;
	const wxString* const translated = set->GetTranslatedString(text, context);
	return translated != nullptr ? *translated : text;
}

wxString ibTranslations::TranslatePlural(const wxString& one, const wxString& many, int n, const wxString& context)
{
	const ibTranslations* const set = ForThisThread();
	if (set != nullptr && n >= 0)
		if (const wxString* const translated = set->GetTranslatedString(one, static_cast<unsigned>(n), context))
			return *translated;
	return n == 1 ? one : many;
}
