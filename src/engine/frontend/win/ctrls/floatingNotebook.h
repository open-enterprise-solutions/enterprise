#ifndef _FLOATING_NOTEBOOK_H__
#define _FLOATING_NOTEBOOK_H__

#include <wx/aui/auibook.h>

class wxFloatingNotebook : public wxAuiNotebook {
	wxAuiManager* m_frameManager;
	wxString m_paneName;
public:

	void SetNullSelection() {
		size_t n = GetSelection();
		if (n == wxNOT_FOUND)
			return;
		wxWindow* wnd = m_tabs.GetWindowFromIdx(n);
		wxAuiTabCtrl* ctrl;
		int ctrl_idx;
		if (FindTab(wnd, &ctrl, &ctrl_idx)) {
			if (!wnd->IsShown())
				ctrl->SetActivePage(n);
			else
				ctrl->SetNoneActive();
		}
		wnd->Show(!wnd->IsShown());
	}

	wxFloatingNotebook(wxAuiManager* frameManager, const wxString& paneName,
		wxWindowID id = wxID_ANY,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxAUI_NB_DEFAULT_STYLE) :
		wxAuiNotebook(frameManager->GetManagedWindow(), id, pos, size, style), m_paneName(paneName), m_frameManager(frameManager) {
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

		wxAuiNotebookPage info;
		info.window = page;
		info.caption = caption;
		info.bitmap = bitmap;
		info.active = false;

		m_tabs.InsertPage(page, info, page_idx);

		wxAuiTabCtrl* active_tabctrl = GetActiveTabCtrl();
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

		if (select) {
			SetSelectionToWindow(page);
		}

		return true;
	}

protected:

	virtual int DoModifySelection(size_t n, bool events) {
		wxWindow* wnd = m_tabs.GetWindowFromIdx(n);
		if (wnd == nullptr)
			return m_curPage;
		bool isShown = true; size_t sel_pane = n; 
		if ((int)n == m_curPage) {
			wxAuiTabCtrl* ctrl;
			int ctrl_idx;
			if (FindTab(wnd, &ctrl, &ctrl_idx)) {
				m_curPage = sel_pane = wxNOT_FOUND;
				ctrl->SetNoneActive();
				isShown = false;
			}
			wnd->Show(false);
		}
		else if (m_curPage == wxNOT_FOUND) {
			wxAuiTabCtrl* ctrl;
			int ctrl_idx;
			if (FindTab(wnd, &ctrl, &ctrl_idx)) {
				m_curPage = sel_pane;
				ctrl->SetActivePage(n);
				isShown = true;			
			}
			wnd->Show(true);
		}

		wxAuiPaneInfo& paneInfo = m_frameManager->GetPane(m_paneName);
		if (paneInfo.IsOk()) {
			paneInfo.Resizable(isShown);
			paneInfo.Layer(1);
			if (isShown) {
				paneInfo.BestSize(GetBestSize());
			}
			else {
				paneInfo.BestSize(wxDefaultSize);
				paneInfo.Show(false);
				m_frameManager->Update();
				paneInfo.Show(true);
			}
		}

		m_frameManager->Update();
		return wxAuiNotebook::DoModifySelection(sel_pane, events);
	}
};

#endif