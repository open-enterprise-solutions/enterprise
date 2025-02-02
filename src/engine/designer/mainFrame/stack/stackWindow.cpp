////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : stack window
////////////////////////////////////////////////////////////////////////////

#include "stackWindow.h"
#include "backend/debugger/debugClient.h"

wxBEGIN_EVENT_TABLE(CStackWindow, wxPanel)
EVT_SIZE(CStackWindow::OnSize)
wxEND_EVENT_TABLE()

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CStackWindow::CStackWindow(wxWindow* parent, int id) :
	wxPanel(parent, id), m_treeCtrl(new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT))
{
	m_columnSize[0] = 0.09f;
	m_columnSize[1] = -120;

	SetSizer(new wxBoxSizer(wxHORIZONTAL));
	ClearAndCreate();
	GetSizer()->Add(m_treeCtrl, 1, wxEXPAND);

	m_treeCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &CStackWindow::OnItemSelected, this);
}

static bool s_initialize = false;

void CStackWindow::SetStack(const stackData_t& stackData)
{
	ClearAndCreate();

	for (unsigned int idx = 0; idx < stackData.GetStackCount(); idx++) {
		long index = m_treeCtrl->InsertItem(m_treeCtrl->GetItemCount(), stackData.GetModuleName(idx));
		m_treeCtrl->SetItem(index, 0, stackData.GetModuleLine(idx));
		m_treeCtrl->SetItem(index, 1, stackData.GetModuleName(idx));
		if (idx == 0) {
			s_initialize = true;
			m_treeCtrl->SetItemState(index,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			s_initialize = false;
		}
	}
}

void CStackWindow::UpdateColumnSizes()
{
	// We subtract two off of the size to avoid generating scroll bars on the window.
	int totalSize = GetClientSize().x - 2;

	int columnSize[2];
	GetColumnSizes(totalSize, columnSize);

	for (unsigned int i = 0; i < s_numColumns; ++i) {
		m_treeCtrl->SetColumnWidth(i, columnSize[i]);
	}
}

void CStackWindow::GetColumnSizes(int totalSize, int columnSize[s_numColumns]) const
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

void CStackWindow::OnItemSelected(wxListEvent& event)
{
	if (s_initialize)
		return;

	debugClient->SetLevelStack(
		m_treeCtrl->GetItemCount() - event.GetIndex() - 1
	);
	
	event.Skip();
}

void CStackWindow::OnSize(wxSizeEvent& event)
{
	UpdateColumnSizes();
	event.Skip();
}

void CStackWindow::ClearAndCreate()
{
	m_treeCtrl->ClearAll();
	m_treeCtrl->AppendColumn("Line", wxLIST_FORMAT_LEFT, 100);
	m_treeCtrl->AppendColumn("Module", wxLIST_FORMAT_LEFT, 750);

	UpdateColumnSizes();
}

#include "mainFrame/mainFrameDesigner.h"

CStackWindow* CStackWindow::GetStackWindow()
{
	if (CDocMDIFrame::GetFrame())
		return mainFrame->GetStackWindow();
	return nullptr; 
}

CStackWindow::~CStackWindow()
{
	m_treeCtrl->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &CStackWindow::OnItemSelected, this);
}