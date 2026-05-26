/////////////////////////////////////////////////////////////////////////////
// ibHelpDragSource — shared drag-from-list logic for the Index + Search
// tabs (the Tree tab uses wxEVT_TREE_BEGIN_DRAG natively).
//
// wxListBox lacks a BEGIN_DRAG event, so we approximate one:
//   1. wxEVT_LEFT_DOWN captures the press point + selected entry id.
//   2. wxEVT_MOTION starts a wxDropSource once the cursor moves past
//      the system drag threshold.
//
// The class avoids two pitfalls that bit the first cut:
//   - wxDropSource on macOS spins a modal event loop; if the help pane
//     is closed mid-drag the helper would dereference a destroyed
//     `this`. We capture all state into local variables BEFORE the
//     DoDragDrop call and touch no member afterwards.
//   - Threshold is DPI-aware via FromDIP, not a hard-coded 6 px.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_DRAG_SOURCE_H_
#define _IB_HELP_DRAG_SOURCE_H_

#include <wx/listbox.h>

#include <memory>

class ibHelpCorpus;

class ibHelpDragSource {
public:
	// `list` is the wxListBox the press happens on. `ids` is the
	// parallel vector mapping list index → entry id. `corpus` provides
	// the entry → InsertTemplate lookup at drag time. Both pointers
	// must outlive the drag source instance (typically owned by the
	// enclosing wxPanel).
	void Bind(wxListBox* list,
	          const std::vector<wxString>* ids,
	          const std::shared_ptr<const ibHelpCorpus>* corpus);

private:
	wxListBox* m_list = nullptr;
	const std::vector<wxString>*               m_ids    = nullptr;
	const std::shared_ptr<const ibHelpCorpus>* m_corpus = nullptr;

	wxPoint  m_dragStart;
	wxString m_dragId;
	bool     m_armed = false;

	void OnLeftDown(wxMouseEvent& event);
	void OnMotion(wxMouseEvent& event);
};

#endif // _IB_HELP_DRAG_SOURCE_H_
