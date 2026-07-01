#include "tableView.h"

// ---------------------------------------------------------
// s_constIgnoreParent — flat-scan sentinel (RESTORED)
// ---------------------------------------------------------
// The control passes this as `parent` when a FLAT List view of a hierarchical (folder-aware) source wants the
// WHOLE table in one ORDER BY — NOT the parent-scoped tree. Distinct from ibDataViewItem() (empty = "top-level
// rows only", the Tree view's root). RunComposerPage tests `parent == s_constIgnoreParent` to drop the parent
// scope. (The arc removed it betting tree-ness = grouping; but the hierarchy is an inherent queryable property
// now — GetHierarchyColumn — so the FRONTEND view mode is what says flat-vs-tree, exactly the old role.)
//
// Marker is a process-lifetime static ibDataViewObject; the const item captures its address (pointer-identity
// compare). Same TU so the marker is constructed before the item.
namespace {
	ibDataViewObject g_ignoreParentMarker;
}
const ibDataViewItem s_constIgnoreParent(&g_ignoreParentMarker);

// ---------------------------------------------------------
// ibDataViewItemAttr
// ---------------------------------------------------------

wxFont ibDataViewItemAttr::GetEffectiveFont(const wxFont& font) const
{
	if (!HasFont())
		return font;

	wxFont f(font);
	if (GetBold())
		f.MakeBold();
	if (GetItalic())
		f.MakeItalic();
	if (GetStrikethrough())
		f.MakeStrikethrough();
	return f;
}


// ---------------------------------------------------------
// ibDataViewModelNotifier
// ---------------------------------------------------------

bool ibDataViewModelNotifier::ItemsAdded(const ibDataViewItem& parent, const ibDataViewItemArray& items)
{
	size_t count = items.GetCount();
	size_t i;
	for (i = 0; i < count; i++)
		if (!ItemInserted(parent, items[i])) return false;

	return true;
}

bool ibDataViewModelNotifier::ItemsDeleted(const ibDataViewItem& parent, const ibDataViewItemArray& items)
{
	size_t count = items.GetCount();
	size_t i;
	for (i = 0; i < count; i++)
		if (!ItemDeleted(parent, items[i])) return false;

	return true;
}

bool ibDataViewModelNotifier::ItemsChanged(const ibDataViewItemArray& items)
{
	size_t count = items.GetCount();
	size_t i;
	for (i = 0; i < count; i++)
		if (!ItemChanged(items[i])) return false;

	return true;
}

// ---------------------------------------------------------
// ibDataViewModel
// ---------------------------------------------------------

ibDataViewModel::ibDataViewModel()
{
}

ibDataViewModel::~ibDataViewModel()
{
	ibDataViewModelNotifiers::const_iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		delete* iter;
	}
}

bool ibDataViewModel::ItemInserted(const ibDataViewItem& parent, const ibDataViewItem& item)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ItemInserted(parent, item))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::ItemAppended(const ibDataViewItem& parent, const ibDataViewItem& item)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ItemAppended(parent, item))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::ItemDeleted(const ibDataViewItem& parent, const ibDataViewItem& item)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ItemDeleted(parent, item))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::ItemChanged(const ibDataViewItem& item)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ItemChanged(item))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::ItemsAdded(const ibDataViewItem& parent, const ibDataViewItemArray& items)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ItemsAdded(parent, items))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::ItemsDeleted(const ibDataViewItem& parent, const ibDataViewItemArray& items)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ItemsDeleted(parent, items))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::ItemsChanged(const ibDataViewItemArray& items)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ItemsChanged(items))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::ValueChanged(const ibDataViewItem& item, unsigned int col)
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->ValueChanged(item, col))
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::Cleared()
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->Cleared())
			ret = false;
	}

	return ret;
}

#pragma region __table_notifier__h__

unsigned int ibDataViewModel::GetCurrentModelColumn(int view_id) const
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->GetCurrentModelColumn();
	return 0;
}

void ibDataViewModel::StartEditing(const ibDataViewItem& item, unsigned int col, int view_id) const
{
	if (m_notifiers.size() > 0)
		m_notifiers[view_id]->StartEditing(item, col);
}

bool ibDataViewModel::ShowListSettings(ibValueModel* model, int view_id)
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->ShowListSettings(model);
	return false;
}

