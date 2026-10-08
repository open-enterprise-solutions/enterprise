#include "frmclient/win/dlgs/settings/settingsConditionalAppearanceEditor.h"

#include "frmclient/win/dlgs/settings/settingsStyle.h"             // ibStyleSettingsGrid, ibSettingsArt — how a settings list looks
#include "frmclient/win/dlgs/settings/settingsFilterEditor.h"      // the CONDITION is a filter, edited by the filter's editor
#include "frmclient/win/dlgs/settings/settingsFieldTree.h"         // …and the fields are picked from the one field tree
#include "frmclient/win/dlgs/settings/settingsAppearanceEditor.h"  // …and the APPEARANCE is the appearance window
#include "frmclient/win/dlgs/textWithDotsCell.h"                   // the cell a summary and its "..." stand in
#include "frmclient/backend/system/value/composition/valueComposerSettings.h"   // ibValueEnumComparisonKind — a condition's word
#include "frmclient/backend/system/value/composition/valueComposerField.h"      // ibValueCompositionField — a picked field

#include <wx/dialog.h>
#include <wx/listbox.h>
#include <wx/sizer.h>
#include <wx/toolbar.h>

#include <utility>   // std::swap, std::move — libstdc++ does not drag it in the way MSVC does
#include <vector>

namespace {

// ⭐ A CONDITION READ OUT — what its cell shows: the switched-on lines, each side as the filter's own cell shows it
// (ibFilterSideText: a field by its whole path, a value as its field writes it), the comparison in its own word. A
// group is bracketed; an empty condition says nothing, and a rule with no condition holds for every row.
wxString ConditionText(ibFilterGroupKind kind, const std::vector<ibFilterNodeDescription>& nodes)
{
	const wxString joint = kind == ibFilterGroupKind_Or ? wxString(wxT(" OR ")) : wxString(wxT(" AND "));
	wxString text;
	for (const ibFilterNodeDescription& node : nodes) {
		if (!node.m_use)
			continue;
		wxString part;
		if (node.m_kind == ibFilterNodeKind_Group) {
			part = ConditionText(node.m_groupKind, node.m_children);
			if (part.IsEmpty())
				continue;
			part = (node.m_groupKind == ibFilterGroupKind_Not ? wxString(wxT("NOT (")) : wxString(wxT("("))) + part + wxT(")");
		}
		else {
			part = ibFilterSideText(node.m_left, node.m_right) + wxT(" ")
				+ ibValue::CreateEnumObject<ibValueEnumComparisonKind>(node.m_comparison).GetString().ToWxString();
			if (ibComparisonTakesValue(node.m_comparison))
				part += wxT(" ") + ibFilterSideText(node.m_right, node.m_left);   // «filled» asks the field alone
		}
		text += (text.IsEmpty() ? wxString() : joint) + part;
	}
	return text;
}

wxString FieldsText(const std::vector<wxString>& fields)
{
	wxString text;
	for (const wxString& field : fields)
		text += (text.IsEmpty() ? wxString() : wxString(wxT(", "))) + field;
	return text;
}

// ⭐ THE CONDITION IN A WINDOW OF ITS OWN — the filter editor itself, over a copy that lands on OK.
class ibDialogCondition : public wxDialog {
public:
	ibDialogCondition(wxWindow* parent, const ibFilterDescription& condition, ibSettingsFieldTree* fields,
		bool readOnly)
		: wxDialog(parent, wxID_ANY, _("Condition"), wxDefaultPosition, wxDefaultSize,
			wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  m_edited(condition)
	{
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		ibFilterEditor* editor = new ibFilterEditor(this, &m_edited, fields);
		editor->SetReadOnly(readOnly);
		editor->SetMinSize(FromDIP(wxSize(640, 360)));
		sizer->Add(editor, 1, wxEXPAND | wxALL, FromDIP(4));
		sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(6));
		SetSizerAndFit(sizer);
		CentreOnParent();
		// …OPEN ON EVERY GROUP when the window is shown. The editor opens itself when IT is shown (a notebook page);
		// here only the window is, and the condition came up folded shut behind one "Filter (And)" line (2026-09-30).
		Bind(wxEVT_SHOW, [this, editor](wxShowEvent& event) {
			event.Skip();
			if (event.IsShown())
				CallAfter([editor] { editor->ExpandFilterTree(); });
		});
	}
	const ibFilterDescription& GetCondition() const { return m_edited; }

private:
	ibFilterDescription m_edited;
};

// ⭐ THE FIELDS A RULE DRESSES — a list, each picked from the field tree; none = the whole row.
class ibDialogFields : public wxDialog {
public:
	ibDialogFields(wxWindow* parent, const std::vector<wxString>& fields, ibSettingsFieldTree* source, bool readOnly)
		: wxDialog(parent, wxID_ANY, _("Fields"), wxDefaultPosition, wxDefaultSize,
			wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
		  m_fields(fields), m_source(source)
	{
		wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
		wxToolBar* toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
			wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
		toolbar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
		toolbar->AddTool(wxID_ADD, _("Add"), ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add (Ins)"));
		toolbar->AddTool(wxID_REMOVE, _("Delete"), ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete (Del)"));
		toolbar->Realize();
		toolbar->Enable(!readOnly);
		sizer->Add(toolbar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));

		m_list = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(320, 220)));
		for (const wxString& field : m_fields)
			m_list->Append(field);
		sizer->Add(m_list, 1, wxEXPAND | wxALL, FromDIP(4));
		sizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, FromDIP(6));
		SetSizerAndFit(sizer);
		CentreOnParent();

		toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) {
			if (m_source == nullptr)
				return;
			ibValueCompositionField* chosen = m_source->ChooseField(this);
			if (chosen == nullptr)
				return;
			const ibValue owner(chosen);   // the picker mints the field; the value is what disposes of it
			const wxString path = chosen->GetPath();
			for (const wxString& field : m_fields)
				if (field.IsSameAs(path, false))
					return;   // named once
			m_fields.push_back(path);
			m_list->Append(path);
		}, wxID_ADD);
		toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) {
			const int at = m_list->GetSelection();
			if (at == wxNOT_FOUND)
				return;
			m_fields.erase(m_fields.begin() + at);
			m_list->Delete(static_cast<unsigned int>(at));
		}, wxID_REMOVE);
	}
	const std::vector<wxString>& GetFields() const { return m_fields; }

private:
	std::vector<wxString> m_fields;
	ibSettingsFieldTree*  m_source = nullptr;
	wxListBox*            m_list = nullptr;
};

} // namespace

// ONE LINE PER RULE — its tick, and its three parts read out.
class ibConditionalAppearanceModel : public ibDataViewVirtualListModel {
public:
	enum { kColUse = 0, kColCondition, kColFields, kColAppearance };

	explicit ibConditionalAppearanceModel(ibConditionalAppearanceEditor* editor)
		: ibDataViewVirtualListModel(RowCount(editor)), m_editor(editor) {
	}

	static unsigned RowCount(const ibConditionalAppearanceEditor* editor) {
		return editor->m_rules != nullptr ? static_cast<unsigned>(editor->m_rules->m_rules.size()) : 0;
	}
	const ibConditionalAppearanceRuleDescription* RuleAt(unsigned row) const {
		const ibConditionalAppearanceDescription* rules = m_editor->m_rules;
		return rules != nullptr && row < rules->m_rules.size() ? &rules->m_rules[row] : nullptr;
	}

	void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		const ibConditionalAppearanceRuleDescription* rule = RuleAt(row);
		if (rule == nullptr)
			return;
		if (col == kColUse)
			variant = rule->m_use;
		else if (col == kColCondition)
			variant = ConditionText(rule->m_condition.m_rootKind, rule->m_condition.m_nodes);
		else if (col == kColFields)
			variant = FieldsText(rule->m_fields);
		else if (col == kColAppearance)
			variant = ibAppearanceSummary(rule->m_appearance);
	}

	// ONLY THE TICK IS WRITTEN HERE — every other part is edited in its own window, through the cell's "...".
	bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		if (m_editor->m_readOnly || col != kColUse || m_editor->m_rules == nullptr
		 || row >= m_editor->m_rules->m_rules.size())
			return false;
		ibConditionalAppearanceRuleDescription& rule = m_editor->m_rules->m_rules[row];
		if (rule.m_use == variant.GetBool())
			return false;
		rule.m_use = variant.GetBool();
		m_editor->Changed();
		return true;
	}

	bool IsEnabledByRow(unsigned int, unsigned int) const override { return !m_editor->m_readOnly; }

private:
	ibConditionalAppearanceEditor* m_editor;
};

