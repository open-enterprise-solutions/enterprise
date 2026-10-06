#include "docViewSpreadsheet.h"

// ----------------------------------------------------------------------------
// ibSpreadsheetEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetEditView, ibMetaView);

// ----------------------------------------------------------------------------
// ibSpreadsheetDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibSpreadsheetDocument, ibMetaDocument);

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetFileDocument, ibSpreadsheetDocument);

// ----------------------------------------------------------------------------
// ibSpreadsheetFileDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

// The sheet is the document's own, so it saves and loads it itself rather than through Save/LoadObject.
bool ibSpreadsheetFileDocument::DoOpenDocument(const wxString& filename)
{
	return m_spreadSheetDocument->LoadFromFile(filename);
}

bool ibSpreadsheetFileDocument::DoSaveDocument(const wxString& filename)
{
	return m_spreadSheetDocument->SaveToFile(filename);
}
