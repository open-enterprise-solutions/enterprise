#ifndef _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__
#define _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__

// THE QUERY CONSTRUCTOR'S MODEL — the engine's (backend/query/queryConstructorModel.h): what may go into a query, its
// sources and their fields, asked of the configuration — the server's. What the client's windows take of it is the
// answer: the fields a query text offers, as the server described them with the window's question.

#include <wx/icon.h>

#include "frmclient/backend/typeDescription.h"
#include "frmclient/backend/query/queryAST.h"
#include "frmclient/backend/query/queryBalanceRole.h"

#include <vector>

class ibMetaData;

// One field of a source: the name a query writes and the words a person reads.
struct ibQueryConstructorField
{
	wxString m_name;           // the technical name — this is what the AST carries
	wxString m_presentation;   // what the tree shows — the NAME, the constructor speaks the query's language
	// WHICH SOURCE IT CAME OUT OF, by the name the query calls that source. Carried on the field so
	// every list of fields can GROUP by it — a flat merge of two tables' fields is unreadable the
	// moment both have a `Code`, and the reader cannot tell which is which without the prefix on
	// every single line (which is the other unreadable answer).
	wxString m_source;
	bool     m_reference = false;   // the field is a reference: it can be dot-walked further
	// WHAT it refers to, when it refers to exactly one thing. Non-zero is the answer to "can this
	// be unfolded", and it is also the key the fields behind it are asked for by — so the shell
	// never has to look a type up, and a COMPOSITE reference (which the engine cannot dot-walk
	// through) is simply left at zero and shown as a leaf.
	ibClassID m_referenceClsid = 0;
	// WHAT IT HOLDS. The explorer node already knows; carrying it here is what lets a host ask a
	// type question (which aggregates fit this field?) without walking the metadata itself.
	ibTypeDescription m_type;
	// HOW IT LOOKS, taken from the COLUMN (ibBackendSourceColumn::GetColumnIcon) rather than
	// decided by the shell: a dimension, a resource and a plain attribute each answer with their
	// own, and a shell that draws them holds no list of kinds. Null only for a field standing
	// behind no column at all — the drawer then uses its default picture.
	wxIcon m_icon;
	// WHETHER A PERSON IS OFFERED IT — asked of the COLUMN too (ibBackendSourceColumn::IsAvailable), so
	// the functional options of the base answer, and a dot-walk is available only while every hop is.
	// The query constructor does not read it: a query is written against the schema, which options leave
	// alone. The settings pickers and a composition's run do, since what they offer or print is what a
	// person may see.
	bool m_available = true;
	// ⭐ WHAT THE SOURCE CALLS IT, and WHAT IT IS IN A BALANCE — asked of the COLUMN a plain projection reads
	// (its synonym, ibBackendSourceColumn::GetBalanceRole), empty / None for anything computed. The
	// constructor still shows the NAME (m_presentation); these are what a composition fills a field in
	// with before anybody says otherwise — its generated title, and its role.
	wxString      m_caption;
	ibBalanceRole m_balanceRole = ibBalanceRole::None;
};

// ⭐⭐ WHAT FIELDS A QUERY TEXT OFFERS — on the client, as the server said: parsing the text and asking the configuration
// are its, so it described the fields with the window's question (Fields — a field's name, its source's caption, its
// type, whether the options leave it). `text` and `metaData` are the desktop's and are not read here; a window whose
// text changes (the designer's Query tab) will ask the server again. `error` stays empty — a text the server could not
// read offers no fields.
FRMCLIENT_API std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& text,
	const ibMetaData* metaData, wxString* error = nullptr);

#endif
