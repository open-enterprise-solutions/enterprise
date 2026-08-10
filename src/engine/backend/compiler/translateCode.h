#ifndef __TRANSLATE_CODE_H__
#define __TRANSLATE_CODE_H__

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <execution>
#include <map>
#include <vector>
#include <wctype.h>

#include "backend/backend_exception.h"

#include "codeDef.h"
#include "value.h"

// Ordering for the lexer's name tables — keywords and #Define alike. Both are
// matched case-insensitively, and both are consulted for EVERY identifier the
// lexer meets, so the folding belongs in the comparator: the alternative is
// upper-casing the query into a throw-away wxString once per token, per table.
struct ibCaseFoldLess {
	bool operator()(const wxString& lhs, const wxString& rhs) const noexcept {
		auto itLhs = lhs.begin(), itRhs = rhs.begin();
		for (; itLhs != lhs.end() && itRhs != rhs.end(); ++itLhs, ++itRhs) {
			const wxUint32 chLhs = ::towupper((*itLhs).GetValue());
			const wxUint32 chRhs = ::towupper((*itRhs).GetValue());
			if (chLhs != chRhs) return chLhs < chRhs;
		}
		return itRhs != rhs.end();//lhs ran out first, so it is the shorter one
	}
};

//List of keywords
struct ibKeyWords {
	wxString m_strKeyWord;
	wxString m_strShortDescription;
};

extern BACKEND_API struct ibKeyWords s_listKeyWord[];

enum {
	LEXEM_ADD = 0,
	LEXEM_ADDDEF,
	LEXEM_IGNORE,
};

//definitions
#define UTF8_LEXEM_TRANSLATE 

class BACKEND_API ibTranslateCode;

//storing one primitive from the source code
struct ibLexem {

	//lexem type:
	short m_lexType;

	//lexem content:
	short m_numData;			// keyword number (KEYWORD) or delimiter symbol (DELIMITER)
	wxString m_strData;			// or identifier name (variable, function, etc.)
	ibValue m_valData;			// value, if it is a constant or real identifier name

	// Per-lexem source attribution lives on the owning ibTranslateCode —
	// every lexem of one module shares the same module name / doc-path /
	// file name. Storing them per-lexem (as 3 wxStrings) was a measurable
	// hit during retokenization: 4 wxString members × N lexems per
	// reserve/_Reallocate amplified the modify-event handler cost.
	// m_translateCode must outlive the lexem; for #define lexems stored
	// in ibDefineCollection it is reset to nullptr (string accessors
	// return empty) and rebound by the consumer at expansion time.
	const ibTranslateCode* m_translateCode = nullptr;

	unsigned int m_numLine;		//source line number (for breakpoints)
	unsigned int m_numString;	//source text number (for error output)

#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int m_numUtf8String; //source text number
#endif

public:

	const wxString& GetModuleName() const;
	const wxString& GetDocPath()    const;
	const wxString& GetFileName()   const;

	unsigned int GetLine() const { return m_numLine + 1; }
	unsigned int GetLength() const {
		if (m_lexType == DELIMITER)
			return 1;
		else if (m_lexType == IDENTIFIER)
			return m_strData.length();
		else if (m_lexType == CONSTANT && m_valData.GetType() == ibValueTypes::TYPE_DATE)
			return m_strData.length() + 2;
		else if (m_lexType == CONSTANT && m_valData.GetType() == ibValueTypes::TYPE_STRING)
			return m_strData.length() + 2;
		else if (m_lexType == CONSTANT)
			return m_strData.length();
		else if (m_lexType == KEYWORD)
			return m_strData.length();
		return 0;
	}

	unsigned int StartPos() const { return m_numString; }
	unsigned int EndPos() const { return m_numString + GetLength(); }

	//Constructor:
	ibLexem() :
		m_lexType(0),
		m_numData(0),
		m_numLine(0),
#ifdef UTF8_LEXEM_TRANSLATE
		m_numString(0),
		m_numUtf8String(0)
#else
		m_numString(0)
#endif
	{
	}

	// Bind to owning translate up front — used by ibTranslateCode ctors
	// for the recycled m_current_lex member so the back-pointer is set
	// in the mem-init list, no post-ctor assignment.
	explicit ibLexem(const ibTranslateCode* tc) :
		m_lexType(0),
		m_numData(0),
		m_translateCode(tc),
		m_numLine(0),
#ifdef UTF8_LEXEM_TRANSLATE
		m_numString(0),
		m_numUtf8String(0)
#else
		m_numString(0)
#endif
	{
	}

