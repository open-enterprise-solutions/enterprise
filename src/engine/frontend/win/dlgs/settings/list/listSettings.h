#ifndef __LIST_SETTINGS_DLG_H__
#define __LIST_SETTINGS_DLG_H__

#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/treectrl.h>
#include <wx/combobox.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/stc/stc.h>
#include <wx/checkbox.h>

#include <memory>

#include "backend/composition/listFilter.h"

// The tabs are model-driven grids, and the value cells are type-driven selectors — both live on the
// dataview machinery below.
#include "frontend/win/ctrls/dataview/dataview.h"

class BACKEND_API ibValueModel;
class BACKEND_API ibValueDynamicList;
class BACKEND_API ibMetaData;

// ---------------------------------------------------------------------------
// "List settings" — THE DYNAMIC LIST'S OWN settings window.
//
// ⭐ ONE OF TWO WORLDS UNDER settings/ (Max, 2026-08-20: "do not mix them — a
// dynamic list has its own grouping; what is really shared is filter and sort").
// This is the list's: its arbitrary QUERY, which of its fields are OUTPUT, and its
// GROUPING. The other world — a report's composition — is settings/composer/, and
// neither contains the other.
//
// What they share sits at the root of settings/ and is EMBEDDED here: the filter
// editor and the sort editor (each over a settings buffer), and the field tree that
// answers "which fields does this thing have". Those are the platform's only ones —
// a second filter editor would be a second set of rules about one object.
//
// ⭐ A PANEL, and a dialog AROUND it (2026-08-18): ibDialogListSettings is the modal
// wrapper the list opens — panel + OK/Cancel. Which pages appear is the embedder's
// choice.
// ---------------------------------------------------------------------------
class ibListSettingsPanel : public wxPanel {
public:
	// WHICH EDITORS this panel carries. A host asks for what it does not already
	// provide itself.
	enum Pages {
		Page_Query  = 1 << 0,   // the arbitrary-query source (dynamic list only, Designer only)
		Page_Filter = 1 << 2,   // the SHARED filter editor
		Page_Sort   = 1 << 3,   // the SHARED sort editor
		Page_Group  = 1 << 4,   // the list's OWN fold
		Page_All    = Page_Query | Page_Filter | Page_Sort | Page_Group,
	};

	// Created ON THE BASIS of a dynamic list: reads the list's source queryable for
	// the available fields and edits the list's settings (GetListSettings()).
	ibListSettingsPanel(wxWindow* parent, ibValueDynamicList* list, int pages = Page_All);

	// Created ON THE BASIS of ANY model: edits the model's settings
	// (GetListSettings()) and builds the available filter fields from the model's
	// COLUMNS (PATH A — see BuildFilterFieldsFromColumns). Used when the "Filter"
	// button is pressed on a plain table model (no dynamic-list source yet).
	ibListSettingsPanel(wxWindow* parent, ibValueModel* model, int pages = Page_All);
	~ibListSettingsPanel();   // out of line — the field tree is held by forward-declared pointer

	// COMMIT THE BUFFER ONTO THE MODEL — what OK does. Separate from the dialog so an
	// embedded panel is committed by whatever its host calls OK. FALSE means the panel
	// refused and said why (a half-written setting): the host must stay open.
	bool Commit();

	// The GROUPING — the list's own, and the one editor of the three that stayed here.
	ibValueGroupList*  GetGroupList() const;

	// THE FIELD PICKER — the available-fields tree as a form of its own. A field is a
	// VALUE (CompositionField), so choosing one is choosing a value: the same door a
	// reference opens its selection form through. Answered by the shared field tree.
	class ibValueCompositionField* ChooseField(wxWindow* parent, const wxString& currentPath = wxEmptyString);

	// RE-READ WHICH FIELDS EXIST. The panel fills its trees when its pages are built; a host that
	// edits the query elsewhere (the composition's own Query tab) says so through this, or the
	// panel keeps showing the answer as it stood when it opened — which was empty.
	void ReloadFields();

	// RE-READ THE SETTINGS the panel edits — the buffer is filled once, when the panel opens, and a
	// host that swaps what the model holds (picking another VARIANT) says so through this.
	void ReloadSettings();
private:
	class ibGroupModel;    // Group tab dataview model (over the buffer group list)

	// ---- Page construction — one builder per tab this world still owns ----
	// (Filter and Sort are the SHARED editors — built, not written, here.)
	wxWindow* BuildQueryPage(wxWindow* parent);   // FIRST tab (dynamic-list only) — arbitrary-query source
	wxWindow* BuildGroupPage(wxWindow* parent);

	// ---- Load / apply / the metadata door ----
	// Assemble the pages this panel was asked for, then load the buffer. Both ctors
	// differ only in what they bind, so the building itself is written once.
	void BuildPages();

