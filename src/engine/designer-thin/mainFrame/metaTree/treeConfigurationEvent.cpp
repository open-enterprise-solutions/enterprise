////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : metaTree events
////////////////////////////////////////////////////////////////////////////

#include "treeConfiguration.h"

void ibMetaTreeBase::OnItemActivated(wxTreeEvent& event)
{
	// An object's row opens its editor; a group's opens and closes as any row does.
	if (GetObject(event.GetItem()) != 0)
		EditItem();
	else
		event.Skip();
}

void ibMetaTreeBase::OnSelected(wxTreeEvent& event)
{
	// …not while the panel goes: the control deletes its rows as it is destroyed, the toolbar gone before it (a tab closed).
	if (!m_drawing && !IsBeingDeleted())
		SelectItem();
	event.Skip();
}

// The inspector shows the tree the person comes to — its selection, as the desktop's tree did on its focus: a row already
// selected there is clicked without a change of selection (another tree's object, or a group's nothing, stayed shown).
void ibMetaTreeBase::OnSetFocus(wxFocusEvent& event)
{
	if (!m_drawing && !IsBeingDeleted())
		SelectItem();
	event.Skip();
}

// ⭐⭐ THE ROW THE EVENT IS ABOUT, NOT THE ONE THAT HAPPENS TO BE SELECTED — and only what the person opens: a search
// opens everything, the drawing opens what was open.
void ibMetaTreeBase::OnExpanded(wxTreeEvent& event)
{
	const ibTreeRow* const row = GetRow(event.GetItem());
	if (row != nullptr && !m_drawing && m_search.IsEmpty())
		m_expanded.insert(row->m_path);
	event.Skip();
}

void ibMetaTreeBase::OnCollapsed(wxTreeEvent& event)
{
	const ibTreeRow* const row = GetRow(event.GetItem());
	if (row != nullptr && !m_drawing && m_search.IsEmpty())
		m_expanded.erase(row->m_path);
	event.Skip();
}

void ibMetaTreeBase::OnItemMenu(wxTreeEvent& event)
{
	const wxTreeItemId item = event.GetItem();
	if (!item.IsOk())
		return;

	m_metaTreeCtrl->SelectItem(item);
	m_metaTreeCtrl->SetFocus();
	ShowContextMenu(item, event.GetPoint());
}

void ibMetaTreeBase::OnStartSearch(wxCommandEvent& event)
{
	Search(event.GetString());
	event.Skip();
}

void ibMetaTreeBase::OnCancelSearch(wxCommandEvent& event)
{
	if (event.GetString().IsEmpty())
		Search(wxEmptyString);
	event.Skip();
}
