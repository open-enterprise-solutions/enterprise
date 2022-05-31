////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : stack window
////////////////////////////////////////////////////////////////////////////

#include "stackWindow.h"
#include "compiler/debugger/debugClient.h"
#include "utils/fs/fs.h"

wxBEGIN_EVENT_TABLE(CStackWindow, wxPanel)
EVT_DEBUG(CStackWindow::OnDebugEvent)
EVT_SIZE(CStackWindow::OnSize)
wxEND_EVENT_TABLE()

wxDEFINE_EVENT(wxEVT_STACK_EVENT, CStackWindow::wxStackEvent);

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CStackWindow::wxStackEvent::wxStackEvent()
	: wxEvent(0, wxEVT_STACK_EVENT)
{
}

CStackWindow::CStackWindow(wxWindow *parent, int id) : wxPanel(parent, id), m_treeCtrl(new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT))
{
	debugClient->AddHandler(this);

	m_columnSize[0] = 0.09f;
	m_columnSize[1] = -120;

	SetSizer(new wxBoxSizer(wxHORIZONTAL));
	ClearAndCreate();
	GetSizer()->Add(m_treeCtrl, 1, wxEXPAND);

	m_treeCtrl->Bind(wxEVT_STACK_EVENT, &CStackWindow::OnStackEvent, this);
	m_treeCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &CStackWindow::OnItemSelected, this);
}

void CStackWindow::SetStack(CMemoryReader &commandReader)
{
	wxStackEvent stackEvent;
	stackEvent.SetEventObject(m_treeCtrl);

	unsigned int count = commandReader.r_u32();

	for (unsigned int i = 0; i < count; i++)
	{
		wxString moduleName;
		commandReader.r_stringZ(moduleName);
		stackEvent.AppendStack(moduleName, commandReader.r_u32());
	}

	wxPostEvent(m_treeCtrl, stackEvent);
}

void CStackWindow::UpdateColumnSizes()
{
	// We subtract two off of the size to avoid generating scroll bars on the window.
	int totalSize = GetClientSize().x - 2;

	int columnSize[2];
	GetColumnSizes(totalSize, columnSize);

	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		m_treeCtrl->SetColumnWidth(i, columnSize[i]);
	}
}

void CStackWindow::GetColumnSizes(int totalSize, int columnSize[s_numColumns]) const
{
	int fixedSize = 0;

	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		if (m_columnSize[i] < 0.0f)
		{
			columnSize[i] = static_cast<int>(-m_columnSize[i]);
			fixedSize += columnSize[i];
		}
	}

	// Set the size of the proportional columns.
	for (unsigned int i = 0; i < s_numColumns; ++i)
	{
		if (m_columnSize[i] >= 0.0f)
		{
			columnSize[i] = static_cast<int>(m_columnSize[i] * (totalSize - fixedSize) + 0.5f);
		}
	}

	// Make sure the total size is exactly correct by resizing the final column.
	for (unsigned int i = 0; i < s_numColumns - 1; ++i)
	{
		totalSize -= columnSize[i];
	}

	columnSize[s_numColumns - 1] = totalSize;
}

void CStackWindow::OnDebugEvent(wxDebugEvent& event)
{
	switch (event.GetEventId())
	{
	case EventId_LeaveLoop:
	case EventId_SessionEnd: ClearAndCreate(); break;
	}
}

bool m_initialize = false;

void CStackWindow::OnStackEvent(wxStackEvent &event)
{
	ClearAndCreate();

	for (unsigned int idx = 0; idx < event.GetStackCount(); idx++) {

		long index = m_treeCtrl->InsertItem(m_treeCtrl->GetItemCount(), event.GetModuleName(idx));

		m_treeCtrl->SetItem(index, 0, event.GetModuleLine(idx));
		m_treeCtrl->SetItem(index, 1, event.GetModuleName(idx));

		if (idx == 0) {
			m_initialize = true;
			m_treeCtrl->SetItemState(index,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
				wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
			m_initialize = false;
		}
	}

	event.Skip();
}

void CStackWindow::OnItemSelected(wxListEvent &event)
{
	if (m_initialize)
		return;

	debugClient->SetLevelStack(m_treeCtrl->GetItemCount()
		- event.GetIndex() - 1
	);
	event.Skip();
}

void CStackWindow::OnSize(wxSizeEvent & event)
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

CStackWindow::~CStackWindow()
{
	m_treeCtrl->Unbind(wxEVT_STACK_EVENT, &CStackWindow::OnStackEvent, this);
	m_treeCtrl->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &CStackWindow::OnItemSelected, this);
}