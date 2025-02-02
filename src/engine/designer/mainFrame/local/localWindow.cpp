////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : local window
////////////////////////////////////////////////////////////////////////////

#include "localWindow.h"
#include "backend/debugger/debugClient.h"

wxBEGIN_EVENT_TABLE(CLocalWindow, wxPanel)
EVT_SIZE(CLocalWindow::OnSize)
wxEND_EVENT_TABLE()

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CLocalWindow::CLocalWindow(wxWindow* parent, int id) :
	wxPanel(parent, id), m_treeCtrl(new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT))
{
	m_columnSize[0] = 0.09f;
	m_columnSize[1] = 0.5;
	m_columnSize[2] = 0.1;

	SetSizer(new wxBoxSizer(wxHORIZONTAL));
	ClearAndCreate();
	GetSizer()->Add(m_treeCtrl, 1, wxEXPAND);

	m_treeCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &CLocalWindow::OnItemSelected, this);
}

static bool s_initialize = false;

void CLocalWindow::SetLocalVariable(const localWindowData_t& locData)
{
	ClearAndCreate();

	for (unsigned int idx = 0; idx < locData.GetVarCount(); idx++) {
		long index = m_treeCtrl->InsertItem(m_treeCtrl->GetItemCount(), locData.GetName(idx));
		m_treeCtrl->SetItem(index, 0, locData.GetName(idx));
		m_treeCtrl->SetItem(index, 1, locData.GetValue(idx));
		m_treeCtrl->SetItem(index, 2, locData.GetType(idx));
		if (idx == 0) {
			s_initialize = true;
			m_treeCtrl->SetItemState(index,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			s_initialize = false;
		}
	}
}

void CLocalWindow::UpdateColumnSizes()
{
	// We subtract two off of the size to avoid generating scroll bars on the window.
	int totalSize = GetClientSize().x - 2;

	int columnSize[3];
	GetColumnSizes(totalSize, columnSize);

	for (unsigned int i = 0; i < s_numColumns; ++i) {
		m_treeCtrl->SetColumnWidth(i, columnSize[i]);
	}
}

void CLocalWindow::GetColumnSizes(int totalSize, int columnSize[s_numColumns]) const
{
	int fixedSize = 0;

	for (unsigned int i = 0; i < s_numColumns; ++i) {
		if (m_columnSize[i] < 0.0f) {
			columnSize[i] = static_cast<int>(-m_columnSize[i]);
			fixedSize += columnSize[i];
		}
	}

	// Set the size of the proportional columns.
	for (unsigned int i = 0; i < s_numColumns; ++i) {
		if (m_columnSize[i] >= 0.0f) {
			columnSize[i] = static_cast<int>(m_columnSize[i] * (totalSize - fixedSize) + 0.5f);
		}
	}

	// Make sure the total size is exactly correct by resizing the final column.
	for (unsigned int i = 0; i < s_numColumns - 1; ++i) {
		totalSize -= columnSize[i];
	}

	columnSize[s_numColumns - 1] = totalSize;
}

void CLocalWindow::OnItemSelected(wxListEvent& event)
{
	if (s_initialize)
		return;

	event.Skip();
}

void CLocalWindow::OnSize(wxSizeEvent& event)
{
	UpdateColumnSizes();
	event.Skip();
}

void CLocalWindow::ClearAndCreate()
{
	m_treeCtrl->ClearAll();
	m_treeCtrl->AppendColumn("Name", wxLIST_FORMAT_LEFT, 100);
	m_treeCtrl->AppendColumn("Value", wxLIST_FORMAT_LEFT, 150);
	m_treeCtrl->AppendColumn("Type", wxLIST_FORMAT_LEFT, 250);

	UpdateColumnSizes();
}

#include "mainFrame/mainFrameDesigner.h"

CLocalWindow* CLocalWindow::GetLocalWindow()
{
	if (CDocDesignerMDIFrame::GetFrame())
		return mainFrame->GetLocalWindow();
	return nullptr; 
}

CLocalWindow::~CLocalWindow()
{
	m_treeCtrl->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &CLocalWindow::OnItemSelected, this);
}