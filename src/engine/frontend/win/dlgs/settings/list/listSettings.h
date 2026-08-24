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

#include "backend/compositionDescription.h"
#include "frontend/win/dlgs/settings/settingsFieldTree.h"   // ibSettingsPlainField — a field, described

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

	// ⭐⭐ ON THE BASIS OF A DESCRIPTION ALONE — the only road there is. Nothing
	// running is handed in: the caller CLONES its variant, this edits the clone in place, and on OK
	// the caller sets the clone back as the property's value (Max, 2026-08-24). The available fields
	// come from the query text the description carries, resolved in `metaData` — the same answer
	// ibQueryFieldsOfText gives the composer's window.
	//
	// ⚠ THE QUERY IS THE TEXT ITSELF here. A dynamic list keeps "use an arbitrary query" as a
	// property of its own, outside the description, so on this road the text's presence IS the flag.
	// ⭐⭐ `fields` — THE FIELDS, DESCRIBED. A description that carries a query answers "what may be
	// filtered by" out of its own text; one that carries none (a value table, a tabular section)
	// cannot, and its fields arrive as DATA instead — name, id and type, which is all a picker ever
	// needed (Max, 2026-08-24: "then describe them, you have everything for it").
	//
	// That is what keeps the runtime out of here: whoever holds the running thing says what its
	// fields ARE, and this window is handed the answer rather than the object to ask.
	// THE AUTHOR'S ROAD — the description is EDITED here: the query is part of what is being written.
	ibListSettingsPanel(wxWindow* parent, ibCompositionDescription& desc, const class ibMetaData* metaData,
		int pages = Page_All);

	// ⭐⭐ THE READER'S ROAD — THE SCHEMA IS THE OWNER, AND IT IS CONST. That is the whole of the
	// guarantee (Max, 2026-08-24: "the schema as the owner, but constant — you cannot change it,
	// only the setting"): a window holding a `const&` cannot write the configuration, and no care
	// or convention is required for that to hold.
	//
	// It is read for one thing — what fields exist — and `settings` is the only thing written. A
	// source that describes no query (a value table, a tabular section) states its fields as data
	// instead, which is what `fields` carries.
	ibListSettingsPanel(wxWindow* parent, const ibCompositionDescription& schema,
		const class ibMetaData* metaData, ibSettingsDescription& settings,
		std::vector<ibSettingsPlainField> fields = std::vector<ibSettingsPlainField>(),
		int pages = Page_All);

	~ibListSettingsPanel();   // out of line — the field tree is held by forward-declared pointer

	// COMMIT THE BUFFER ONTO THE MODEL — what OK does. Separate from the dialog so an
	// embedded panel is committed by whatever its host calls OK. FALSE means the panel
	// refused and said why (a half-written setting): the host must stay open.
	bool Commit();

protected:

	// ⭐⭐ SOMETHING CHANGED — ONE FUNCTION, and it asks whether there is a DOCUMENT behind this
	// window (Max, 2026-08-24). With one, every edit reaches it at once and the tree lights up with
	// its asterisk; without one — the SNAPSHOT road, opened from a property cell — nothing is marked
	// until OK, where setting the value raises the cascade that ends at the form attribute.
	//
	// The composer's panel carries the same verb for the same reason, so the two windows cannot
	// disagree about when a change counts as one.
	virtual void MarkModified();

	// ⭐ THE RED LINE UNDER THE QUERY — said on OPEN as well as on a change, the same as the
	// composer's window. A stored query that no longer compiles is exactly the one its author needs
	// told about before touching anything; showing it only after the first edit meant the window
	// knew and stayed quiet.
	void ShowQueryFault();

