#ifndef __QUERY_CONSTRUCTOR_FIELD_H__
#define __QUERY_CONSTRUCTOR_FIELD_H__

////////////////////////////////////////////////////////////////////////////
//	Description : ONE FIELD A QUERY MAY NAME - as the server describes it and every window reads it
////////////////////////////////////////////////////////////////////////////
//
// The server's model (backend/query/queryConstructorModel.h) asks the configuration and fills it; the
// windows that offer fields read it — the desktop's beside the model, the client's from the server's
// answer. One schema for both, so it lives here: nothing in it is a widget, and its picture is a
// description a reader makes its own image from.

#include "core/description/typeDescription.h"   // ibTypeDescription — what it holds
#include "core/description/pictureDescription.h"   // ibPictureDescription — how it looks, by its number
#include "core/query/queryBalanceRole.h"        // ibBalanceRole — what it is in a balance

#include <wx/string.h>

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
	// HOW IT LOOKS, taken from the COLUMN (ibBackendSourceColumn::GetColumnPicture) rather than
	// decided by the shell: a dimension, a resource and a plain attribute each answer with their
	// own, and a shell that draws them holds no list of kinds. Empty only for a field standing
	// behind no column at all — the drawer then uses its default picture.
	ibPictureDescription m_picture;
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

#endif // __QUERY_CONSTRUCTOR_FIELD_H__
