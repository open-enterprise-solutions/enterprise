#include "rowHeight.h"
#include "frontend/mainFrame/grid/gridCommon.h"

CRowHeightWnd::CRowHeightWnd(CGrid* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) : 
	wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);
	m_maximumRow = new wxCheckBox(this, wxID_ANY, _("Maximun row height"), wxDefaultPosition, wxDefaultSize, 0);
	rowSizer->Add(m_maximumRow, 0, wxALL, 5);

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
	rowSizer->Add(m_spinCtrlHeight, 0, 0, 5);
	mainSizer->Add(rowSizer, 1, wxEXPAND, 5);

	m_sdbSizerBottom = new wxStdDialogButtonSizer();
	m_sdbSizerBottomOK = new wxButton(this, wxID_OK);
	m_sdbSizerBottom->AddButton(m_sdbSizerBottomOK);
	m_sdbSizerBottomCancel = new wxButton(this, wxID_CANCEL);
	m_sdbSizerBottom->AddButton(m_sdbSizerBottomCancel);
	m_sdbSizerBottom->Realize();

	mainSizer->Add(m_sdbSizerBottom, 1, wxEXPAND, 5);

	wxDialog::SetSizer(mainSizer);
	wxDialog::Layout();
	mainSizer->Fit(this);

	wxDialog::Centre(wxBOTH);
}

CRowHeightWnd::~CRowHeightWnd()
{
}