	void LoadFromSettings();
	void ApplyToSettings();
	const ibMetaData* SourceMetaData() const;   // config that resolves reference targets (list's, else active)

	// Fill the shared field tree from whatever this panel was opened on — a source that
	// describes itself, else the model's flat columns.
	void BindFieldSource();

	// ---- Context menu — for the tabs this world still owns, routed by the view that fired ----
	void OnListContextMenu(ibDataViewEvent&);   // right-click a list row -> Add/Remove command menu

	// ---- GROUP tab. The list's own fold. ----
	size_t GroupIndexAt(const ibDataViewItem& row) const;
	void AddGroupForField(const wxTreeItemId& item);
	void OnGroupFieldActivated(wxTreeEvent&);
	void OnGroupAdd(wxCommandEvent&);
	void OnGroupRemove(wxCommandEvent&);
	void MoveGroupLine(int delta);

	// ---- Commit ----

	// The model this dialog edits. Always set (the dynamic-list ctor passes the list,
	// which IS-A ibValueModel). The field source for the Filter "Add" picker comes
	// from EITHER the model's columns (PATH A) OR the list's source explorer (PATH B).
	ibValueModel*        m_model;      // the model whose composer the dialog commits to on OK
	ibValueDynamicList*  m_list;       // non-null only on the dynamic-list path (source + composer); null for a plain model
	ibValuePtr<ibValueListSettings> m_settings;   // the dialog's OWN transactional BUFFER (load from composer on open, commit on OK)

	// Query tab (dynamic-list only) — the arbitrary query that lives OVER the list's main table. Edits
	// the list's own UseCustomQuery / CustomQuery properties (not the settings buffer). Applied AT ONCE
	// rather than on OK, because every other tab's field picker depends on it: change the query and the
	// filters, sorts and groupings must be offering the new fields before you walk over to them.
	wxCheckBox* m_queryUseCheck = nullptr;
	// The arbitrary query, in the SAME styled editor the constructor uses — one language, one look.
	class wxStyledTextCtrl* m_queryText = nullptr;
	class wxButton* m_queryBuild = nullptr;   // opens the query constructor ON this text and writes back into it
	class wxStaticText* m_queryError = nullptr;   // the ENGINE's verdict, verbatim; hidden when the query resolves

	// Push the query onto the list and rebuild everything that depends on it (the three field trees,
	// the error line). Called when the flag is toggled and when the editor loses focus.
	void ApplyQueryToList();

	// ⭐ THE TWO SHARED EDITORS, embedded as tabs. They are the composer's too — which is
	// exactly why they are not written here (settings/settingsFilterEditor.h, settingsSortEditor.h).
	class ibFilterEditor* m_filterEditor = nullptr;
	class ibSortEditor*   m_sortEditor   = nullptr;

	// WHICH FIELDS THIS THING HAS — one answer, shared by the two editors above and by
	// this panel's own tabs (settings/settingsFieldTree.h).
	std::unique_ptr<class ibSettingsFieldTree> m_fields;

	// Group — Field, model-driven.
	ibDataViewCtrl* m_groupView      = nullptr;
	ibGroupModel*   m_groupModel     = nullptr;
	wxTreeCtrl*     m_groupFieldTree = nullptr;   // Group tab — available fields (left pane)

	int m_pages = Page_All;   // which editors this panel was asked for

	// (Page_Select was DECLARED here and never written — a projection editor with four
	//  handlers and a model, none of which had a body. What is output is the composer's
	//  own question and its window asks it; a dangling half-declaration on this side only
	//  read as "the list has one too".)
};

// ---------------------------------------------------------------------------
// The modal wrapper the list itself opens: the panel plus OK / Cancel. OK commits
// the panel's buffer onto the model; Cancel drops it, which is what makes the whole
// window transactional.
// ---------------------------------------------------------------------------
class ibDialogListSettings : public wxDialog {
public:
	ibDialogListSettings(wxWindow* parent, ibValueDynamicList* list);
	ibDialogListSettings(wxWindow* parent, ibValueModel* model);

	// Opened by the designer property (ibPGDynamicListProperty) against the
	// attribute's dynamic list. OK applies the edits onto the list's composer.
	static bool ShowListSettingsDialog(ibValueDynamicList* list);

	// General entry: open the settings window for ANY model. The filter fields come
	// from the model's columns (PATH A). OK commits the panel's buffer onto the
	// model's composer (ibCommitSettingsToComposer + NotifyReset).
	static bool ShowListSettingsDialog(ibValueModel* model);

	ibListSettingsPanel* GetPanel() const { return m_panel; }

private:
	void Build();
	void OnOk(wxCommandEvent&);

	ibListSettingsPanel* m_panel = nullptr;
};

#endif // __LIST_SETTINGS_DLG_H__
