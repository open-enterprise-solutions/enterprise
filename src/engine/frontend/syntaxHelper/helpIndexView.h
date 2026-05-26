/////////////////////////////////////////////////////////////////////////////
// "Index" tab — alphabetic filter + filtered listbox.
//
// User types into the textbox; the list updates live via
// corpus->SearchPrefix(). Selection invokes pane's OnEntryActivated.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_HELP_INDEX_VIEW_H_
#define _IB_HELP_INDEX_VIEW_H_

#include "frontend/syntaxHelper/helpDragSource.h"

#include <wx/panel.h>
#include <wx/textctrl.h>
#include <wx/listbox.h>

#include <memory>
#include <vector>

class ibHelpCorpus;
class ibHelpPaneView;

class ibHelpIndexView : public wxPanel {
public:
	ibHelpIndexView(wxWindow* parent, ibHelpPaneView* pane);

	void Rebuild(const std::shared_ptr<const ibHelpCorpus>& corpus);

private:
	ibHelpPaneView* m_pane    = nullptr;
	wxTextCtrl*     m_filter  = nullptr;
	wxListBox*      m_list    = nullptr;

	std::shared_ptr<const ibHelpCorpus> m_corpus;
	std::vector<wxString>               m_ids; // parallel to m_list's items

	void RefreshList(const wxString& prefix);

	void OnFilterChanged(wxCommandEvent& event);
	void OnSelection(wxCommandEvent& event);

	ibHelpDragSource m_drag;
};

#endif // _IB_HELP_INDEX_VIEW_H_
