#include "colHeightWnd.h"
#include "frontend/grid/gridCommon.h"

CColWidthWnd::CColWidthWnd(CGrid* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style) :
	wxDialog(parent, id, title, pos, size, style)
{
	wxDialog::SetSizeHints(wxDefaultSize, wxDefaultSize);

	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);
	wxBoxSizer* rowSizer = new wxBoxSizer(wxHORIZONTAL);
	m_maximumCol = new wxCheckBox(this, wxID_ANY, _("Maximun column width"), wxDefaultPosition, wxDefaultSize, 0);
	rowSizer->Add(m_maximumCol, 0, wxALL, 5);

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
	rowSizer->Add(m_spinCtrlWidth, 0, 0, 5);
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

CColWidthWnd::~CColWidthWnd()
{
}