ibConditionalAppearanceEditor::ibConditionalAppearanceEditor(wxWindow* parent,
	ibConditionalAppearanceDescription* rules, ibSettingsFieldTree* fields, MetaOf metaOf)
	: wxPanel(parent, wxID_ANY), m_rules(rules), m_fields(fields), m_metaOf(std::move(metaOf))
{
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// THE SAME COMMANDS AS THE SORT EDITOR, on the same kind of toolbar: add, delete, and the two that make an
	// ordered list an ordered list — a later rule paints over an earlier one.
	m_toolbar = new wxToolBar(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTB_HORIZONTAL | wxTB_FLAT | wxTB_NODIVIDER);
	m_toolbar->SetToolBitmapSize(FromDIP(wxSize(16, 16)));
	m_toolbar->AddTool(wxID_ADD, _("Add"), ibSettingsArt(wxASCII_STR(wxART_NEW), this), _("Add (Ins)"));
	m_toolbar->AddTool(wxID_REMOVE, _("Delete"), ibSettingsArt(wxASCII_STR(wxART_DELETE), this), _("Delete (Del)"));
	m_toolbar->AddSeparator();
	m_toolbar->AddTool(wxID_UP, _("Move up"), ibSettingsArt(wxASCII_STR(wxART_GO_UP), this), _("Move up"));
	m_toolbar->AddTool(wxID_DOWN, _("Move down"), ibSettingsArt(wxASCII_STR(wxART_GO_DOWN), this), _("Move down"));
	m_toolbar->Realize();
	sizer->Add(m_toolbar, 0, wxLEFT | wxRIGHT | wxTOP | wxEXPAND, FromDIP(4));

	m_view = new ibDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	ibStyleSettingsGrid(m_view);
	m_model = new ibConditionalAppearanceModel(this);
	m_view->AssociateModel(m_model);

	m_view->GetRootColumnGroup()->AppendColumn(new ibDataViewColumn(wxEmptyString,
		new ibDataViewToggleRenderer(ibDataViewToggleRenderer::GetDefaultType(), wxDATAVIEW_CELL_ACTIVATABLE),
		ibConditionalAppearanceModel::kColUse, FromDIP(24), wxAlignment::wxALIGN_CENTER));
	// THE RULE AS A SENTENCE — when, what, how — each part a summary and a "..." to its own window.
	ibDataViewColumn* const conditionColumn = new ibDataViewColumn(_("Condition"),
		new ibTextWithDotsRenderer(this, [this](wxString& text) { return EditCondition(text); }, /*typed*/ false),
		ibConditionalAppearanceModel::kColCondition, FromDIP(260), wxAlignment::wxALIGN_LEFT);
	ibDataViewColumn* const fieldsColumn = new ibDataViewColumn(_("Fields"),
		new ibTextWithDotsRenderer(this, [this](wxString& text) { return EditFields(text); }, /*typed*/ false),
		ibConditionalAppearanceModel::kColFields, FromDIP(200), wxAlignment::wxALIGN_LEFT);
	ibDataViewColumn* const appearanceColumn = new ibDataViewColumn(_("Appearance"),
		new ibTextWithDotsRenderer(this, [this](wxString& text) { return EditAppearance(text); }, /*typed*/ false),
		ibConditionalAppearanceModel::kColAppearance, FromDIP(260), wxAlignment::wxALIGN_LEFT);
	m_view->GetRootColumnGroup()->AppendColumn(conditionColumn);
	m_view->GetRootColumnGroup()->AppendColumn(fieldsColumn);
	m_view->GetRootColumnGroup()->AppendColumn(appearanceColumn);
	// …AND THE THREE FILL THE LIST, sharing it as they were first sized — the appearance used to stand behind the
	// pane's right edge, to be scrolled to (Max, 2026-09-30: "by default the columns do not fit"). The tick keeps its own.
	m_view->Bind(wxEVT_SIZE, [this, conditionColumn, fieldsColumn, appearanceColumn](wxSizeEvent& e) {
		e.Skip();
		const int rest = m_view->GetClientSize().x - FromDIP(24);
		if (rest <= 0)
			return;
		conditionColumn->SetWidth(rest * 36 / 100);
		fieldsColumn->SetWidth(rest * 28 / 100);
		appearanceColumn->SetWidth(rest - rest * 36 / 100 - rest * 28 / 100);
	});

	m_view->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, [this](ibDataViewEvent& e) {
		m_view->EditItem(e.GetItem(), e.GetDataViewColumn());
		e.Skip();
	});
	sizer->Add(m_view, 1, wxEXPAND | wxALL, FromDIP(4));
	SetSizer(sizer);

	m_toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) {
		if (m_rules == nullptr || m_readOnly)
			return;
		m_rules->m_rules.emplace_back();   // switched on, holding for every row, dressing the whole row
		Reload();
		m_view->Select(m_model->GetItem(static_cast<unsigned>(m_rules->m_rules.size() - 1)));
		Changed();
	}, wxID_ADD);
	m_toolbar->Bind(wxEVT_TOOL, [this](wxCommandEvent&) {
		size_t at = 0;
		if (m_readOnly || SelectedRule(&at) == nullptr)
			return;
		m_rules->m_rules.erase(m_rules->m_rules.begin() + at);
		Reload();
		Changed();
	}, wxID_REMOVE);
	const auto move = [this](int step) {
		size_t at = 0;
		if (m_readOnly || SelectedRule(&at) == nullptr)
			return;
		const long to = static_cast<long>(at) + step;
		if (to < 0 || to >= static_cast<long>(m_rules->m_rules.size()))
			return;
		std::swap(m_rules->m_rules[at], m_rules->m_rules[static_cast<size_t>(to)]);
		Reload();
		m_view->Select(m_model->GetItem(static_cast<unsigned>(to)));
		Changed();
	};
	m_toolbar->Bind(wxEVT_TOOL, [move](wxCommandEvent&) { move(-1); }, wxID_UP);
	m_toolbar->Bind(wxEVT_TOOL, [move](wxCommandEvent&) { move(+1); }, wxID_DOWN);
}

