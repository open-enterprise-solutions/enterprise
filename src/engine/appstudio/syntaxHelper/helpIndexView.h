/////////////////////////////////////////////////////////////////////////////
// "Index" tab — alphabetic filter + filtered listbox.
//
// User types into the textbox; the list updates live via
// corpus->SearchPrefix(). Selection invokes pane's OnEntryActivated.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_INDEX_VIEW_H_
#define _IB_HELP_INDEX_VIEW_H_

#include "appstudio/syntaxHelper/helpDragSource.h"

#include "deskgui/univ/panel.h"
#include "deskgui/univ/ctrl/textCtrl.h"
#include <wx/listbox.h>

#include <memory>
#include <vector>

class ibHelpCorpus;
class ibHelpPaneView;

class ibHelpIndexView : public ibPanel {
public:
	ibHelpIndexView(wxWindow* parent, ibHelpPaneView* pane);

	void Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus);

private:
	ibHelpPaneView* m_pane    = nullptr;
	ibTextCtrl*     m_filter  = nullptr;
	ibListBox*      m_list    = nullptr;

	std::shared_ptr<const ibHelpCorpus> m_corpus;
	std::vector<wxString>               m_ids; // parallel to m_list's items

	void RefreshList(const wxString& prefix);

	void OnFilterChanged(wxCommandEvent& event);
	void OnSelection(wxCommandEvent& event);

	ibHelpDragSource m_drag;
};

#endif // _IB_HELP_INDEX_VIEW_H_
