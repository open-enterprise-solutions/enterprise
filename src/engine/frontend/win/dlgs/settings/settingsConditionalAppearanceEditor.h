#ifndef __SETTINGS_CONDITIONAL_APPEARANCE_EDITOR_H__
#define __SETTINGS_CONDITIONAL_APPEARANCE_EDITOR_H__

// ---------------------------------------------------------------------------
// THE CONDITIONAL APPEARANCE PAGE — a list of rules, each read as a sentence: WHEN its condition holds, the
// FIELDS it names wear its APPEARANCE. One page for lists and for reports, since the rules are a part of the
// setting in both (ibConditionalAppearanceDescription), pointed at the storey selected the way the filter and
// the sort editors are.
//
// Each cell's "..." opens what edits it, and nothing here is a second editor of anything: the CONDITION is the
// filter editor in a window of its own (Max, 2026-09-30: "you press the three dots and the filter opens"), the
// APPEARANCE is the appearance window a field of the report opens, the FIELDS are picked from the same field tree.
// ---------------------------------------------------------------------------

#include <functional>

#include <wx/panel.h>

#include "backend/compositionDescription.h"   // ibConditionalAppearanceDescription — what is edited here

class ibMetaData;
class ibDataViewCtrl;
class ibSettingsFieldTree;
class ibConditionalAppearanceModel;
class wxToolBar;

class ibConditionalAppearanceEditor : public wxPanel {
public:
	// `fields` — the tree a condition and the fields are picked from; `metaOf` — the configuration a text is
	// written in, asked when a window opens.
	using MetaOf = std::function<const ibMetaData*()>;
	ibConditionalAppearanceEditor(wxWindow* parent, ibConditionalAppearanceDescription* rules,
		ibSettingsFieldTree* fields, MetaOf metaOf);

	// POINTED AT ANOTHER STOREY — the node selected, or the report.
	void SetConditionalAppearance(ibConditionalAppearanceDescription* rules);
	// …and RE-READ where it stands — what the window loaded into the rules changed under it (the filter and the
	// sort editors are reloaded the same way).
	void Reload();
	void SetReadOnly(bool readOnly);
	// …and told when a person changed something, as it happens — the window marks its settings touched.
	void SetOnChanged(std::function<void()> changed) { m_changed = std::move(changed); }

private:
	ibConditionalAppearanceRuleDescription* SelectedRule(size_t* index = nullptr);
	void Changed();

	// THE THREE "..." — each opens what edits its part of the rule.
	bool EditCondition(wxString& text);
	bool EditFields(wxString& text);
	bool EditAppearance(wxString& text);

	ibConditionalAppearanceDescription* m_rules = nullptr;
	ibSettingsFieldTree*                m_fields = nullptr;
	MetaOf                              m_metaOf;
	bool                                m_readOnly = false;
	ibDataViewCtrl*                     m_view = nullptr;
	ibConditionalAppearanceModel*       m_model = nullptr;
	wxToolBar*                          m_toolbar = nullptr;
	std::function<void()>               m_changed;

	friend class ibConditionalAppearanceModel;
};

#endif
