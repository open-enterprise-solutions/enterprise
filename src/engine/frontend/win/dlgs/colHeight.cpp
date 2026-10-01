#include "colHeight.h"
#include "frontend/win/editor/gridEditor/gridEditor.h"

ibDialogColWidth::ibDialogColWidth(ibGridEditor* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	// ⭐ THE DEFAULT WIDTH, the first question and the same shape the row height dialog asks in: on, the
	// columns keep no width of their own; off, they take the width below. Shown ON when none of the selected
	// columns has a width of its own — which is how a column starts out.
	bool defaultWidth = true;
	wxObjectDataPtr<ibBackendSpreadsheetObject> doc;
	if (parent->GetActiveDocument(doc) && doc != nullptr) {
		for (auto cell : parent->GetSelectedBlocks()) {
			for (int col = cell.GetLeftCol(); col <= cell.GetRightCol() && defaultWidth; col++)
				defaultWidth = !doc->GetSpreadsheetDesc().HasColSize(col);
		}
	}

	m_defaultWidth = new wxCheckBox(this, wxID_ANY, _("Default column width"), wxDefaultPosition, wxDefaultSize, 0);
	m_defaultWidth->SetValue(defaultWidth);
	mainSizer->Add(m_defaultWidth, 0, wxALL, FromDIP(5));

	// (A "Maximum column width" checkbox stood here and did nothing — see the note in rowHeight.cpp.)
	wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);

	int width = 0;

	for (auto cell : parent->GetSelectedBlocks()) {
		if (cell.GetTopRow() == 0 &&
			cell.GetLeftCol() > 0) {
			width = parent->GetColSize(cell.GetLeftCol());
		}
		else if (cell.GetLeftCol() == 0 &&
			cell.GetTopRow() == 0) {
			if (cell.GetRightCol() == parent->GetNumberCols()) {
				width = parent->GetColSize(cell.GetLeftCol());
			}
		}
	}

	m_spinCtrlWidth = new wxSpinCtrlDouble(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 0, 999, width);
	m_spinCtrlWidth->SetDigits(2);
	rowSizer->Add(m_spinCtrlWidth, 0, 0, FromDIP(5));
	mainSizer->Add(rowSizer, 1, wxEXPAND, FromDIP(5));

	// A width is asked for only when the columns are not to take the default one.
	m_spinCtrlWidth->Enable(!defaultWidth);
	m_defaultWidth->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
		m_spinCtrlWidth->Enable(!m_defaultWidth->GetValue());
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

ibDialogColWidth::~ibDialogColWidth()
{
}