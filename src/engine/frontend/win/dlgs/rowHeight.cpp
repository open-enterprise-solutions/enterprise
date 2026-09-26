#include "rowHeight.h"
#include "frontend/win/editor/gridEditor/gridEditor.h"

ibDialogRowHeight::ibDialogRowHeight(ibGridEditor* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : 
	wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// ⭐ AUTOMATIC ROW HEIGHT, the first question and the default: on, the rows keep no height of their own
	// and follow their text; off, they take the height below. Shown ON when none of the selected rows has a
	// height of its own — which is how a row starts out.
	bool autoHeight = true;
	wxObjectDataPtr<ibBackendSpreadsheetObject> doc;
	if (parent->GetActiveDocument(doc) && doc != nullptr) {
		for (auto cell : parent->GetSelectedBlocks()) {
			for (int row = cell.GetTopRow(); row <= cell.GetBottomRow() && autoHeight; row++)
				autoHeight = !doc->GetSpreadsheetDesc().HasRowSize(row);
		}
	}

	m_autoHeight = new wxCheckBox(this, wxID_ANY, _("Auto row height"), wxDefaultPosition, wxDefaultSize, 0);
	m_autoHeight->SetValue(autoHeight);
	mainSizer->Add(m_autoHeight, 0, wxALL, FromDIP(5));

	// (A "Maximum row height" checkbox stood here and did nothing at all — no model behind it, and the
	//  misspelling of its own caption never came up, which says how far it was from working. A ceiling on
	//  automatic height is a per-row fact the description does not hold; when it does, this is where it
	//  goes — spreadsheet-document.md § 2a.)
	wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);

	int height = 0;

	for (auto cell : parent->GetSelectedBlocks()) {
		if (cell.GetTopRow() > 0 &&
			cell.GetLeftCol() == 0) {
			height = parent->GetRowSize(cell.GetTopRow());
		}
		else if (cell.GetLeftCol() == 0 &&
			cell.GetTopRow() == 0) {
			if (cell.GetBottomRow() == parent->GetNumberRows()) {
				height = parent->GetRowSize(cell.GetTopRow());
			}
		}
	}

	m_spinCtrlHeight = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 999, height);
	m_spinCtrlHeight->SetDigits(2);
	rowSizer->Add(m_spinCtrlHeight, 0, 0, FromDIP(5));
	mainSizer->Add(rowSizer, 1, wxEXPAND, FromDIP(5));

	// A height is asked for only when the rows are not to follow their text.
	m_spinCtrlHeight->Enable(!autoHeight);
	m_autoHeight->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
		m_spinCtrlHeight->Enable(!m_autoHeight->GetValue());
	});

	m_sdbSizerBottom = new wxStdDialogButtonSizer();
	m_sdbSizerBottomOK = new wxButton(this, wxID_OK);
	m_sdbSizerBottom->AddButton(m_sdbSizerBottomOK);
	m_sdbSizerBottomCancel = new wxButton(this, wxID_CANCEL);
	m_sdbSizerBottom->AddButton(m_sdbSizerBottomCancel);
	m_sdbSizerBottom->Realize();

	mainSizer->Add(m_sdbSizerBottom, 1, wxEXPAND, FromDIP(5));

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();
	mainSizer->Fit(this);

	wxDialog::Centre(wxBOTH);
}

ibDialogRowHeight::~ibDialogRowHeight()
{
}