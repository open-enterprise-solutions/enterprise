/////////////////////////////////////////////////////////////////////////////
// "Search" tab — full-text search.
//
// wxSearchCtrl on top, "Found: N" badge underneath, wxListBox of
// ranked results. Drives corpus->SearchText.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_SEARCH_VIEW_H_
#define _IB_HELP_SEARCH_VIEW_H_

#include <wx/panel.h>
#include <wx/srchctrl.h>
#include <wx/stattext.h>
#include <wx/listbox.h>

#include <memory>
#include <vector>

class ibHelpCorpus;
class ibHelpPaneView;

class ibHelpSearchView : public wxPanel {
public:
	ibHelpSearchView(wxWindow* parent, ibHelpPaneView* pane);

	void Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus);

private:
	ibHelpPaneView* m_pane   = nullptr;
	wxSearchCtrl*   m_search = nullptr;
	wxStaticText*   m_badge  = nullptr;
	wxListBox*      m_list   = nullptr;

	std::shared_ptr<const ibHelpCorpus> m_corpus;
	std::vector<wxString>               m_ids;

	void RefreshList(const wxString& query);

	void OnQueryChanged(wxCommandEvent& event);
	void OnSelection(wxCommandEvent& event);
	void OnListMouseDown(wxMouseEvent& event);
	void OnListMouseMove(wxMouseEvent& event);

	wxPoint  m_dragStart;
	wxString m_dragId;
};

#endif // _IB_HELP_SEARCH_VIEW_H_
