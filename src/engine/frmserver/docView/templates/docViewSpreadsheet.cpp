#include "docViewSpreadsheet.h"

#include "backend/sheetFormat/sheetFormat.h"   // the file is the session's temporary one
#include "frmserver/visualView/ctrl/gridBox.h"   // the sheet drawn and read as a gridbox's

// ----------------------------------------------------------------------------
// ibSpreadsheetEditView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetEditView, ibMetaView);

void ibSpreadsheetEditView::OnDraw(ibDataNode& frame)
{
	const ibSpreadsheetDocument* const document = dynamic_cast<const ibSpreadsheetDocument*>(GetDocument());
	if (document == nullptr)
		return;

	// THE TAB'S VIEW IS A GRIDBOX'S NODE — a client draws it with the one it draws a form's sheet with. The document's
	// sheet is its own and is not replaced: no Version, read once. Its grid lines shown, as the desktop's document showed
	// them; a report in a form has none.
	frame.SetClsid(g_controlGridBoxCLSID);
	ibDataNode& state = frame.Child(wxT("State"));
	state.SetValue(wxT("GridLines"), true);
}

bool ibSpreadsheetEditView::Fetch(const ibDataNode& WXUNUSED(request), ibDataNode& response)
{
	const ibSpreadsheetDocument* const document = dynamic_cast<const ibSpreadsheetDocument*>(GetDocument());
	return document != nullptr && ibFetchSpreadsheet(document->GetSpreadsheet(), response);
}

bool ibSpreadsheetEditView::OnClientEvent(ibProtocolEvent event, const ibDataNode& args)
{
	ibSpreadsheetDocument* const document = dynamic_cast<ibSpreadsheetDocument*>(GetDocument());
	if (document == nullptr)
		return false;

	switch (event) {
	case ibProtocolEvent::Cell: {
		// A CELL BOUND TO A VALUE OPENS IT — the desktop editor's double click (ibGridEditor::OnMouseLeftDown): asked of
		// the sheet, opened by the runtime; on a sheet being edited the click is the edit's. The cell as Fetch wrote it —
		// from 1.
		const ibBackendSpreadsheetObject& spreadsheet = document->GetSpreadsheet();
		const int row = args.GetValue<s32>(wxT("Row")) - 1, col = args.GetValue<s32>(wxT("Col")) - 1;
		if (spreadsheet.IsEditable() || row < 0 || col < 0)
			return true;
		wxString bound;
		spreadsheet.GetCellDetailsParameter(row, col, bound);
		ibValue boundValue;
		if (!bound.IsEmpty() && spreadsheet.GetParameter(bound, boundValue))
			boundValue.ShowValue();
		return true;
	}
	case ibProtocolEvent::Change: {
		ibSpreadsheetDescription sheet;
		if (!ibSpreadsheetDescriptionMemory::ReadNode(args.GetProperty(wxT("Sheet")), sheet))
			return false;
		document->SetSpreadsheetDesc(std::move(sheet));
		return true;
	}
	default:
		return false;
	}
}

// ----------------------------------------------------------------------------
// ibSpreadsheetDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibSpreadsheetDocument, ibMetaDocument);

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetFileDocument, ibSpreadsheetDocument);

// ----------------------------------------------------------------------------
// ibSpreadsheetFileDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

// The sheet is the document's own, so it saves and loads it itself rather than through Save/LoadObject. Its file is the
// session's temporary one (by its id), read and written in the format the name it came under says.
bool ibSpreadsheetFileDocument::DoOpenDocument(const wxString& filename)
{
	return ibSheetFormatReadTempFile(filename, m_spreadSheetDocument->GetSpreadsheetDesc());
}

wxString ibSpreadsheetFileDocument::GetSaveFilter() const
{
	return ibSheetFormatSaveFilter();
}

bool ibSpreadsheetFileDocument::DoSaveDocument(const wxString& filename)
{
	return ibSheetFormatWriteTempFile(filename, m_spreadSheetDocument->GetSpreadsheetDesc());
}

// ----------------------------------------------------------------------------
// ibSpreadsheetGridBoxDocument / View: the document a form's grid box holds, and its view
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetGridBoxDocument, ibSpreadsheetFileDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibSpreadsheetGridBoxView, ibSpreadsheetEditView);

ibSpreadsheetGridBoxDocument::ibSpreadsheetGridBoxDocument() : ibSpreadsheetFileDocument()
{
	SetTitle(_("Spreadsheet document"));
}