void ibConditionalAppearanceEditor::SetConditionalAppearance(ibConditionalAppearanceDescription* rules)
{
	m_rules = rules;
	Reload();
}

void ibConditionalAppearanceEditor::SetReadOnly(bool readOnly)
{
	m_readOnly = readOnly;
	if (m_toolbar != nullptr)
		m_toolbar->Enable(!readOnly);
	if (m_view != nullptr)
		m_view->Refresh();
}

void ibConditionalAppearanceEditor::Reload()
{
	if (m_model != nullptr)
		m_model->Reset(ibConditionalAppearanceModel::RowCount(this));
}

void ibConditionalAppearanceEditor::Changed()
{
	if (m_view != nullptr)
		m_view->Refresh();
	if (m_changed)
		m_changed();
}

ibConditionalAppearanceRuleDescription* ibConditionalAppearanceEditor::SelectedRule(size_t* index)
{
	if (m_rules == nullptr || m_view == nullptr)
		return nullptr;
	const ibDataViewItem item = m_view->GetSelection();
	if (!item.IsOk())
		return nullptr;
	const unsigned row = m_model->GetRow(item);
	if (row >= m_rules->m_rules.size())
		return nullptr;
	if (index != nullptr)
		*index = row;
	return &m_rules->m_rules[row];
}

bool ibConditionalAppearanceEditor::EditCondition(wxString& text)
{
	ibConditionalAppearanceRuleDescription* rule = SelectedRule();
	if (rule == nullptr)
		return false;
	ibDialogCondition dialog(this, rule->m_condition, m_fields, m_readOnly);
	if (dialog.ShowModal() != wxID_OK || m_readOnly || dialog.GetCondition() == rule->m_condition)
		return false;
	rule->m_condition = dialog.GetCondition();
	text = ConditionText(rule->m_condition.m_rootKind, rule->m_condition.m_nodes);
	Changed();
	return true;
}

bool ibConditionalAppearanceEditor::EditFields(wxString& text)
{
	ibConditionalAppearanceRuleDescription* rule = SelectedRule();
	if (rule == nullptr)
		return false;
	ibDialogFields dialog(this, rule->m_fields, m_fields, m_readOnly);
	if (dialog.ShowModal() != wxID_OK || m_readOnly || dialog.GetFields() == rule->m_fields)
		return false;
	rule->m_fields = dialog.GetFields();
	text = FieldsText(rule->m_fields);
	Changed();
	return true;
}

bool ibConditionalAppearanceEditor::EditAppearance(wxString& text)
{
	ibConditionalAppearanceRuleDescription* rule = SelectedRule();
	if (rule == nullptr)
		return false;
	if (!ibEditAppearance(this, rule->m_appearance, m_metaOf ? m_metaOf() : nullptr, m_readOnly))
		return false;
	text = ibAppearanceSummary(rule->m_appearance);
	Changed();
	return true;
}
