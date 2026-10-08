////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxwidgets community
//	Description : main child window
////////////////////////////////////////////////////////////////////////////

#include "mainFrameChild.h"

wxIMPLEMENT_CLASS(ibAuiDocChildFrame, wxAuiMDIChildFrame);
wxIMPLEMENT_CLASS(ibDialogDocChildFrame, wxDialog);

#include "mainFrame.h"

ibAuiDocChildFrame::~ibAuiDocChildFrame()
{
	wxAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();

	if (pParentFrame && ibFrontendMainFrame::GetFrame()) {
		if (pParentFrame->GetActiveChild() == this) {
			pParentFrame->SetActiveChild(nullptr);
			pParentFrame->SetChildMenuBar(nullptr);
		}
		wxAuiMDIClientWindow* pClientWindow = pParentFrame->GetClientWindow();
		wxASSERT(pClientWindow);
		int idx = pClientWindow->GetPageIndex(this);
		if (idx != wxNOT_FOUND) {
			pClientWindow->RemovePage(idx);
		}
	}
}

// ⚠ DELETED WITH THE MODAL IT WAS OPENED OVER — that dialog's child, deleted as one, never closed: its view still shows
// in it. The view lets go of it and closes while it is still here — its focus watch comes off it, the form's close is
// asked of the server, which still has the tab — and goes, and with the last view the document, out of the frame's forms.
ibDialogDocChildFrame::~ibDialogDocChildFrame()
{
	ibFrontendView* const view = GetView();
	if (view == nullptr)
		return;
	view->SetDocChildFrame(nullptr);
	SetView(nullptr);
	view->Close(false);
	delete view;
}

#if wxUSE_MENUS
void ibAuiDocChildFrame::SetMenuBar(wxMenuBar* menuBar)
{
	wxMenuBar* pOldMenuBar = m_pMenuBar;
	m_pMenuBar = menuBar;

	struct ibProcSubMenu {

		// Clone every item of `dst` (source, iterated) into `src` (destination, appended to). Submenus need a
		// different append API (wxMenu::AppendSubMenu) — routing them through Append() + SetSubMenu() on a
		// wxITEM_NORMAL item leaves the submenu disconnected on MSW.
		static void ConstructMenu(wxMenu* dst, wxMenu* src) {

			for (const auto it : dst->GetMenuItems()) {

				if (it->IsSubMenu()) {
					// Recurse into a fresh submenu and attach via AppendSubMenu — that's the one wx treats as a
					// proper submenu parent across platforms.
					wxMenu* subMenu = new wxMenu;
					ConstructMenu(it->GetSubMenu(), subMenu);
					wxMenuItem* menuItem =
						src->AppendSubMenu(subMenu, it->GetItemLabel(),
							it->GetHelp());
					menuItem->Enable(it->IsEnabled());
					menuItem->SetBitmap(it->GetBitmap());
#ifdef __WXMSW__
					menuItem->SetMarginWidth(it->GetMarginWidth());
#endif
					continue;
				}

				if (it->GetKind() == wxITEM_SEPARATOR) {
					src->AppendSeparator();
					continue;
				}

				wxMenuItem* menuItem = src->Append(
					it->GetId(),
					it->GetItemLabel(),
					it->GetHelp(),
					it->GetKind()
				);

				menuItem->Enable(it->IsEnabled());

				menuItem->SetBitmap(it->GetBitmap());
#ifdef __WXMSW__
				menuItem->SetMarginWidth(it->GetMarginWidth());
#endif
				menuItem->SetHelp(it->GetHelp());
			}
		}
	};

	wxAuiMDIParentFrame* pParentFrame = GetMDIParentFrame();
	wxASSERT_MSG(pParentFrame, wxT("Missing MDI Parent Frame"));

	if (pOldMenuBar == pParentFrame->GetMenuBar())
		pParentFrame->SetChildMenuBar(nullptr);

	const bool is_active_child =
		pParentFrame->GetActiveChild() == this;

	if (m_pMenuBar != nullptr) {

		// replace current menu bars
		if (is_active_child)
			pParentFrame->SetChildMenuBar(nullptr);

		wxMenuBar* pMenuBar = pParentFrame->GetMenuBar();

		if (pMenuBar != nullptr) {

			const int pos_edit = pMenuBar->FindMenu(wxGetStockLabel(wxID_EDIT));

			for (unsigned int idx = 0; idx < pMenuBar->GetMenuCount(); idx++) {

				wxMenu* dst = pMenuBar->GetMenu(idx);
				wxMenu* src = new wxMenu;

				if (pos_edit != wxNOT_FOUND) {
					if ((int)idx > pos_edit) {
						m_pMenuBar->Append(src,
							pMenuBar->GetMenuLabel(idx)
						);
					}
					else {
						m_pMenuBar->Insert(idx, src,
							pMenuBar->GetMenuLabel(idx)
						);
					}
				}
				else {
					m_pMenuBar->Append(src,
						pMenuBar->GetMenuLabel(idx)
					);
				}

				ibProcSubMenu::ConstructMenu(dst, src);
			}
		}

		m_pMenuBar->SetParent(pParentFrame);

		if (is_active_child)
			pParentFrame->SetChildMenuBar(this);
	}
	else if (is_active_child) {
		pParentFrame->SetChildMenuBar(nullptr);
	}

	wxDELETE(pOldMenuBar);
}

wxMenuBar* ibAuiDocChildFrame::GetMenuBar() const
{
	return m_pMenuBar;
}
#endif

void ibAuiDocChildFrame::SetLabel(const wxString& label)
{
	if (label != GetLabel()) {
		wxAuiMDIChildFrame::SetLabel(label);
		wxAuiMDIChildFrame::SetTitle(label);
	}
}
