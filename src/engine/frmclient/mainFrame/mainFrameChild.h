#ifndef _FRMCLIENT_MAIN_FRAME_CHILD_H__
#define _FRMCLIENT_MAIN_FRAME_CHILD_H__

#include <memory>

#include "deskgui/univ/aui/aui.h"
#include <wx/cmdproc.h>
#include <wx/utils.h>

#include "frmclient/docView/docView.h"
#include "deskgui/univ/dialog.h"

#include "frmclient/frmclient.h"

// THE TAB OF A DOCUMENT'S VIEW — the frame the doc manager's view shows in (ibFrontendMainFrame::CreateChildFrame): its
// events go to the view and the manager first, its close is the view's, and the menu bar of its view is merged with the
// main window's. It joins the notebook when it is shown, under its icon (ibAuiMDIChildFrame::Show).
class FRMCLIENT_API ibAuiDocChildFrame :
	public ibDocChildFrameAny<ibAuiMDIChildFrame, ibAuiMDIParentFrame> {
public:

	// default ctor, use Create after it
	ibAuiDocChildFrame() {}

	// ctor for a frame showing the given view of the specified document
	ibAuiDocChildFrame(ibFrontendDocument* doc,
		ibFrontendView* view,
		ibAuiMDIParentFrame* parent,
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

	bool Create(ibFrontendDocument* doc,
		ibFrontendView* view,
		ibAuiMDIParentFrame* parent,
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

	virtual bool Destroy() override {

		ibAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
		wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

		ibAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT_MSG(pClientWindow, wxT("Missing MDI Client Window"));

		if (pParentFrame->GetActiveChild() == this) {
			// deactivate ourself
			pParentFrame->SetChildMenuBar(nullptr);
		}

		bool success_destroy = false;

		if (m_docManager != nullptr) {

			ibFrontendView* view = m_docManager->GetCurrentView();
			if (view == nullptr || view == GetView() || view->GetFrame() == nullptr) {

				const int page_idx = pClientWindow->GetPageIndex(this);

				// save active window pointer
				wxWindow* new_active = nullptr;

				// find out which onscreen tab ctrl owns this tab
				ibAuiTabCtrl* tabCtrl = nullptr; int page_tab_idx = 0;
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

		ibAuiTabCtrl* tabCtrl = nullptr; int page_tab_idx = 0;
		if (pClientWindow->FindTab(this, &tabCtrl, &page_tab_idx)) {

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

	ibFrontendDocManager* m_docManager;

	wxDECLARE_CLASS(ibAuiDocChildFrame);
	wxDECLARE_NO_COPY_CLASS(ibAuiDocChildFrame);
};

class FRMCLIENT_API ibDialogDocChildFrame :
	public ibDocChildFrameAny<ibDialog, wxWindow> {
public:

	ibDialogDocChildFrame()
	{
	}

	ibDialogDocChildFrame(ibFrontendDocument* doc,
		ibFrontendView* view,
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

	virtual ~ibDialogDocChildFrame();

	// MODAL AS THE MODAL IT IS OPENED OVER: while it is shown the other windows take no input, that one among them — a
	// form shown over the list's settings left them open to be closed under it
	virtual bool Show(bool show = true) override;

	bool Create(ibFrontendDocument* doc,
		ibFrontendView* view,
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

	std::unique_ptr<wxWindowDisabler> m_winDisabler;   // the other windows, disabled while it is shown

	wxDECLARE_CLASS(ibDialogDocChildFrame);
	wxDECLARE_NO_COPY_CLASS(ibDialogDocChildFrame);
};

#endif
