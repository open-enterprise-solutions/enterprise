#ifndef _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__
#define _FRMCLIENT_BACKEND_QUERY_CONSTRUCTOR_MODEL_H__

// THE QUERY CONSTRUCTOR'S MODEL — the engine's (backend/query/queryConstructorModel.h): what may go into a query, its
// sources and their fields, asked of the configuration — the server's. What the client's windows take of it is the
// answer: the fields a query text offers (ibQueryConstructorField, the core's), as the server described them with the
// window's question.

#include "frmclient/backend/backend_core.h"   // FRMCLIENT_API
#include "core/query/queryConstructorField.h"   // ibQueryConstructorField — one field, as every window reads it

#include <vector>

class ibMetaData;

// ⭐⭐ WHAT FIELDS A QUERY TEXT OFFERS — on the client, as the server said: parsing the text and asking the configuration
// are its, so it described the fields with the window's question (Fields — a field's name, its source's caption, its
// type, whether the options leave it). A configuration that is a tab's (a composer's panel, ibMetaData::GetTabId) asks
// that tab for `text` — the server parses it; `error` is why it offers none. Otherwise `text` is not read: the fields
// are the question's.
FRMCLIENT_API std::vector<ibQueryConstructorField> ibQueryFieldsOfText(const wxString& text,
	const ibMetaData* metaData, wxString* error = nullptr);

// …AND WHAT A TAB'S VIEW ALREADY SAID OF A TEXT — a composer's tab draws its query's Fields and References with the view
// (and Error): kept here by the tab, so the panel's read of that same text asks nothing; forgotten with the tab.
class ibProtocolNode;
FRMCLIENT_API void ibQueryFieldsKnown(long long tabId, const wxString& text, const ibProtocolNode& answer);
FRMCLIENT_API void ibQueryFieldsForget(long long tabId);

// A DYNAMIC LIST'S FIELDS on a tab — those of its main table (wxNOT_FOUND: none) and what its query adds, as the desktop's
// list settings window reads them; asked of the tab (fetch {Tab, Request: {Kind: Source, Id, Text}}) once per main table
// and text. Off a tab: none.
FRMCLIENT_API std::vector<struct ibSettingsPlainField> ibListSourceFields(ibMetaID mainTable, const wxString& text,
	const ibMetaData* metaData, wxString* error = nullptr);

#endif
