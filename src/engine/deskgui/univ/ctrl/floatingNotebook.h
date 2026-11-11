#ifndef _FLOATING_NOTEBOOK_H__
#define _FLOATING_NOTEBOOK_H__

#include "deskgui/univ/aui/auibook.h"
#include <wx/wupdlock.h>

class ibFloatingNotebook :
	public ibAuiNotebook {

public:

	void SetNullSelection() {

		size_t n = GetSelection();
		if (n == wxNOT_FOUND)
			return;

		wxWindow* wnd = m_tabs.GetWindowFromIdx(n);
		ibAuiTabCtrl* ctrl;
		int ctrl_idx;
		if (FindTab(wnd, &ctrl, &ctrl_idx)) {
			if (!wnd->IsShown())
				ctrl->SetActivePage(n);
			else
				ctrl->SetNoneActive();
		}

		wnd->Show(!wnd->IsShown());
	}

	ibFloatingNotebook(ibAuiManager* frameManager, const wxString& strPaneName,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = ibAUI_NB_DEFAULT_STYLE) :
		ibAuiNotebook(frameManager->GetManagedWindow(), id, pos, size, style), m_strPaneName(strPaneName), m_frameManager(frameManager)
	{
		GetMainTabCtrl()->Bind(wxEVT_LEFT_DOWN, &ibFloatingNotebook::OnLeftDown, this);
	}

	template <class retWindow>
	retWindow* AddPage(retWindow* page,
		const wxString& caption,
		bool select = false,
		const wxBitmapBundle& bitmap = wxBitmapBundle()) {
		if (InsertPage(GetPageCount(), page, caption, select, bitmap))
			return page;
		return nullptr;
	}

	bool InsertPage(size_t page_idx,
		wxWindow* page,
		const wxString& caption,
		bool select,
		const wxBitmapBundle& bitmap)
	{
		wxASSERT_MSG(page, wxT("page pointer must be non-nullptr"));
		if (!page)
			return false;

		page->Reparent(this);

		ibAuiNotebookPage info;
		info.window = page;
		info.caption = caption;
		info.bitmap = bitmap;
		info.active = false;

		m_tabs.InsertPage(page, info, page_idx);

		ibAuiTabCtrl* active_tabctrl = GetActiveTabCtrl();

		if (page_idx >= active_tabctrl->GetPageCount())
			active_tabctrl->AddPage(page, info);
		else
			active_tabctrl->InsertPage(page, info, page_idx);

		// Note that we don't need to call DoSizing() if the height has changed, as
		// it's already called from UpdateTabCtrlHeight() itself in this case.
		if (!UpdateTabCtrlHeight())
			DoSizing();

		active_tabctrl->DoShowHide();

		// adjust selected index
		if (m_curPage >= (int)page_idx)
			m_curPage++;

		if (select)
			SetSelectionToWindow(page);

		return true;
	}

protected:

	virtual int DoModifySelection(size_t n, bool events) {
		wxWindow* wnd = m_tabs.GetWindowFromIdx(n);
		if (wnd == nullptr)
			return m_curPage;

		// The managed frame held still for the whole collapse/expand: two layouts and the page changed are one change,
		// thawed once - without it the collapse showed a frame of the half laid out window (2026-11-11).
		wxWindowUpdateLocker freeze(m_frameManager->GetManagedWindow());

		bool isShown = true; size_t sel_pane = n;
		if ((int)n == m_curPage) {
			ibAuiTabCtrl* ctrl;
			int ctrl_idx;
			if (FindTab(wnd, &ctrl, &ctrl_idx)) {
				m_curPage = sel_pane = wxNOT_FOUND;
				ctrl->SetNoneActive();
				isShown = false;
			}
			wnd->Show(false);
		}
		else if (m_curPage == wxNOT_FOUND) {
			// the page is chosen by the base below, not here: chosen here, the base took it for the same page clicked
			// again and gave the focus to the tab strip - inside the page's own focus, which opens the notebook
			// (OnChildFocusNotebook), so the page heard it lost the focus before it heard it gained it, and kept a
			// focus it did not have (the messages' editor moving a field's caret on its every blink)
			wnd->Show(true);
		}

		ibAuiPaneInfo& paneInfo = m_frameManager->GetPane(m_strPaneName);
		if (paneInfo.IsOk()) {
			paneInfo.Resizable(isShown);
			paneInfo.Layer(1);
			if (isShown) {
				// the pages' size: ours answers none while no page is chosen, and the base chooses it below
				paneInfo.BestSize(ibAuiNotebook::DoGetBestSize());
			}
			else {
				// AUI keeps the pane at its current height unless we cycle
				// Show() around an Update(); the cycle resets the pane's
				// geometry to wxDefaultSize so it collapses down to the
				// tab strip only. Frozen above, the first Update() is never
				// seen. Since wx 3.3.4 Update()
				// stores the dock's size in its panes and a dock shown again
				// takes it back, so the remembered size goes too - else the
				// pane comes back at its expanded height, empty (2026-10-27).
				paneInfo.BestSize(wxDefaultSize);
				paneInfo.dock_size = 0;
				paneInfo.Show(false);
				m_frameManager->Update();
				paneInfo.Show(true);
			}
		}

		m_frameManager->Update();

		return ibAuiNotebook::DoModifySelection(sel_pane, events);
	}

	virtual wxSize DoGetBestSize() const {
		size_t n = GetSelection();
		if (n == wxNOT_FOUND)
			return wxSize(0, 0);
		return ibAuiNotebook::DoGetBestSize();
	}

private:

	void OnLeftDown(wxMouseEvent& event)
	{
		ibAuiTabCtrl* active_tabctrl = GetMainTabCtrl();

		auto const tabInfo =
			active_tabctrl->TabHitTest(event.GetPosition());

		int new_selection = tabInfo.pos;

		if (new_selection != wxNOT_FOUND && new_selection == active_tabctrl->GetActivePage()) {
			SetSelection(new_selection);
			return;
		}

		event.Skip();
	}

	wxString m_strPaneName;
	ibAuiManager* m_frameManager;
};

#endif