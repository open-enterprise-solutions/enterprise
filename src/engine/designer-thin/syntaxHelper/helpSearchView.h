/////////////////////////////////////////////////////////////////////////////
// "Search" tab — full-text search.
//
// ibSearchCtrl on top, "Found: N" badge underneath, ibListBox of
// ranked results. Drives corpus->SearchText.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_SEARCH_VIEW_H_
#define _IB_HELP_SEARCH_VIEW_H_

#include "designer-thin/syntaxHelper/helpDragSource.h"

#include "gui/univ/panel.h"
#include "gui/univ/ctrl/searchCtrl.h"
#include "gui/univ/ctrl/staticText.h"
#include <wx/listbox.h>

#include <memory>
#include <vector>

class ibHelpCorpus;
class ibHelpPaneView;

class ibHelpSearchView : public ibPanel {
public:
	ibHelpSearchView(wxWindow* parent, ibHelpPaneView* pane);

	void Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus);

private:
	ibHelpPaneView* m_pane   = nullptr;
	ibSearchCtrl*   m_search = nullptr;
	ibStaticText*   m_badge  = nullptr;
	ibListBox*      m_list   = nullptr;

	std::shared_ptr<const ibHelpCorpus> m_corpus;
	std::vector<wxString>               m_ids;

	void RefreshList(const wxString& query);

	void OnQueryChanged(wxCommandEvent& event);
	void OnSelection(wxCommandEvent& event);

	ibHelpDragSource m_drag;
};

#endif // _IB_HELP_SEARCH_VIEW_H_
