#ifndef __HOME_PAGE_EDITOR__
#define __HOME_PAGE_EDITOR__

// Home-page workspace editor — the designer surface of ibHomePageDescription.
//
// Reached from the config root's context menu ("Open home page workspace"), through the same
// seam the predefined-values editor uses: the metaobject asks (ibBackendMetadataTree::
// EditHomePage), the designer owns the dialog.
//
// What it edits is deliberately small: which forms the start page shows, in which column, in
// which order, with what height share, and the column template. It edits a WORKING COPY and
// commits it to the metaobject on OK, so Cancel really cancels.
//
// The runtime side that renders this is frontend/docView/templates/docViewHomePage.{h,cpp}.

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/choice.h>
#include <wx/panel.h>
#include <wx/aui/auibar.h>

#include "backend/metaCollection/metaObjectMetadata.h"

class wxButton;
class wxTreeEvent;
class wxStaticText;

class ibDialogHomePageEditor : public wxDialog {

	enum {
		wxID_TOOL_ADD = wxID_HIGHEST + 1,
		wxID_TOOL_DELETE,
		wxID_TOOL_EDIT,
		wxID_TOOL_UP,
		wxID_TOOL_DOWN,
		wxID_TOOL_TO_RIGHT,
		wxID_TOOL_TO_LEFT,
	};

public:

	ibDialogHomePageEditor(wxWindow* parent, ibValueMetaObjectConfiguration* metaObject);

protected:

	void CreateDialogView();

	// One column pane: caption + toolbar + list. The two panes differ only by which column of
	// the description they show, so they are built by the same call.
	wxWindow* CreateColumnPane(wxWindow* parent, ibHomePageColumn column, const wxString& caption);

	// Refill a list from the description (the list is a projection — the description is the
	// state), keeping the selected row where it can.
	void FillColumn(ibHomePageColumn column, long selectIndex = wxNOT_FOUND);

	// Which pane the last action came from — the toolbars share one handler.
	ibHomePageColumn ColumnOfEvent(const wxCommandEvent& event) const;
	ibHomePageColumn ColumnOfList(const wxObject* listCtrl) const;

	long GetSelection(ibHomePageColumn column) const;

	// The form metaobject an item points at, resolved LIVE (name + icon come from it); null
	// when it was deleted since — the row then says so instead of vanishing silently.
	const class ibValueMetaObjectFormBase* FindItemForm(const ibHomePageItem& item) const;

	void OnCommandMenu(wxCommandEvent& event);
	void OnItemActivated(wxListEvent& event);
	void OnItemChecked(wxListEvent& event);
	void OnTemplateChanged(wxCommandEvent& event);
	void OnCommandOK(wxCommandEvent& event);

	// The template says how many columns there ARE — the dialog shows exactly that many. A
	// switch to one column also FOLDS the right column into the left one, so what the editor
	// lists is what the start page will render; nothing keeps rendering out of a pane the
	// user can no longer see.
	void ApplyTemplateLayout(bool foldColumns);

private:

	ibValueMetaObjectConfiguration* m_metaObject;
	ibHomePageDescription m_description;   // working copy — committed on OK

	wxWindow*     m_pane[eHomePageColumn_Count] = { nullptr, nullptr };
	wxAuiToolBar* m_toolbar[eHomePageColumn_Count] = { nullptr, nullptr };
	wxListCtrl*   m_listCtrl[eHomePageColumn_Count] = { nullptr, nullptr };
	wxStaticText* m_paneCaption[eHomePageColumn_Count] = { nullptr, nullptr };

	wxChoice* m_templateChoice = nullptr;
	wxButton* m_buttonOK = nullptr;
};

// The form picker — the tree of every form the configuration owns, grouped by its owner
// metaobject, plus the common forms. Answers ONE question: which form to attach.
class ibDialogHomePageFormSelect : public wxDialog {
public:

	ibDialogHomePageFormSelect(wxWindow* parent, const ibMetaData* metaData);

	ibMetaID GetSelectedForm() const { return m_selectedForm; }

protected:

	void FillTree();
	// Push an icon into the tree's image list, -1 when there is none to push.
	int AppendIcon(const wxIcon& icon);

	void OnSelectionChanged(wxTreeEvent& event);
	void OnItemActivated(wxTreeEvent& event);

private:

	const ibMetaData* m_metaData;
	class wxTreeCtrl* m_treeCtrl = nullptr;
	wxButton* m_buttonOK = nullptr;
	ibMetaID m_selectedForm = wxNOT_FOUND;
};

#endif
