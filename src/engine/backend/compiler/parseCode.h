#ifndef __IB_PARSE_CODE_H__
#define __IB_PARSE_CODE_H__

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

struct ibModuleElement
{
	wxString      m_name;            // element identifier (function / procedure / variable name)
	wxString      m_shortDescription;  // object kind label (one-line tooltip)

	int           m_imageIndex = 0;    // icon index in the autocomplete image list
	int           m_lineStart  = -1;   // first source line where the element appears
	int           m_lineEnd    = -1;   // last source line where the element appears

	// How many formal parameters the declaration takes. The walk already steps over them one by
	// one; keeping the count is what lets a module's export surface built from TEXT say the same
	// thing as one built from BYTECODE, where it arrives as ibByteCode::GetNParams.
	int           m_paramCount = 0;

	wxString      m_moduleName;        // owning module name
	ibContentType m_eType      = eEmpty;
};

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