#ifndef _PROFILE_H__
#define _PROFILE_H__

#include "frmclient/docView/tabDocument.h"

#include "deskgui/univ/ctrl/treelistctrl.h"

#include "core/debugger/debugDefs.h"   // ibProfileData — what it shows

// WHAT A MEASUREMENT MEASURED, IN A TAB THE SERVER RAISED (Kind Profile) as it ended — Debug → Performance measurement, or
// the assistant's profile_stop / profile_read {show}: the server's document keeps it (frmserver ibProfileDocument), this one fetches it once
// and shows it. The clients' calls first, a method a row — how often, its own time (what its lines did not take), its
// time, the bytes of its requests and answers — with the lines it ran under it; then the lines of no call. A line a
// row — its module, procedure and number, how often it ran, its own time, its time with the calls it made, and what of
// it went to the database and what stayed in memory. An own time's share is of every own time measured, a call's and a
// line's. Under a line are the queries it ran and the statements the database got, each with its plan; the platform's
// own work, done for no line, is a row of its own. A column's header sorts by it, the slowest first; a line's row opens
// it. A check a level on the bar above (ibShown): a level unchecked is not drawn, what was under it goes up to the
// nearest one drawn, and the same thing met twice there is summed into one row — the SQL alone is every statement of
// the measurement, the heaviest first. A plan and the values are a statement's: with the SQL off they are not drawn.

class ibProfileView : public ibFrontendView {
public:

	ibProfileView() : ibFrontendView() {}

	virtual bool OnCreate(ibFrontendDocument* doc, long flags) override;
	virtual void OnDraw(wxDC* dc) override;

private:

	// The levels of the tree, a check each (Max, 2026-11-15: *"a filter with checks — show the runtime, the SQL and so
	// on"*): bits of m_shown.
	enum ibShown {
		ShowCalls,
		ShowLines,
		ShowQueries,
		ShowStatements,
		ShowPlans,
		ShowValues,
	};

	// The levels shown, in the order asked for, written into the tree with what they ran under them; a row that was open
	// stays open.
	void Fill();

	void OnColumnClick(wxListEvent& event);
	void OnItemActivated(wxTreeEvent& event);
	void OnShown(wxCommandEvent& event);

	ibTreeListCtrl* m_tree = nullptr;
	wxTreeItemId    m_root;
	ibProfileData   m_profile;
	std::int64_t    m_spent = 0;            // every own time measured — a call's and a line's — added
	unsigned        m_shown = ~0u;          // the levels shown (ibShown) — all at first

	// What every level is sorted by: a column (as the tree's) and which way.
	int  m_sortColumn = 0;
	bool m_ascending = false;
};

// ----------------------------------------------------------------------------
// ibProfileDocument: the server's measurement tab and its tree married
// ----------------------------------------------------------------------------

class ibProfileDocument : public ibTabDocument {
public:

	explicit ibProfileDocument(long long tabId) : ibTabDocument(tabId) {}

protected:

	virtual ibFrontendView* DoCreateView() override;

	wxDECLARE_NO_COPY_CLASS(ibProfileDocument);
};

#endif
