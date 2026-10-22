////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : a notebook of a view, and its pages
////////////////////////////////////////////////////////////////////////////

#include "notebook.h"

#include "frmclient/visualView/pageWindow.h"
#include "frmclient/visualView/visualHostClient.h"   // a designer's host selects and moves by it
#include "frmclient/win/picture.h"
#include "frmclient/win/theme/luna_tabart.h"

namespace {

// Where the form put the tabs (OrientPage) - none: the notebook is a panel.
ibProtocol::ibForm::ibNotebook::ibTabLocation TabLocationOf(const ibProtocolNode& node)
{
	return static_cast<ibProtocol::ibForm::ibNotebook::ibTabLocation>(node.GetInt(ibProtocol::ibForm::ibNotebook::OrientPage,
		static_cast<long long>(ibProtocol::ibForm::ibNotebook::ibTabLocation::Top)));
}

// The desktop's style of a notebook: its tabs where the form put them, moved and scrolled; split at runtime only, as
// the desktop's (ibValueNotebook::Create) - with a split, wxAuiNotebook ends a drag inside its own tabs before it says
// DRAG_DONE, so the designer would never hear that a page was moved.
long NotebookStyleOf(const ibProtocolNode& node, const ibVisualHostClient& host)
{
	long style = (TabLocationOf(node) == ibProtocol::ibForm::ibNotebook::ibTabLocation::Bottom ? wxAUI_NB_BOTTOM : wxAUI_NB_TOP) |
		wxAUI_NB_TAB_MOVE |
		wxAUI_NB_SCROLL_BUTTONS;
	if (!host.IsDesignerHost())
		style |= wxAUI_NB_TAB_SPLIT;
	return style;
}

} // namespace

//***********************************************************************************
//*                                 Notebook                                        *
//***********************************************************************************

void ibValueNotebook::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_notebook = new wxAuiNotebook(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, NotebookStyleOf(node, GetVisualHost()));
	m_notebook->SetArtProvider(new wxAuiLunaTabArt());

	m_notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
	m_notebook->Bind(wxEVT_AUINOTEBOOK_DRAG_DONE, &ibValueNotebook::OnEndDrag, this);
}

void ibValueNotebook::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);

	m_notebook->SetWindowStyle(NotebookStyleOf(node, GetVisualHost()));
	// …and its tabs nowhere when OrientPage is None: a panel, the active page alone, switched by the server
	// (ActivePage). The designer keeps them, as the desktop's (ibValueNotebook::OnUpdated).
	const bool tabs = GetVisualHost().IsDesignerHost() ||
		TabLocationOf(node) != ibProtocol::ibForm::ibNotebook::ibTabLocation::None;
	m_notebook->SetTabCtrlHeight(tabs ? -1 : 0);

	m_activePage = state.GetInt(ibProtocol::ibForm::ibNotebook::ActivePage);
	SelectActivePage();

	UpdateWindow(m_notebook, node);
}

void ibValueNotebook::SelectActivePage()
{
	for (unsigned int i = 0; i < GetChildCount(); i++) {
		ibValueFrame* const child = GetChild(i);
		if (child->GetControlID() != m_activePage)
			continue;
		const int pos = m_notebook->GetPageIndex(child->GetWindow());
		// No event: the server chose it, the person did not.
		if (pos != wxNOT_FOUND && pos != m_notebook->GetSelection())
			m_notebook->ChangeSelection(pos);
		return;
	}
}

void ibValueNotebook::OnPageChanged(wxAuiNotebookEvent& event)
{
	event.Skip();

	wxWindow* const page = m_notebook->GetPage(event.GetSelection());
	for (unsigned int i = 0; i < GetChildCount(); i++) {
		ibValueFrame* const child = GetChild(i);
		if (child->GetWindow() != page || child->GetControlID() == m_activePage)
			continue;
		m_activePage = child->GetControlID();

		// The designer's: the page clicked is selected in its editor (the desktop's SelectControl) - nothing goes to
		// the server.
		if (GetVisualHost().IsDesignerHost()) {
			GetVisualHost().SelectControl(child);
			return;
		}

		ibProtocolNode args;
		args.SetValue(ibProtocol::ibEvent::Page, m_activePage);
		Send(ibProtocol::ibEvent::ibKind::Page, args);
		return;
	}
}

