#ifndef _MAINFRAMECHILD_H__
#define _MAINFRAMECHILD_H__

#include <wx/aui/aui.h>
#include <wx/cmdproc.h>

#include "frontend/docView/docView.h"   // forked ib* doc/view (replaces wx/docview.h)

#include "frontend/frontend.h"

class FRONTEND_API ibAuiChildFrame :
	public wxAuiMDIChildFrame {
public:

	ibAuiChildFrame() : wxAuiMDIChildFrame() {}
	ibAuiChildFrame(wxAuiMDIParentFrame* parent,
		wxWindowID winid,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr))
		:
		wxAuiMDIChildFrame(parent, winid, title, pos, size, style, name) {
	}

	virtual ~ibAuiChildFrame() {}

	bool Create(wxAuiMDIParentFrame* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr)) {

		wxAuiMDIClientWindow* pClientWindow = parent->GetClientWindow();
		wxASSERT_MSG((pClientWindow != NULL), wxT("Missing MDI client window."));

		// see comment in constructor
		if (style & wxMINIMIZE)
			m_activateOnCreate = false;

		// create the window hidden to prevent flicker
		wxWindow::Show(false);
		wxWindow::Create(pClientWindow,
			id,
			wxDefaultPosition,
			size,
			wxNO_BORDER, name);

		SetMDIParentFrame(parent);

		m_title = title;
		return true;
	}

	virtual bool Show(bool show = true) override {

		wxAuiMDIClientWindow* pClientWindow = m_pMDIParentFrame->GetClientWindow();
		wxASSERT_MSG((pClientWindow != NULL), wxT("Missing MDI client window."));

		pClientWindow->Freeze();

		if (wxAuiMDIChildFrame::Show(show)) {

			const size_t idx = pClientWindow->FindPage(this);

			if (show && idx == wxNOT_FOUND) {

				const wxSize sizeIcon(wxSystemSettings::GetMetric(wxSYS_SMALLICON_X, this),
					wxSystemSettings::GetMetric(wxSYS_SMALLICON_Y, this));

				wxBitmap mdiChildIcon;
				if (m_icons.IsOk())
					mdiChildIcon.CopyFromIcon(m_icons.GetIcon(sizeIcon));
				pClientWindow->AddPage(this, m_title, m_activateOnCreate, mdiChildIcon);
			}

			wxAuiTabCtrl* tabCtrl = nullptr; int page_tab_idx = 0;
			if (pClientWindow->FindTab(this, &tabCtrl, &page_tab_idx)) {
#ifdef __WXMSW__
				tabCtrl->HandlePaint();
#endif
				tabCtrl->SetTabOffset(page_tab_idx);
			}

			// Check that the parent notion of the active child coincides with our one.
			// This is less obvious that it seems because we must honour
			// m_activateOnCreate flag but only if it's not the first child because
			// this one becomes active unconditionally.
			wxASSERT_MSG
			(
				(m_activateOnCreate || pClientWindow->GetPageCount() == 1)
				== (m_pMDIParentFrame->GetActiveChild() == this),
				wxS("Logic error: child [not] activated when it should [not] have been.")
			);
		}

		pClientWindow->Thaw();

		return true;
	}

	virtual bool Destroy() override {

		wxAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
		wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

		bool success = false;

		wxAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT_MSG(pClientWindow, wxT("Missing MDI Client Window"));

		pClientWindow->Freeze();

		if (pParentFrame->GetActiveChild() == this) {
			// deactivate ourself
			wxActivateEvent event(wxEVT_ACTIVATE, false, GetId());
			event.SetEventObject(this);
			GetEventHandler()->ProcessEvent(event);

			pParentFrame->SetChildMenuBar(nullptr);
		}

		wxAuiTabCtrl* tabCtrl = nullptr; int page_tab_idx = 0;
		if (pClientWindow->FindTab(this, &tabCtrl, &page_tab_idx)) {

			// state the window hidden to prevent flicker
			if (pClientWindow->GetPageCount() == 1)
				tabCtrl->Show(false);

			const int page_idx = pClientWindow->GetPageIndex(this);
			success = page_idx != wxNOT_FOUND ?
				pClientWindow->DeletePage(page_idx) : false;
		}

		pClientWindow->Thaw();

		return success;
	}

	virtual void SetIcons(const wxIconBundle& icons) override {

		m_icons = icons;
		wxAuiMDIChildFrame::SetIcons(icons);
	}
};

class FRONTEND_API ibAuiDocChildFrame :
	public ibDocChildFrameAny<ibAuiChildFrame, wxAuiMDIParentFrame> {
public:

	// default ctor, use Create after it
	ibAuiDocChildFrame() {}

	// ctor for a valueForm showing the given view of the specified document
	ibAuiDocChildFrame(ibDocument* doc,
		ibView* view,
		wxAuiMDIParentFrame* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr))
		:
		ibDocChildFrameAny(doc, view, parent, id, title, pos, size, style, name), m_docManager(doc ? doc->GetDocumentManager() : nullptr)
	{
	}

	virtual ~ibAuiDocChildFrame();

	bool Create(ibDocument* doc,
		ibView* view,
		wxAuiMDIParentFrame* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr))
	{
		return ibDocChildFrameAny::Create
		(
			doc, view,
			parent, id, title, pos, size, style, name
		);
	}