public:

	// The GROUPING — the list's own, and the one editor of the three that stayed here.
	// The part of the edited copy it stands over; never null while the panel exists.
	ibGroupDescription* GetGroupList();

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

	// ⭐ THE DESCRIPTION — and there is nothing running beside it: this panel holds no model and no
	// list. Every question it used to ask them, a description answers (Max, 2026-08-24).
	// WHAT IS READ — always set: the schema this window shows fields out of. Const, because reading
	// is all it is for.
	const ibCompositionDescription* m_schema = nullptr;

	// WHAT IS WRITTEN — set on the AUTHOR's road only, and then it is the same object as m_schema.
	// Null on the reader's road, which is what makes the schema unwritable there by construction.
	ibCompositionDescription* m_desc     = nullptr;
	const class ibMetaData*   m_metaData = nullptr;
	// ⭐⭐ THE COPY THIS DIALOG EDITS — a transaction is a copy plus an assignment. It is a SETTING and
	// nothing more (a model deals in settings; the whole schema is the list's own business), taken
	// from what is in force when the window opens and handed back on OK as the USER setting. Cancel
	// drops it: there is nothing to undo, because nothing was done.
	//
	// Each editor on the tabs is handed the PART of this copy it edits — the filter, the sort, the
	// grouping — by pointer, in its constructor. There is no object in between: what they write is
	// this setting, and what OK hands over is this setting.
	ibSettingsDescription m_edited;

	// ⭐⭐ WHAT THE EDITORS ARE POINTED AT — the setting HANDED IN when there is one, this window's own
	// buffer otherwise.
	//
	// A reader's road already arrives with a copy: the box took it, and dropping it is what Cancel
	// means there. Copying it again into `m_edited` and copying it back on OK was a third store, and
	// the third store is where the drift lives — the same three chores the composer's panel was
	// cured of on 2026-08-24 (load in, keep in step, write back).
	//
	// The AUTHOR's road has no copy of its own — it edits the description — so the buffer stays for
	// it, and there Cancel is what the buffer is FOR.
	ibSettingsDescription& EditedSettings() {
		return m_settings != nullptr ? *m_settings : m_edited;
	}

	// ⭐ THE SETTING THE CALLER HANDED IN, when it handed one. `m_edited` starts as a copy of it and,
	// on OK, is copied back — so the box that took the copy is the one that decides what to do with
	// the result. Null: the old road, where this window read the model and assigned to it itself.
	ibSettingsDescription* m_settings = nullptr;

	// The fields, as they were described to us — used when the description carries no query to read
	// them out of. Empty is a legitimate answer: nothing to filter by.
	std::vector<ibSettingsPlainField> m_plainFields;

	// Query tab (dynamic-list only) — the arbitrary query that lives OVER the list's main table. Edits
	// the list's own UseCustomQuery / CustomQuery properties (not the settings buffer). Applied AT ONCE
	// rather than on OK, because every other tab's field picker depends on it: change the query and the
	// filters, sorts and groupings must be offering the new fields before you walk over to them.
	// (⛔ THE "ARBITRARY QUERY" CHECKBOX STOOD HERE — a second spelling of "the description carries a
	//  query". See BuildQueryPage: the two disagreed, and the text won by accident.)
	// The arbitrary query, in the SAME styled editor the constructor uses — one language, one look.
	class wxStyledTextCtrl* m_queryText = nullptr;
	class wxButton* m_queryBuild = nullptr;   // opens the query constructor ON this text and writes back into it
	class wxStaticText* m_queryError = nullptr;   // the ENGINE's verdict, verbatim; hidden when the query resolves
	wxString            m_queryFault;             // what it said, kept so the line can be shown on open too

	// Push the query onto the list and rebuild everything that depends on it (the three field trees,
	// the error line). Called when the flag is toggled and when the editor loses focus.
	void ApplyQueryToList();

	// ⭐ THE TWO SHARED EDITORS, embedded as tabs. They are the composer's too — which is
	// exactly why they are not written here (settings/settingsFilterEditor.h, settingsSortEditor.h).
	class ibFilterEditor* m_filterEditor = nullptr;
	class ibSortEditor*   m_sortEditor   = nullptr;

	// WHICH FIELDS THIS THING HAS — one answer, shared by the two editors above and by
	// this panel's own tabs (settings/settingsFieldTree.h).
	std::unique_ptr<class ibSettingsFieldTree> m_fieldSource;

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
	// THE AUTHOR'S ROAD — the description is edited (its query included).
	ibDialogListSettings(wxWindow* parent, ibCompositionDescription& desc, const class ibMetaData* metaData);

	// THE READER'S ROAD — the schema is const, the setting is what changes. See the panel.
	ibDialogListSettings(wxWindow* parent, const ibCompositionDescription& schema,
		const class ibMetaData* metaData, ibSettingsDescription& settings,
		std::vector<ibSettingsPlainField> fields = std::vector<ibSettingsPlainField>());

	// ⭐⭐ THE USER'S SETTINGS OF A MODEL — ONE static door, and the composer window's twin
	// (ibDialogComposerSettings::ShowUserSettings). The whole sequence is the model's own pair: take
	// the setting in force, let the person change a copy of it, and on OK assign it back.
	//
	// ⚠ ASKED FOR BY NAME. A control used to reach this through a method OF ITS OWN
	// (ibTableViewCtrl::ShowListSettings), which put the settings road inside a widget — so "how are
	// a model's settings opened" had two answers, and only one of them was true.
	//
	// Works for ANY model: a dynamic list keeps its source-explorer field picker, anything else
	// offers its columns.
	// ⭐⭐ THE WHOLE SEQUENCE, AND IT LIVES HERE — take a COPY of what is in force, let the person
	// change it, and on OK assign that copy to the model's composer. The caller says only "show the
	// settings"; it does not carry the copy around (Max, 2026-08-24).
	//
	// The PANEL below is handed the copy and nothing else — no model. Whose setting it is, and what
	// accepting it means, is this door's business; a setting is a setting, and the panel edits one.
	static bool ShowUserSettings(wxWindow* parent, ibValueModel* model, const class ibMetaData* metaData);

	// ⭐⭐ …AND THE SNAPSHOT ROAD — the one a property cell takes. It edits a composer DESCRIPTION in
	// place and holds nothing running; the caller passes a CLONE of its variant and, on true, sets
	// that clone back as the property's value (Max, 2026-08-24). True = the snapshot differs.
	static bool ShowListSettings(ibCompositionDescription& desc, const class ibMetaData* metaData);

	ibListSettingsPanel* GetPanel() const { return m_panel; }

private:
	void Build();
	void OnOk(wxCommandEvent&);

	ibListSettingsPanel* m_panel = nullptr;
};

#endif // __LIST_SETTINGS_DLG_H__
