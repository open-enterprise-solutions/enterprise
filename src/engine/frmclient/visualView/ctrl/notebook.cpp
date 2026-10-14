////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko, wxFormBuilder
//	Description : a notebook of a view, and its pages
////////////////////////////////////////////////////////////////////////////

#include "notebook.h"

#include "frmclient/visualView/pageWindow.h"
#include "frmclient/win/picture.h"
#include "frmclient/win/theme/luna_tabart.h"

namespace {

// The desktop's style of a notebook at runtime: its tabs where the form put them, moved, scrolled and split.
long NotebookStyleOf(const ibProtocolNode& node)
{
	return static_cast<long>(node.GetInt(ibProtocolName::OrientPage, wxAUI_NB_TOP)) |
		wxAUI_NB_TAB_MOVE |
		wxAUI_NB_SCROLL_BUTTONS |
		wxAUI_NB_TAB_SPLIT;
}

} // namespace

//***********************************************************************************
//*                                 Notebook                                        *
//***********************************************************************************

void ibValueNotebook::Create(wxWindow* parent, const ibProtocolNode& node)
{
	m_notebook = new wxAuiNotebook(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, NotebookStyleOf(node));
	m_notebook->SetArtProvider(new wxAuiLunaTabArt());

	m_notebook->Bind(wxEVT_AUINOTEBOOK_PAGE_CHANGED, &ibValueNotebook::OnPageChanged, this);
	m_notebook->Bind(wxEVT_AUINOTEBOOK_END_DRAG, &ibValueNotebook::OnEndDrag, this);
}

void ibValueNotebook::Update(const ibProtocolNode& node)
{
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);

	m_notebook->SetWindowStyle(NotebookStyleOf(node));

	m_activePage = state.GetInt(ibProtocolName::ActivePage);
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

		ibProtocolNode args;
		args.SetValue(ibProtocolName::Page, m_activePage);
		Send(ibProtocolEvent::Page, args);
		return;
	}
}

void ibValueNotebook::OnEndDrag(wxAuiNotebookEvent& event)
{
	event.Skip();

	ibProtocolNode args;
	args.SetValue(ibProtocolName::Position, static_cast<long long>(event.GetSelection()));
	Send(ibProtocolEvent::Move, args);
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
	const ibProtocolNode state = node.FindChild(ibProtocolName::State);
	ibValueNotebook* const owner = GetNotebook();
	wxAuiNotebook* const notebook = owner->m_notebook;

	// Its place among the pages that are on the notebook — those before it.
	const bool shown = state.GetBool(ibProtocolName::Visible, true);
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
	const wxString title = state.GetString(ibProtocolName::Title);
	const wxBitmap picture = ibProtocolPicture(state.GetString(ibProtocolName::Picture));
	if (was != pos)
		notebook->InsertPage(pos, m_page, title, false, picture);
	notebook->SetPageText(pos, title);
	notebook->SetPageBitmap(pos, picture);

	m_page->SetOrientation(static_cast<int>(node.GetInt(ibProtocolName::Orient, wxVERTICAL)));

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