#if wxUSE_MENUS
	virtual void SetMenuBar(wxMenuBar* menuBar) override;
	virtual wxMenuBar* GetMenuBar() const override;
#endif // wxUSE_MENUS

	virtual void SetLabel(const wxString& label) override;

protected:

	virtual bool Destroy() {

		wxAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
		wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

		wxAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT_MSG(pClientWindow, wxT("Missing MDI Client Window"));

		pClientWindow->Freeze();

		if (pParentFrame->GetActiveChild() == this) {
			// deactivate ourself
			pParentFrame->SetChildMenuBar(nullptr);
		}

		bool success_destroy = false;

		if (m_docManager != nullptr) {

			ibView* view = m_docManager->GetCurrentView();
			if (view == nullptr || view == GetView() || view->GetFrame() == nullptr) {

				const int page_idx = pClientWindow->GetPageIndex(this);

				// save active window pointer
				wxWindow* new_active = nullptr;

				// find out which onscreen tab ctrl owns this tab
				wxAuiTabCtrl* tabCtrl = nullptr; int page_tab_idx = 0;
				if (pClientWindow->FindTab(this, &tabCtrl, &page_tab_idx)) {

					bool is_curpage = page_idx == pClientWindow->GetSelection();
					bool is_active_in_split = tabCtrl->GetPage(page_tab_idx).active;

					if (is_active_in_split) {
						if (page_tab_idx > 0 && page_tab_idx < (int)tabCtrl->GetPageCount()) {
							if (is_curpage) new_active = tabCtrl->GetWindowFromIdx(page_tab_idx - 1);
						}
					}
				}

				if (new_active == nullptr) {

					const int page_count = pClientWindow->GetPageCount();

					// we haven't yet found a new page to active,
					// so select the next page from the main tab
					// catalogue

					if (page_idx > 0 && page_idx < (int)page_count) {
						new_active = pClientWindow->GetPage(page_idx - 1);
					}

					if (new_active == nullptr && page_count > 1) {
						new_active = pClientWindow->GetPage(page_count - 1);
					}
				}

				view = nullptr;

				if (new_active != nullptr) {
					for (auto current_doc : m_docManager->GetDocumentsVector()) {
						for (auto current_view : current_doc->GetViewsVector()) {
							if (new_active == current_view->GetFrame()) {
								view = current_view;
								break;
							}
						}
					}
				}
			}

			if (view != nullptr) {
				wxWindow* frameWindow = view->GetFrame();
				if (frameWindow != nullptr) frameWindow->Raise();
			}
		}

		wxAuiTabCtrl* tabCtrl = nullptr; int page_tab_idx = 0;
		if (pClientWindow->FindTab(this, &tabCtrl, &page_tab_idx)) {

			// state the window hidden to prevent flicker
			if (pClientWindow->GetPageCount() == 1)
				tabCtrl->Show(false);

			// save active window pointer
			wxWindow* new_active = pClientWindow->GetPage(pClientWindow->GetSelection());

			pClientWindow->SetEvtHandlerEnabled(false);
			const int page_idx = pClientWindow->GetPageIndex(this);
			success_destroy = page_idx != wxNOT_FOUND ?
				pClientWindow->RemovePage(page_idx) : false;

			const int restore_selection = pClientWindow->GetPageIndex(new_active);

			if (restore_selection != page_idx)
				pClientWindow->SetSelection(restore_selection);

			pClientWindow->SetEvtHandlerEnabled(true);
		}
	
		pClientWindow->Thaw();

		// This is a child window, so we need to delete it immediately instead of
		// postponing it until idle time as we do with real TLWs.
		delete this;

		return success_destroy;
	}

#if __WXMSW__
	// override base class version to add menu bar accel processing
	virtual bool MSWTranslateMessage(WXMSG* msg) override { return false; }
#endif

private:

	ibDocManager* m_docManager;

	wxDECLARE_CLASS(ibAuiDocChildFrame);
	wxDECLARE_NO_COPY_CLASS(ibAuiDocChildFrame);
};

class FRONTEND_API ibDialogDocChildFrame :
	public ibDocChildFrameAny<wxDialog, wxWindow> {
public:

	ibDialogDocChildFrame()
	{
	}

	ibDialogDocChildFrame(ibDocument* doc,
		ibView* view,
		wxWindow* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_DIALOG_STYLE,
		const wxString& name = wxASCII_STR(wxDialogNameStr))
		: ibDocChildFrameAny(doc, view,
			parent, id, title, pos, size, style, name)
	{
	}

	bool Create(ibDocument* doc,
		ibView* view,
		wxWindow* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_DIALOG_STYLE,
		const wxString& name = wxASCII_STR(wxDialogNameStr))
	{
		return ibDocChildFrameAny::Create
		(
			doc, view,
			parent, id, title, pos, size, style, name
		);
	}

private:

	wxDECLARE_CLASS(ibDialogDocChildFrame);
	wxDECLARE_NO_COPY_CLASS(ibDialogDocChildFrame);
};
#endif 