bool ibDataViewModel::ShowViewMode(int view_id)
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->ShowViewMode();
	return false;
}

void ibDataViewModel::Select(const ibDataViewItem& item, int view_id) const
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->Select(item);
}

int ibDataViewModel::GetCountPerPage(int view_id) const
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->GetCountPerPage();
	return 0;
}

ibDataViewItem ibDataViewModel::GetSelection(int view_id) const
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->GetSelection();
	return ibDataViewItem(nullptr);
}

ibDataViewItem ibDataViewModel::GetDrillParent(int view_id) const
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->GetDrillParent();
	return ibDataViewItem();
}

int ibDataViewModel::GetSelections(ibDataViewItemArray& sel, int view_id) const
{
	if (m_notifiers.size() > 0)
		return m_notifiers[view_id]->GetSelections(sel);
	return 0;
}

#pragma endregion 

bool ibDataViewModel::BeforeReset()
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->BeforeReset())
			ret = false;
	}

	return ret;
}

bool ibDataViewModel::AfterReset()
{
	bool ret = true;

	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		if (!notifier->AfterReset())
			ret = false;
	}

	return ret;
}

void ibDataViewModel::Resort()
{
	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		ibDataViewModelNotifier* notifier = *iter;
		notifier->Resort();
	}
}

void ibDataViewModel::OnSortColumnChanged(unsigned int /*col*/, bool /*ascending*/)
{
	// Default: legacy behaviour — let notifiers re-sort the existing
	// rendered window. Paged models override this to update their own
	// sort state and trigger a fresh DB fetch.
	Resort();
}

void ibDataViewModel::AddNotifier(ibDataViewModelNotifier* notifier)
{
	m_notifiers.push_back(notifier);
	notifier->SetOwner(this);
}

void ibDataViewModel::RemoveNotifier(ibDataViewModelNotifier* notifier)
{
	ibDataViewModelNotifiers::iterator iter;
	for (iter = m_notifiers.begin(); iter != m_notifiers.end(); ++iter)
	{
		if (*iter == notifier)
		{
			delete notifier;
			m_notifiers.erase(iter);

			// Skip the assert below.
			return;
		}
	}

	wxFAIL_MSG(wxS("Removing non-registered notifier"));
}

int ibDataViewModel::Compare(const ibDataViewItem& item1, const ibDataViewItem& item2,
	unsigned int column, bool ascending) const
{
	wxVariant value1, value2;

	// Avoid calling GetValue() for the cells that are not supposed to have any
	// value, this might be unexpected.
	if (HasValue(item1, column))
		GetValue(value1, item1, column);
	if (HasValue(item2, column))
		GetValue(value2, item2, column);

	if (!ascending)
	{
		wxVariant temp = value1;
		value1 = value2;
		value2 = temp;
	}

	if (value1.GetType() == wxT("string"))
	{
		wxString str1 = value1.GetString();
		wxString str2 = value2.GetString();
		int res = str1.Cmp(str2);
		if (res)
			return res;
	}
	else if (value1.GetType() == wxT("long"))
	{
		long l1 = value1.GetLong();
		long l2 = value2.GetLong();
		if (l1 < l2)
			return -1;
		else if (l1 > l2)
			return 1;
	}
	else if (value1.GetType() == wxT("double"))
	{
		double d1 = value1.GetDouble();
		double d2 = value2.GetDouble();
		if (d1 < d2)
			return -1;
		else if (d1 > d2)
			return 1;
	}
#if wxUSE_DATETIME
	else if (value1.GetType() == wxT("datetime"))
	{
		wxDateTime dt1 = value1.GetDateTime();
		wxDateTime dt2 = value2.GetDateTime();
		if (dt1.IsEarlierThan(dt2))
			return -1;
		if (dt2.IsEarlierThan(dt1))
			return 1;
	}
#endif // wxUSE_DATETIME
	else if (value1.GetType() == wxT("bool"))
	{
		bool b1 = value1.GetBool();
		bool b2 = value2.GetBool();

		if (b1 != b2)
			return b1 ? 1 : -1;
	}
	else
	{
		int res = DoCompareValues(value1, value2);
		if (res != 0)
			return res;
	}


	// items must be different
	wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
		id2 = wxPtrToUInt(item2.GetID());

	return ascending ? id1 - id2 : id2 - id1;
}