	ibLexem(const ibLexem& src) :
		m_lexType(src.m_lexType),
		m_numData(src.m_numData),
		m_strData(src.m_strData),
		m_valData(src.m_valData),
		m_translateCode(src.m_translateCode),
		m_numLine(src.m_numLine),
#ifdef UTF8_LEXEM_TRANSLATE
		m_numString(src.m_numString),
		m_numUtf8String(src.m_numUtf8String)
#else
		m_numString(src.m_numString)
#endif // UTF8_LEXEM_TRANSLATE
	{
	}

	// noexcept move so vector<ibLexem>::reserve / emplace_back use moves
	// (pointer-swap of wxString internals, ibValue tagged-union move) on
	// realloc instead of falling back to copy for strong-exception
	// guarantee.
	ibLexem(ibLexem&& src) noexcept :
		m_lexType(src.m_lexType),
		m_numData(src.m_numData),
		m_strData(std::move(src.m_strData)),
		m_valData(std::move(src.m_valData)),
		m_translateCode(src.m_translateCode),
		m_numLine(src.m_numLine),
#ifdef UTF8_LEXEM_TRANSLATE
		m_numString(src.m_numString),
		m_numUtf8String(src.m_numUtf8String)
#else
		m_numString(src.m_numString)
#endif // UTF8_LEXEM_TRANSLATE
	{
		src.m_lexType = 0;
		src.m_numData = 0;
		// m_translateCode intentionally not nulled — recycled m_current_lex
		// keeps its back-pointer across emplace_back(std::move(...)), so
		// the ibTranslateCode ctor's one-time bind is enough.
		src.m_numLine = 0;
		src.m_numString = 0;
#ifdef UTF8_LEXEM_TRANSLATE
		src.m_numUtf8String = 0;
#endif
	}

	ibLexem& operator =(const ibLexem& src)
	{
		m_lexType = src.m_lexType;
		m_numData = src.m_numData;
		m_numLine = src.m_numLine;
		m_numString = src.m_numString;
#ifdef UTF8_LEXEM_TRANSLATE
		m_numUtf8String = src.m_numUtf8String;
#endif

		m_valData = src.m_valData;
		m_strData = src.m_strData;
		m_translateCode = src.m_translateCode;

		return *this;
	}

	ibLexem& operator =(ibLexem&& src) noexcept
	{
		m_lexType = src.m_lexType;
		m_numData = src.m_numData;
		m_numString = src.m_numString;
		m_numLine = src.m_numLine;

		m_valData = std::move(src.m_valData);
		m_strData = std::move(src.m_strData);
		m_translateCode = src.m_translateCode;

		src.m_lexType = 0;
		src.m_numData = 0;
		// m_translateCode kept on src — see move-ctor comment.
		src.m_numLine = 0;
		src.m_numString = 0;
#ifdef UTF8_LEXEM_TRANSLATE
		src.m_numUtf8String = 0;
#endif
		return *this;
	}
};

typedef std::vector<ibLexem> ibLexemList;

/***************************************************
ibTranslateCode-stage of source code parsing
The entry point is the Load() and TranslateModule() procedures.
The first procedure initializes variables and loads
the text of the executable code, the second procedure performs translation
(parsing the code). As a result, an array of "raw" bytecode in the cByteCode variable is filled in the class structure.
****************************************************/

class BACKEND_API ibTranslateCode {
	// ibLexem reads m_strModuleName / m_strDocPath / m_strFileName via
	// its back-pointer accessors (GetModuleName / GetDocPath / GetFileName)
	// — those fields are protected, so grant friendship.
	friend struct ibLexem;

	// A module's #Define table, and one link of the scope chain: module ->
	// parent module -> ... -> the process-wide root (ms_listDefine).
	//
	// READS walk the chain; WRITES never leave the local map. Defining a name an
	// ancestor already holds SHADOWS it here rather than overwriting the ancestor's
	// entry — the parent link is a `const` pointer, so that is enforced by the type
	// and not by discipline. That is what makes the static root safe to share across
	// sessions compiling concurrently: nothing reachable from a compile path can
	// write above itself, so the shared state is immutable and needs no lock. If the
	// platform (or a plugin) ever needs to seed defines into the root, it has to
	// un-const it deliberately — which is the moment to answer "seeded when, by whom,
	// before which session" rather than to discover the answer under load.
	class ibDefineCollection {
	public:
		ibDefineCollection() : m_parentDefine(nullptr) {};

		void Clear() { m_defineList.clear(); }
		void SetParent(const ibDefineCollection* parent) { m_parentDefine = parent; }

