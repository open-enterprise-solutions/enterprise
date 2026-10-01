////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : stack window
////////////////////////////////////////////////////////////////////////////

#include "stackWindow.h"
#include "backend/debugger/debugClient.h"

#ifdef __WXMSW__
#include <windows.h>
#include <commctrl.h>

namespace {
// Suspends native Windows redraw on a wxListCtrl AND its header control
// (the header is a separate HWND that wxWindow::Freeze does not cover).
// Final refresh is Refresh(false) — no RDW_ERASE, so LVS_EX_DOUBLEBUFFER
// can draw off-screen without a background-erase flash.
class ibListRedrawLock {
public:
	explicit ibListRedrawLock(wxListCtrl* ctrl)
		: m_ctrl(ctrl),
		m_hwnd(static_cast<HWND>(ctrl->GetHWND())),
		m_header(m_hwnd ? reinterpret_cast<HWND>(::SendMessageW(m_hwnd, LVM_GETHEADER, 0, 0)) : nullptr)
	{
		if (m_hwnd) ::SendMessageW(m_hwnd, WM_SETREDRAW, FALSE, 0);
		if (m_header) ::SendMessageW(m_header, WM_SETREDRAW, FALSE, 0);
	}
	~ibListRedrawLock() {
		if (m_header) ::SendMessageW(m_header, WM_SETREDRAW, TRUE, 0);
		if (m_hwnd) ::SendMessageW(m_hwnd, WM_SETREDRAW, TRUE, 0);
		if (m_ctrl) m_ctrl->Refresh(false);
	}
	ibListRedrawLock(const ibListRedrawLock&) = delete;
	ibListRedrawLock& operator=(const ibListRedrawLock&) = delete;
private:
	wxListCtrl* m_ctrl;
	HWND m_hwnd;
	HWND m_header;
};
} // namespace
#endif

wxBEGIN_EVENT_TABLE(ibStackWindow, wxPanel)
EVT_SIZE(ibStackWindow::OnSize)
wxEND_EVENT_TABLE()

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

ibStackWindow::ibStackWindow(wxWindow* parent, int id) :
	wxPanel(parent, id), m_treeCtrl(new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT))
{
	// Card-style depth — panel powder-blue, list cream (matches editor).
	this->SetBackgroundColour(wxColour(184, 201, 212));   // #B8C9D4 powder-blue panel
	m_treeCtrl->SetBackgroundColour(wxColour(253, 251, 245));  // #FDFBF5 light cream list

	m_columnSize[0] = 0.09f;
	m_columnSize[1] = -120;

	SetSizer(new wxBoxSizer(wxHORIZONTAL));
	ClearAndCreate();
	m_treeCtrl->SetDoubleBuffered(true);
	GetSizer()->Add(m_treeCtrl, 1, wxEXPAND);

	m_treeCtrl->Bind(wxEVT_LIST_ITEM_ACTIVATED, &ibStackWindow::OnItemSelected, this);
}

static bool s_initialize = false;

void ibStackWindow::SetStack(const ibStackData& stackData)
{
#ifdef __WXMSW__
	ibListRedrawLock redrawLock(m_treeCtrl);
#else
	m_treeCtrl->Freeze();
#endif

	const unsigned int newCount = stackData.GetStackCount();
	const long oldCount = m_treeCtrl->GetItemCount();

	for (unsigned int idx = 0; idx < newCount; idx++) {
		long index;
		if (static_cast<long>(idx) < oldCount) {
			index = static_cast<long>(idx);
		}
		else {
			index = m_treeCtrl->InsertItem(static_cast<long>(idx), wxEmptyString);
		}
		m_treeCtrl->SetItem(index, 0, stackData.GetModuleLine(idx));
		m_treeCtrl->SetItem(index, 1, stackData.GetModuleName(idx));
		if (idx == 0) {
			const long mask = wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED;
			if ((m_treeCtrl->GetItemState(index, mask) & mask) != mask) {
				s_initialize = true;
				m_treeCtrl->SetItemState(index, mask, mask);
				s_initialize = false;
			}
		}
	}

	for (long i = oldCount - 1; i >= static_cast<long>(newCount); --i) {
		m_treeCtrl->DeleteItem(i);
	}

#ifndef __WXMSW__
	m_treeCtrl->Thaw();
#endif
}

void ibStackWindow::UpdateColumnSizes()
{
	// We subtract two off of the size to avoid generating scroll bars on the window.
	int totalSize = GetClientSize().x - 2;

	int columnSize[2];
	GetColumnSizes(totalSize, columnSize);

	for (unsigned int i = 0; i < s_numColumns; ++i) {
		m_treeCtrl->SetColumnWidth(i, columnSize[i]);
	}
}

void ibStackWindow::GetColumnSizes(int totalSize, int columnSize[s_numColumns]) const
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

void ibStackWindow::OnItemSelected(wxListEvent& event)
{
	if (s_initialize)
		return;

	debugClient->SetLevelStack(
		m_treeCtrl->GetItemCount() - event.GetIndex() - 1
	);
	
	event.Skip();
}

void ibStackWindow::OnSize(wxSizeEvent& event)
{
	UpdateColumnSizes();
	event.Skip();
}

void ibStackWindow::ClearAndCreate()
{
	m_treeCtrl->ClearAll();
	m_treeCtrl->AppendColumn(_("Line"), wxLIST_FORMAT_LEFT, 100);
	m_treeCtrl->AppendColumn(_("Module"), wxLIST_FORMAT_LEFT, 750);

	UpdateColumnSizes();
}

#include "mainFrame/mainFrameDesigner.h"

ibStackWindow* ibStackWindow::GetStackWindow()
{
	if (ibFrontendMainFrame::GetFrame())
		return mainFrame->GetStackWindow();
	return nullptr; 
}

ibStackWindow::~ibStackWindow()
{
	m_treeCtrl->Unbind(wxEVT_LIST_ITEM_ACTIVATED, &ibStackWindow::OnItemSelected, this);
}