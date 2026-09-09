#ifndef __IB_SCRIPT_PARSE_CODE_H__
#define __IB_SCRIPT_PARSE_CODE_H__

// WHAT A MODULE TEXT DECLARES, read WITHOUT compiling it — the walk that fills the designer's
// "Procedures and functions" window, the editor's autocomplete, and module_outline.
//
// ⭐ IT LIVES IN THE BACKEND BECAUSE THE ANSWER IS NEEDED ON BOTH SIDES. It began in the code
// editor, and by location it looked like an editor concern; it is not. A module's exports are ALSO
// its own value's member surface (ibRuntimeModuleDataObject::ExportMethodsToHelper), and that
// surface reads BYTECODE — which the designer does not have, because it compiles nothing. So the
// same question — "what does this text export" — was answered by this parser on one side and by
// nothing at all on the other: `StockManagement.` offered nothing while module_outline listed its
// exports from the very same text.
//
// One walk, one answer, both readers. It derives from ibTranslateCode, which was always a backend
// class, and it touches no widget — the move is where it belonged.

#include "translateCode.h"

enum ibContentType
{
	eVariable = 0,
	eExportVariable,
	eProcedure,
	eExportProcedure,
	eFunction,
	eExportFunction,
	eLambda,           // anonymous Function/Procedure used as expression —
	                   // no name, navigable by line range. m_name carries
	                   // a synthetic label like "<lambda @ line 23>" for
	                   // listing in symbol pickers; declaration kind
	                   // (function vs procedure) is encoded in m_imageIndex.

	eEmpty
};

// One formal parameter of a declaration, as the text spells it.
//
// ⭐ A NAME, NOT A TALLY. The walk stepped over these one by one and kept only how many there
// were — so a module's export surface could say a method takes three arguments and never which
// three, and every caller looking at `Print` was told its arity and left to guess the rest
// (2026-09-09, reading a manager's members through script_complete: platform verbs came back
// with their call form, a configuration's own with a bare name).
//
// The two axes beside the name are the ones that change how a CALL is written: `Val` decides
// whether the callee can hand something back through the argument, and a default decides whether
// the argument may be left out at all.
struct ibModuleParam
{
	wxString m_name;
	bool     m_byValue  = false;   // declared `Val` — copied in, so the caller sees no change
	bool     m_optional = false;   // has a default, so it may be omitted
};

struct ibModuleElement
{
	wxString      m_name;            // element identifier (function / procedure / variable name)
	wxString      m_shortDescription;  // object kind label (one-line tooltip)

	int           m_imageIndex = 0;    // icon index in the autocomplete image list
	int           m_lineStart  = -1;   // first source line where the element appears
	int           m_lineEnd    = -1;   // last source line where the element appears

	// The formal parameters this declaration takes. What lets a module's export surface built
	// from TEXT say the same thing as one built from BYTECODE, where the names arrive as
	// ibByteParam::m_strName.
	//
	// 🛑 THE COUNT IS THIS VECTOR'S SIZE and is not stored beside it: two places holding the same
	// number is two places to disagree.
	std::vector<ibModuleParam> m_params;

	int ParamCount() const { return (int)m_params.size(); }

	wxString      m_moduleName;        // owning module name
	ibContentType m_eType      = eEmpty;
};

// HOW THIS DECLARATION IS CALLED, in one line — `Post(Val Cancel, Mode, [Reason])`.
//
// ⭐ WRITTEN ONCE, BESIDE THE THING IT DESCRIBES. Two readers want it — the module's member
// surface (ibRuntimeModuleDataObject::ExportMethodsToHelper) and module_outline — and a second
// copy of the spelling is a second convention.
//
// Optional parameters go in SQUARE BRACKETS because that is what this platform's own syntax
// helper already does (`WriteJournalEvent(<String>, [<Marker>], [<Category>])`), and `Val` is
// written where the declaration writes it. No types: the text does not state them.
inline wxString ibModuleCallForm(const ibModuleElement& element)
{
	wxString form = element.m_name + wxT("(");

	for (size_t index = 0; index < element.m_params.size(); ++index) {

		const ibModuleParam& param = element.m_params[index];

		if (index > 0)
			form += wxT(", ");

		if (param.m_optional)
			form += wxT("[");
		if (param.m_byValue)
			form += wxT("Val ");

		form += param.m_name;

		if (param.m_optional)
			form += wxT("]");
	}

	return form + wxT(")");
}

class BACKEND_API ibParseCode : public ibTranslateCode
{
	int                          m_cursor = wxNOT_FOUND;  // current position in the lexem array
	std::vector<ibModuleElement> m_content;

protected:

	const ibLexem& PreviewGetLexem();
	const ibLexem& GetLexem();
	const ibLexem& ExpectLexem();
	void           ExpectDelimeter(const wxUniChar& c);

	bool           IsNextDelimeter(const wxUniChar& c);
	bool           IsNextKeyWord(int keyword);
	void           ExpectKeyword(int keyword);
	wxString       ExpectIdentifier(bool strRealName = false);
	ibValue        ExpectConstant();

public:

	ibParseCode();
	bool ParseModule(const wxString& sModule);

	// Module elements collected by ParseModule — list of every
	// procedure / function / variable declaration found in the source.
	// Callers iterate and filter themselves; the dedicated GetVariables
	// / GetFunctions / GetProcedures helpers were unreachable and got
	// removed.
	std::vector<ibModuleElement>& GetAllContent() { return m_content; }
};

#endif 