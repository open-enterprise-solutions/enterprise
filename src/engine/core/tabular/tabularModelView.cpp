#include "core/tabular/tabularModelView.h"

#include <system_error>
#include <thread>

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

// The model's own fetch thread — see ibDataViewModel::SubmitFetchAsync. A class of
// its own only so <thread> stays out of tabularModelView.h, which half the engine sees.
class ibModelFetchThread
{
public:
	~ibModelFetchThread() { Join(); }

	void Join() { if (m_thread.joinable()) m_thread.join(); }

	// One at a time: the previous read is waited out before the next starts, so a
	// model is never walked by two reads at once and the thread object is never
	// reassigned while it runs.
	void Run(std::function<void()> work)
	{
		Join();
		m_thread = std::thread(std::move(work));
	}

private:
	std::thread m_thread;
};

// The default answer to "where does this model's portion read run": on a thread of
// the model's own — one, owned, joined in the dtor and joined again before the next
// read starts, so a read can never outlive the model it is walking and two of them
// can never walk it at once.
void ibDataViewModel::SubmitFetchAsync(std::function<void()> work)
{
	if (!work)
		return;

	// Under the door's own lock (GuardFetch — one portion at a time), and behind a
	// catch: anything a read throws is caught where the work is defined (the control
	// logs it and reports the portion as failed), but an escape into std::thread
	// calls std::terminate, so this is the last line of defence.
	auto locked = GuardFetch(std::move(work));
	// A bare thread — no session, no base. The server's model binds the asker's base to it
	// (ibDataViewModelProvider); the thin client's rows are the server's, read over the protocol.
	auto guarded = [locked]() {
		try { locked(); } catch (...) { /* swallowed: the work owns its own reporting; letting it out kills the process */ }
	};

	try {
		if (!m_fetchThread)
			m_fetchThread = std::make_unique<ibModelFetchThread>();
		m_fetchThread->Run(std::move(guarded));
	}
	catch (const std::system_error&) {
		// No thread to be had (the OS refused). Read it here — a list that fills
		// while the window waits is better than a list that never fills. `guarded`
		// was moved from by then, so this runs the wrapped work directly.
		try { locked(); } catch (...) { /* swallowed: same rule as above */ }
	}
}

ibDataViewModel::~ibDataViewModel()
{
	// FIRST — a read must never outlive the model it is walking. Joining here is
	// what makes that true without any caller having to arrange it.
	m_fetchThread.reset();

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
	unsigned int /*column*/, bool ascending) const
{
	// items must be different
	wxUIntPtr id1 = wxPtrToUInt(item1.GetID()),
		id2 = wxPtrToUInt(item2.GetID());

	return ascending ? id1 - id2 : id2 - id1;
}