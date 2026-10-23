#include "docViewSpreadsheet.h"

#include "backend/sheetFormat/sheetFormat.h"   // the file is the session's temporary one
#include "frmserver/visualView/ctrl/gridBox.h"   // the sheet drawn and read as a gridbox's

// ----------------------------------------------------------------------------
// ibSheetView implementation
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSheetView, ibMetaView);

void ibSheetView::OnDraw(ibDataNode& frame)
{
	const ibSheetDocument* const document = dynamic_cast<const ibSheetDocument*>(GetDocument());
	if (document == nullptr)
		return;

	// THE TAB'S VIEW IS A GRIDBOX'S NODE — a client draws it with the one it draws a form's sheet with. The document's
	// sheet is its own and is not replaced: no Version, read once. Its grid lines shown, as the desktop's document showed
	// them; a report in a form has none.
	frame.SetClsid(g_controlGridBoxCLSID);
	ibDataNode& state = frame.Child(ibProtocol::ibView::State);
	state.SetValue(ibProtocol::ibForm::ibSpreadsheet::GridLines, true);
}

bool ibSheetView::Fetch(const ibDataNode& /*request*/, ibDataNode& response)
{
	const ibSheetDocument* const document = dynamic_cast<const ibSheetDocument*>(GetDocument());
	return document != nullptr && ibFetchSpreadsheet(document->GetSpreadsheet(), response);
}

bool ibSheetView::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	ibSheetDocument* const document = dynamic_cast<ibSheetDocument*>(GetDocument());
	if (document == nullptr)
		return false;

	switch (event) {
	case ibProtocol::ibEvent::ibKind::Cell: {
		// A CELL BOUND TO A VALUE OPENS IT — the desktop editor's double click (ibGridEditor::OnMouseLeftDown): asked of
		// the sheet, opened by the runtime; on a sheet being edited the click is the edit's. The cell as Fetch wrote it —
		// from 1.
		const ibSpreadsheetObject& spreadsheet = document->GetSpreadsheet();
		const int row = args.GetValue<std::int32_t>(ibProtocol::ibEvent::Row) - 1, col = args.GetValue<std::int32_t>(ibProtocol::ibEvent::Col) - 1;
		if (spreadsheet.IsEditable() || row < 0 || col < 0)
			return true;
		ibString bound;
		spreadsheet.GetCellDetailsParameter(row, col, bound);
		ibAnyValuePtr<ibAnyValue> held;
		if (!bound.IsEmpty() && spreadsheet.GetParameter(bound, held)) {
			ibValue boundValue = ibValue::FromAnyValue(held);
			boundValue.ShowValue();
		}
		return true;
	}
	case ibProtocol::ibEvent::ibKind::Change: {
		ibSpreadsheetDescription sheet;
		if (!ibSpreadsheetDescriptionMemory::ReadNode(args.GetProperty(ibProtocol::ibForm::ibSpreadsheet::Sheet), sheet))
			return false;
		document->SetSpreadsheetDesc(std::move(sheet));
		return true;
	}
	default:
		return false;
	}
}

// ----------------------------------------------------------------------------
// ibSheetDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

wxIMPLEMENT_ABSTRACT_CLASS(ibSheetDocument, ibMetaDocument);

wxIMPLEMENT_DYNAMIC_CLASS(ibSheetFileDocument, ibSheetDocument);

// ----------------------------------------------------------------------------
// ibSheetFileDocument: ibDocument and its sheet married
// ----------------------------------------------------------------------------

// The sheet is the document's own, so it saves and loads it itself rather than through Save/LoadObject. Its file is the
// session's temporary one (by its id), read and written in the format the name it came under says.
bool ibSheetFileDocument::DoOpenDocument(const wxString& filename)
{
	return ibSheetFormatReadTempFile(filename, m_spreadSheetDocument->GetSpreadsheetDesc());
}

wxString ibSheetFileDocument::GetSaveFilter() const
{
	return ibSheetFormatSaveFilter();
}

bool ibSheetFileDocument::DoSaveDocument(const wxString& filename)
{
	return ibSheetFormatWriteTempFile(filename, m_spreadSheetDocument->GetSpreadsheetDesc());
}

// ----------------------------------------------------------------------------
// ibSheetGridBoxDocument / View: the document a form's grid box holds, and its view
// ----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(ibSheetGridBoxDocument, ibSheetFileDocument);
wxIMPLEMENT_DYNAMIC_CLASS(ibSheetGridBoxView, ibSheetView);

ibSheetGridBoxDocument::ibSheetGridBoxDocument() : ibSheetFileDocument()
{
	SetTitle(_("Spreadsheet document"));
}