void ibValueNotebook::OnEndDrag(wxAuiNotebookEvent& event)
{
	event.Skip();

	// The designer's: the page dragged moves among the notebook's pages in the form, its editor's command (the
	// desktop's ChangeChildPosition and RefreshEditor) - the runtime's is the server's.
	if (GetVisualHost().IsDesignerHost()) {
		wxWindow* const page = m_notebook->GetPage(event.GetSelection());
		const int pos = m_notebook->GetPagePosition(event.GetSelection()).tabIdx;
		for (unsigned int i = 0; i < GetChildCount(); i++) {
			ibValueFrame* const child = GetChild(i);
			if (child->GetWindow() == page) {
				GetVisualHost().MovePosition(child, this, static_cast<unsigned int>(pos));
				break;
			}
		}
		return;
	}

	ibProtocolNode args;
	args.SetValue(ibProtocol::ibEvent::Position, static_cast<long long>(event.GetSelection()));
	Send(ibProtocol::ibEvent::ibKind::Move, args);
}

//***********************************************************************************
//*                                 Notebook page                                   *
//***********************************************************************************

void ibValueNotebookPage::Create(wxWindow* parent, const ibProtocolNode& /*node*/)
{
	m_page = new ibPanelPage(parent, wxID_ANY);
}

void ibValueNotebookPage::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocol::ibForm::ibState::State);
	ibValueNotebook* const owner = GetNotebook();
	wxAuiNotebook* const notebook = owner->m_notebook;

	// Its place among the pages that are on the notebook — those before it.
	const bool shown = state.GetBool(ibProtocol::ibForm::ibState::Visible, true);
	int pos = wxNOT_FOUND;
	if (shown) {
		pos = 0;
		for (unsigned int i = 0; i < owner->GetChildCount() && owner->GetChild(i) != this; i++) {
			if (notebook->GetPageIndex(owner->GetChild(i)->GetWindow()) != wxNOT_FOUND)
				pos++;
		}
	}

	// The notebook selects a page of its own as pages come and go — not the person's pick, not told to the server.
	wxEventBlocker blocker(notebook, wxEVT_AUINOTEBOOK_PAGE_CHANGED);

	const int was = notebook->GetPageIndex(m_page);
	if (was != wxNOT_FOUND && was != pos)
		notebook->RemovePage(was);

	// ⚠ A PAGE THAT IS NOT IN THE NOTEBOOK MUST BE HIDDEN. It was built as a CHILD of the notebook, so leaving it shown
	// does not mean "not on a tab" — it means "drawn at (0, 0)", over the notebook's tab strip (the desktop's).
	if (!shown) {
		m_page->Hide();
		return;
	}

	// The server has resolved what the tab carries: no title on a picture-only tab, no picture on a text-only one.
	const wxString title = state.GetString(ibProtocol::ibForm::ibState::Title);
	const wxBitmap picture = ibProtocolPicture(state.GetString(ibProtocol::ibForm::ibState::Picture));
	if (was != pos)
		notebook->InsertPage(pos, m_page, title, false, picture);
	notebook->SetPageText(pos, title);
	notebook->SetPageBitmap(pos, picture);

	m_page->SetOrientation(static_cast<int>(node.GetInt(ibProtocol::ibForm::ibProperty::Orient, wxVERTICAL)));

	owner->SelectActivePage();
}

void ibValueNotebookPage::OnSelected()
{
	// The desktop's: selected in the designer, or a control on it is — its notebook shows it, through the one door that
	// brings a page up (SelectActivePage), as a tab clicked in the designer makes it the active one (OnPageChanged).
	ibValueNotebook* const owner = GetNotebook();
	if (owner == nullptr)
		return;
	owner->m_activePage = GetControlID();
	owner->SelectActivePage();
}

wxWindow* ibValueNotebookPage::GetWindow() const
{
	return m_page;
}

wxSizer* ibValueNotebookPage::GetSizer() const
{
	return m_page != nullptr ? m_page->GetSizer() : nullptr;
}

ibValueNotebook* ibValueNotebookPage::GetNotebook() const
{
	return dynamic_cast<ibValueNotebook*>(GetParent());
}