		// #Undef — local only, symmetric with SetDefine: an ancestor's define
		// is not ours to remove.
		void RemoveDef(const wxString& strName);
		bool HasDefine(const wxString& strName) const { return FindDefine(strName) != nullptr; }
		// The lookup, chain-walking and read-only: nullptr when nobody defines the
		// name. Callers expand from the returned list by COPY — it is a dictionary
		// entry shared with every other expansion site, not scratch space.
		const ibLexemList* FindDefine(const wxString& strName) const;
		void SetDefine(const wxString& strName, const ibLexemList* src);
		void SetDefine(const wxString& strName, const wxString& strValue);

	private:

		std::map<wxString, ibLexemList, ibCaseFoldLess> m_defineList;//name -> its lexemes, owned by value
		const ibDefineCollection* m_parentDefine;
	};

	// The root every module chain ends at. Empty for the whole process lifetime
	// today; const so it stays that way by construction — see ibDefineCollection.
	static const ibDefineCollection ms_listDefine;

public:

	ibTranslateCode();
	ibTranslateCode(const wxString& strModuleName, const wxString& strDocPath);
	ibTranslateCode(const wxString& strFileName);

	virtual ~ibTranslateCode();

	bool HasDefine(const wxString& strName) const {
		if (m_defineList != nullptr)
			return m_defineList->HasDefine(strName);
		return false;
	};

	//methods:
	void Load(const wxString& strCode);

	void AppendModule(ibTranslateCode* module);
	void RemoveModule(ibTranslateCode* module);

	virtual void OnSetParent(ibTranslateCode* setParent);

	virtual void Clear();
	void ClearLexem() { m_listLexem.resize(0); } // resetting and free data to reuse an object
	size_t GetLexemCount() const { return m_listLexem.size(); } // token count after PrepareLexem (diagnostics / tests)
	const std::vector<ibLexem>& GetLexems() const { return m_listLexem; } // read-only lexem stream (lambda expr recorder / tests)

	bool PrepareLexem();

	// Was the token just emitted a `.`? Asked while classifying the NEXT word:
	// in a property position (`sel.Where`, `q.Select`) a contextual keyword is a
	// member NAME, and stamping it KEYWORD there breaks the parse and the
	// editor's completion after the dot. The lexer already holds what it emitted;
	// asking costs nothing and is decided at the one place that classifies.
	bool PreviousLexemIsDot() const {
		return !m_listLexem.empty()
			&& m_listLexem.back().m_lexType == DELIMITER
			&& m_listLexem.back().m_numData == (short)'.';
	}

protected:
	void SetError(int codeError, unsigned int currPos, const wxString& errorDesc = wxEmptyString) const;
	void SetError(int codeError,
		const wxString& strFileName, const wxString& strModuleName, const wxString& strDocPath,
		int currPos, int currLine,
		const wxString& errorDesc = wxEmptyString) const
	{
		DoSetError(codeError,
			strFileName, strModuleName, strDocPath,
			currPos, currLine, errorDesc
		);
	}
public:

	inline void SkipSpaces() const;

	bool IsByte(const wxUniChar& c) const;
#pragma region get_byte
	bool GetByte() const { return GetByte(nullptr); }
	bool GetByte(wxUniChar& c) const { return GetByte(&c); }
	bool GetByte(wxUniChar* c) const;
#pragma endregion  

	bool IsWord() const;
#pragma region get_word
	bool GetWord(bool realName = false, bool get_point = false) const { return GetWord(nullptr, nullptr, realName, get_point); }
	bool GetWord(wxString& strWord, bool realName = false, bool get_point = false) const { return GetWord(&strWord, nullptr, realName, get_point); }
	bool GetWord(wxString& strWord, wxString& strRealName, bool realName = false, bool get_point = false) const { return GetWord(&strWord, &strRealName, realName, get_point); }
	bool GetWord(wxString* strWord, wxString* strRealName, bool realName, bool get_point) const;
#pragma endregion  

	bool IsNumber() const;
#pragma region get_number
	bool GetNumber() const { return GetNumber(nullptr); }
	bool GetNumber(wxString& strNumber) const { return GetNumber(&strNumber); }
	bool GetNumber(wxString* strNumber) const;
#pragma endregion  

	bool IsString() const;
#pragma region get_string
	bool GetString() const { return GetString(nullptr); }
	bool GetString(wxString& strString) const { return GetString(&strString); }
	bool GetString(wxString* strString) const;
#pragma endregion  

	bool IsDate() const;
#pragma region get_date
	bool GetDate() const { return GetDate(nullptr); }
	bool GetDate(wxString& strDate) const { return GetDate(&strDate); }
	bool GetDate(wxString* strDate) const;
#pragma endregion 

	bool IsEnd() const;

	static int IsKeyWord(const wxString& sKeyWord);
	static wxString GetKeyWord(int keyword);

	wxString GetStrToEndLine() const;
	void PrepareFromCurrent(int nMode, const wxString& strName = wxEmptyString);

	wxString GetModuleName() const { return m_strModuleName; }

	unsigned int GetBufferSize() const { return m_strBuffer.size(); }

	unsigned int GetCurrentLine() const { return m_currentLine; }
	unsigned int GetCurrentPos() const { return m_currentPos; }

#ifdef UTF8_LEXEM_TRANSLATE
	unsigned int GetCurrentUtf8Pos() const { return m_currentUtf8Pos; }
#endif

public:

	// The keyword index used to live here as a public static map, filled by a
	// public LoadKeyWords() that every ctor called behind an `if (empty())` check.
	// It is derived from s_listKeyWord and nothing else, and has exactly one reader,
	// so it is now a build-once immutable table living inside IsKeyWord itself.

	// Per-keyword availability gate. Reads the active code-style and
	// hides VES-only block-fence keywords (Then / Do / EndIf / EndDo /
	// EndFunction / EndProcedure / EndTry) when CES is active — brace-
	// style sources have no place for them. `IsKeyWord` consults this
	// itself, so lexer / highlighter / autocomplete / parser inherit
	// the filter without per-callsite plumbing.
	static bool IsAllowedKey(int keywordId);

protected:

#ifdef UTF8_LEXEM_TRANSLATE

	inline unsigned int GetUtf8CharOffset(const wxUniChar& c) const {

		unsigned int code = c.GetValue();

		//    Char. number range   |        UTF-8 octet sequence
		//       (hexadecimal)     |              (binary)
		//   ----------------------+---------------------------------------------
		//   0000 0000 - 0000 007F | 0xxxxxxx
		//   0000 0080 - 0000 07FF | 110xxxxx 10xxxxxx
		//   0000 0800 - 0000 FFFF | 1110xxxx 10xxxxxx 10xxxxxx
		//   0001 0000 - 0010 FFFF | 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
		//
		//   Code point value is stored in bits marked with 'x', lowest-order bit
		//   of the value on the right side in the diagram above.
		//                                                        (from RFC 3629)

		if (code <= 0x7F)
			return 1;
		else if (code <= 0x07FF)
			return 2;
		else if (code < 0xFFFF)
			return 3;
		else if (code <= 0x10FFFF)
			return 4;

		wxFAIL_MSG(wxT("trying to encode undefined Unicode character"));
		return 0;
	}

	inline void SetUtf8CharOffset(const wxUniChar& c, unsigned int& raw_pos) const {
		raw_pos += GetUtf8CharOffset(c);
	}

#endif // UTF8_LEXEM_TRANSLATE

	virtual void DoSetError(int codeError,
		const wxString& strFileName, const wxString& strModuleName, const wxString& strDocPath,
		unsigned int currPos, unsigned int currLine,
		const wxString& errorDesc = wxEmptyString) const
	{
	}

	size_t CalcAllocSize() const;

	//current lexem
	ibLexem m_current_lex;

	//methods and variables for text parsing
	std::vector<ibTranslateCode*> m_listTranslateCode;

	//Support for "defines":
	ibDefineCollection* m_defineList;

	bool m_bAutoDeleteDefList;
	int m_nModePreparing;

	//attributes:
	wxString m_strModuleName;//name of the compiled module (to display information in case of errors)
	wxString m_strDocPath; // unique path to the document
	wxString m_strFileName; // path to the file (if external processing)

	// Reading position over m_strBuffer. Clear() — reached through Load() — resets
	// all four, so every path that loads text before parsing it is covered; the
	// INITIALISERS are what covers a reader that runs before any text arrives.
	// Left uninitialised, the same shape on ibCompileCode's lexem cursor made the
	// whole eval / watch path answer "Module code expected".
	unsigned int m_bufferSize = 0;//size of the original text

	//original and upper text :
	wxStringImpl m_strBuffer, m_strBUFFER;

	mutable unsigned int m_currentPos = 0; //current position of the processed text
	mutable unsigned int m_currentLine = 0; //current line of the processed text
#ifdef UTF8_LEXEM_TRANSLATE
	mutable unsigned int m_currentUtf8Pos = 0; //current raw position of the processed text
#endif // UTF8_LEXEM_TRANSLATE

	//intermediate array with lexemes:
	std::vector<ibLexem> m_listLexem;
};

//empty lexem  
extern BACKEND_API const ibLexem gs_nullLexem;

#endif