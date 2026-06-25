#include "frontend/win/dlgs/listSettings/listSettings.h"
#include "backend/metaCollection/metaObjectComposite.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"

#include "backend/metadataConfiguration.h"
#include "backend/objCtor.h"

#include "frontend/win/ctrls/controlTextEditor.h"
#include "frontend/visualView/ctrl/typeControl.h"
#include "frontend/visualView/ctrl/frame.h"

#include "backend/system/value/valueType.h"
#include "backend/metaCollection/partial/list/dynamicList.h"   // ibValueDynamicList — the dialog is built on it
#include "backend/query/queryable.h"                            // ibBackendQueryable::GetColumns
#include "backend/query/queryColumn.h"                          // ibBackendQueryColumn

#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/app.h>

// Filter-tab column ids (model columns). Mirrors wxFilterDialog's enum in
// tableView.cpp: Use / Name / Comparison / Value.
enum {
	eFilterUse = 0,
	eFilterField,
	eFilterComparison,
	eFilterValue
};

// ---------------------------------------------------------------------------
//  Filter model — virtual-list over the runtime ibValueFilterList.
//
//  Same shape as wxDataViewFilterModel (tableView.cpp), but instead of a copied
//  ibFilterRow it edits the LIVE ibValueFilterList held by the dialog's
//  ibValueListSettings. GetValueByRow / SetValueByRow walk Count()/GetItem(i)
//  and the FilterItem getters/setters. Row tag is 1-based (GetID()).
// ---------------------------------------------------------------------------
class ibDialogListSettings::ibFilterModel : public ibDataViewVirtualListModel {
	ibDialogListSettings* m_dialog;
public:
	explicit ibFilterModel(ibDialogListSettings* dialog)
		: ibDataViewVirtualListModel(), m_dialog(dialog) {
	}

	ibValueFilterList* GetFilter() const { return m_dialog->GetFilterList(); }

	void ResetFromList() {
		ibValueFilterList* filter = GetFilter();
		Reset(filter != nullptr ? (unsigned int)filter->Count() : 0u);
	}

	virtual void GetValueByRow(wxVariant& variant,
		unsigned row, unsigned col) const override {
		ibValueFilterList* filter = GetFilter();
		if (filter == nullptr)
			return;
		ibValueFilterItem* item = filter->GetItem(row);
		if (item == nullptr)
			return;
		if (col == eFilterUse) {
			variant = item->GetUse();
		}
		else if (col == eFilterField) {
			variant = item->GetField();
		}
		else if (col == eFilterComparison) {
			variant = (long)item->GetComparison();
		}
		else if (col == eFilterValue) {
			variant = item->GetFilterValue().GetString();
		}
	}

	virtual bool SetValueByRow(const wxVariant& variant,
		unsigned row, unsigned col) override {
		ibValueFilterList* filter = GetFilter();
		if (filter == nullptr)
			return false;
		ibValueFilterItem* item = filter->GetItem(row);
		if (item == nullptr)
			return false;

		if (col == eFilterUse) {
			item->SetUse(variant.GetBool());
			return true;
		}
		else if (col == eFilterComparison) {
			// NB: ibValueFilterItem has no SetComparison() — it is set at Add
			// time. Editing the comparison in-place would need a setter on the
			// backend FilterItem.
			// TODO(spike): add ibValueFilterItem::SetComparison(ibComparisonKind)
			// and dispatch here so the comparison column becomes editable.
			return false;
		}
		else if (col == eFilterValue) {
			// Value editing goes THROUGH the runtime — same path as
			// wxDataViewFilterModel::SetValueByRow. The text typed into the
			// editor is resolved against a freshly created value of the row's
			// class, then coerced to the field's type via AdjustValue.
			const ibValue& selValue = item->GetFilterValue();
			const ibValue& newValue = activeMetaData->CreateObject(selValue.GetClassType());
			const wxString& strData = variant.GetString();
			if (strData.Length() > 0) {
				std::vector<ibValue> listValue;
				if (newValue.FindValue(strData, listValue)) {
					item->SetFilterValue(ibValueTypeDescription::AdjustValue(
						item->GetTypeDescription(), listValue.at(0)
					));
				}
				else {
					return false;
				}
			}
			else {
				item->SetFilterValue(ibValueTypeDescription::AdjustValue(
					item->GetTypeDescription(), newValue
				));
			}
			item->SetUse(true);
			return true;
		}
		return false;
	}
};

// ---------------------------------------------------------------------------
//  Filter value renderer — the runtime editor for the Value column.
//
//  1:1 port of wxValueViewRenderer (tableView.cpp), retargeted from
//  ibFilterRow::ibFilterData onto our ibValueFilterItem. It is both a
//  ibDataViewCustomRenderer (draws the value string, hosts the editor) and an
//  ibControlFrame (so QuickChoice / ProcessChoice can push the chosen value
//  back through GetControlValue / SetControlValue / ChoiceProcessing).
//
//  The editor is an ibControlTextEditor with a Select ("...") and Clear button.
//  Pressing Select:
//    - empty value  -> ibTypeControlFactory::ShowSelectType + CreateObject(clsid)
//    - non-empty    -> ibTypeControlFactory::QuickChoice (primitive/enum/quick-ref)
//                      else metaObject->ProcessChoice (full selection form)
//  The selection comes back via ChoiceProcessing -> SetFilterValue + Use=true.
// ---------------------------------------------------------------------------
class ibDialogListSettings::ibFilterValueRenderer : public ibDataViewCustomRenderer,
	public ibControlFrame {
	ibDialogListSettings* m_dialog;
public:

	explicit ibFilterValueRenderer(ibDialogListSettings* dialog)
		: ibDataViewCustomRenderer(wxT("string"), wxDATAVIEW_CELL_EDITABLE, wxALIGN_LEFT), m_dialog(dialog) {
	}

	void FinishSelecting() {
		if (m_editorCtrl != nullptr) {
			// Remove our event handler first to prevent it from (recursively)
			// calling us again via FinishEditing() when the editor loses focus
			// when we hide it below.
			wxEvtHandler* const handler = m_editorCtrl->PopEventHandler();

			// Hide the control immediately but don't delete it yet as there
			// could be some pending messages for it.
			m_editorCtrl->Hide();

			wxPendingDelete.Append(handler);
			wxPendingDelete.Append(m_editorCtrl);

			// Ensure DestroyEditControl() is not called again for this control.
			m_editorCtrl.Release();
		}

		DoHandleEditingDone(nullptr);
	}

	virtual bool Render(wxRect rect, wxDC* dc, int state) override {
		RenderText(m_valueVariant, 0, rect, dc, state);
		return true;
	}

	virtual bool ActivateCell(const wxRect& cell,
		ibDataViewModel* model,
		const ibDataViewItem& item,
		unsigned int col,
		const wxMouseEvent* mouseEvent) override {
		return false;
	}

	virtual wxSize GetSize() const override {
		if (!m_valueVariant.IsNull()) {
			return GetTextExtent(m_valueVariant);
		}
		else {
			return GetView()->FromDIP(wxSize(wxDVC_DEFAULT_RENDERER_SIZE,
				wxDVC_DEFAULT_RENDERER_SIZE));
		}
	}

	virtual bool SetValue(const wxVariant& value) override {
		m_valueVariant = value.GetString();
		return true;
	}

	virtual bool GetValue(wxVariant& WXUNUSED(value)) const override {
		return true;
	}

#if wxUSE_ACCESSIBILITY
	virtual wxString GetAccessibleDescription() const override {
		return m_valueVariant;
	}
#endif // wxUSE_ACCESSIBILITY

	virtual bool HasEditorCtrl() const override {
		return true;
	}

	virtual wxWindow* CreateEditorCtrl(wxWindow* dv,
		wxRect labelRect,
		const wxVariant& value) override {

		ibControlTextEditor* textEditor = new ibControlTextEditor;
		textEditor->SetDVCMode(true);

		// create the window hidden to prevent flicker
		textEditor->Show(false);

		bool result = textEditor->Create(dv, wxID_ANY, value,
			labelRect.GetPosition(),
			labelRect.GetSize());

		if (!result)
			return nullptr;

		textEditor->ShowSelectButton(true);
		textEditor->ShowClearButton(true);
		textEditor->ShowOpenButton(false);

		ibDataViewCtrl* parentWnd = dynamic_cast<ibDataViewCtrl*>(dv->GetParent());
		if (parentWnd != nullptr) {
			textEditor->SetBackgroundColour(parentWnd->GetBackgroundColour());
			textEditor->SetForegroundColour(parentWnd->GetForegroundColour());
			textEditor->SetFont(parentWnd->GetFont());
		}
		else {
			textEditor->SetBackgroundColour(dv->GetBackgroundColour());
			textEditor->SetForegroundColour(dv->GetForegroundColour());
			textEditor->SetFont(dv->GetFont());
		}

		textEditor->SetPasswordMode(false);
		textEditor->SetMultilineMode(false);
		textEditor->SetTextEditMode(true);

		textEditor->Bind(wxEVT_CONTROL_BUTTON_SELECT, &ibFilterValueRenderer::OnSelectButtonPressed, this);
		textEditor->Bind(wxEVT_CONTROL_BUTTON_CLEAR, &ibFilterValueRenderer::OnClearButtonPressed, this);

		textEditor->LayoutControls();
		textEditor->Show(true);

		textEditor->SetInsertionPointEnd();
		return textEditor;
	}

	virtual bool GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override {
		ibControlTextEditor* textEditor = wxDynamicCast(ctrl, ibControlTextEditor);
		if (textEditor == nullptr)
			return false;
		value = textEditor->GetValue();
		return true;
	}

	// Counter reference — the renderer is not ref-counted (owned by the column).
	virtual void ControlIncrRef() {}
	virtual void ControlDecrRef() {}

public:

	virtual bool GetControlValue(ibValue& pvarControlVal) const {
		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return false;
		pvarControlVal = item->GetFilterValue();
		return true;
	}

	virtual bool SetControlValue(const ibValue& varValue) const {
		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return false;
		item->SetFilterValue(varValue);
		return true;
	}

	virtual bool HasQuickChoice() const {
		ibValue selValue; GetControlValue(selValue);
		const ibCtorAbstractType* so = activeMetaData->GetAvailableCtor(selValue.GetClassType());
		if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_primitive) {
			return true;
		}
		else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_enum) {
			return true;
		}
		else if (so != nullptr && so->GetObjectTypeCtor() == ibCtorObjectType_object_meta_value) {
			const ibCtorMetaValueType* meta_so = dynamic_cast<const ibCtorMetaValueType*>(so);
			if (meta_so != nullptr) {
				const ibValueMetaObjectRecordDataRef* metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(meta_so->GetMetaObject());
				if (metaObject != nullptr)
					return metaObject->HasQuickChoice();
			}
		}
		return false;
	}

private:

	// The currently-selected filter item, or nullptr. Row tag is 1-based.
	ibValueFilterItem* GetSelectedItem() const {
		const ibDataViewItem& item = m_dialog->GetFilterView()->GetSelection();
		if (!item.IsOk())
			return nullptr;
		size_t index = reinterpret_cast<size_t>(item.GetID());
		ibValueFilterList* filter = m_dialog->GetFilterList();
		if (filter == nullptr || index == 0)
			return nullptr;
		return filter->GetItem(index - 1);
	}

	//events
	void OnSelectButtonPressed(wxCommandEvent& event) {
		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return;
		if (item->GetFilterValue().GetType() == ibValueTypes::TYPE_EMPTY) {
			const ibClassID& clsid = ibTypeControlFactory::ShowSelectType(activeMetaData,
				item->GetTypeDescription()
			);
			if (clsid != 0 && activeMetaData->IsRegisterCtor(clsid)) {
				item->SetFilterValue(activeMetaData->CreateObject(clsid));
			}
			return;
		}
		const ibClassID& clsid = item->GetFilterValue().GetClassType();
		if (!ibTypeControlFactory::QuickChoice(this, clsid, GetEditorCtrl())) {
			const ibCtorMetaValueType* singleValue = activeMetaData->GetTypeCtor(clsid);
			if (singleValue != nullptr) {
				const ibValueMetaObject* metaObject = singleValue->GetMetaObject();
				wxASSERT(metaObject);
				const ibValueMetaObjectAttributeBase* attribute = activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase>(item->GetModel(), true);

				ibSelectMode selMode = ibSelectMode::ibSelectMode_Items;
				if (attribute != nullptr)
					selMode = attribute->GetSelectMode();
				metaObject->ProcessChoice(this, wxEmptyString, selMode);
			}
		}
	}

	void OnClearButtonPressed(wxCommandEvent& event) {
		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return;
		item->SetFilterValue(ibValueTypeDescription::AdjustValue(item->GetTypeDescription()));
		item->SetUse(true);
		FinishSelecting();
	}

	virtual void ChoiceProcessing(ibValue& vSelected) {
		ibValueFilterItem* item = GetSelectedItem();
		if (item == nullptr)
			return;
		item->SetFilterValue(vSelected);
		item->SetUse(true);
		FinishSelecting();
	}

private:
	wxVariant m_valueVariant;
};

// ---------------------------------------------------------------------------
//  Construction
// ---------------------------------------------------------------------------

ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibValueDynamicList* list)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(540, 440),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  m_list(list), m_settings(list != nullptr ? list->GetListSettings() : nullptr)
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	notebook->AddPage(BuildFilterPage(notebook), _("Filter"), true);
	notebook->AddPage(BuildOrderPage(notebook),  _("Sort"));
	notebook->AddPage(BuildGroupPage(notebook),  _("Group"));
	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, FromDIP(6));

	wxStdDialogButtonSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	mainSizer->Add(btns, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, FromDIP(6));

	SetSizer(mainSizer);

	LoadFromSettings();

	Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOk, this, wxID_OK);
}

ibValueFilterList* ibDialogListSettings::GetFilterList() const
{
	return m_settings != nullptr ? m_settings->GetFilter() : nullptr;
}

// ---------------------------------------------------------------------------
//  Pages
// ---------------------------------------------------------------------------

wxWindow* ibDialogListSettings::BuildFilterPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	// Runtime-driven dataview: Use (toggle) / Field / Comparison (choice) /
	// Value (custom runtime renderer). Same column set as wxFilterDialog.
	m_filterView = new ibDataViewCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_filterView->SetBackgroundColour(panel->GetBackgroundColour());
	m_filterView->SetForegroundColour(panel->GetForegroundColour());

	m_filterView->Bind(wxEVT_DATAVIEW_ITEM_ACTIVATED, &ibDialogListSettings::OnFilterItemActivated, this);

	// Comparison choices — order MUST match enum ibComparisonKind.
	wxArrayString cmpChoices;
	cmpChoices.push_back(_("Equal"));
	cmpChoices.push_back(_("Not equal"));
	cmpChoices.push_back(_("Greater"));
	cmpChoices.push_back(_("Less"));
	cmpChoices.push_back(_("Greater or equal"));
	cmpChoices.push_back(_("Less or equal"));
	cmpChoices.push_back(_("Contains"));

	m_filterView->AppendToggleColumn(_("Use"), eFilterUse,
		wxDATAVIEW_CELL_ACTIVATABLE, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_filterView->AppendTextColumn(_("Field"), eFilterField,
		wxDATAVIEW_CELL_INERT, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);

	ibDataViewColumn* cmpColumn = new ibDataViewColumn(_("Comparison"),
		new ibDataViewChoiceByIndexRenderer(cmpChoices, wxDATAVIEW_CELL_EDITABLE, wxAlignment::wxALIGN_LEFT),
		eFilterComparison,
		wxNOT_FOUND,
		wxAlignment::wxALIGN_LEFT
	);
	m_filterView->AppendColumn(cmpColumn);

	ibDataViewColumn* valColumn = new ibDataViewColumn(_("Value"),
		new ibFilterValueRenderer(this),
		eFilterValue,
		wxNOT_FOUND,
		wxAlignment::wxALIGN_LEFT
	);
	m_filterView->AppendColumn(valColumn);

	sizer->Add(m_filterView, 1, wxALL | wxEXPAND, FromDIP(4));

	// Add / Remove a filter row. The new row's FIELD is picked here (selecting
	// the field is what fixes the row's type — Use / Comparison / Value are
	// then edited inside the row).
	wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
	m_filterAddField = new wxComboBox(panel, wxID_ANY);
	FillFieldChoice(m_filterAddField);
	wxButton* addBtn = new wxButton(panel, wxID_ANY, _("Add"));
	wxButton* delBtn = new wxButton(panel, wxID_ANY, _("Remove"));

	row->Add(m_filterAddField, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
	row->Add(addBtn,           0, wxALL, FromDIP(2));
	row->Add(delBtn,           0, wxALL, FromDIP(2));
	sizer->Add(row, 0, wxEXPAND);

	addBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnFilterAdd, this);
	delBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnFilterRemove, this);

	// Model edits the live ibValueFilterList directly (no separate apply).
	m_filterModel = new ibFilterModel(this);
	m_filterView->AssociateModel(m_filterModel);

	panel->SetSizer(sizer);
	return panel;
}

wxWindow* ibDialogListSettings::BuildOrderPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	m_orderList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_orderList->AppendColumn(_("Field"),     wxLIST_FORMAT_LEFT, FromDIP(260));
	m_orderList->AppendColumn(_("Direction"), wxLIST_FORMAT_LEFT, FromDIP(150));
	sizer->Add(m_orderList, 1, wxALL | wxEXPAND, FromDIP(4));

	wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
	m_orderField = new wxComboBox(panel, wxID_ANY);
	FillFieldChoice(m_orderField);
	m_orderDir = new wxChoice(panel, wxID_ANY);
	// Order MUST match enum ibSortDirection.
	m_orderDir->Append(_("Ascending"));
	m_orderDir->Append(_("Descending"));
	m_orderDir->SetSelection(0);
	wxButton* addBtn = new wxButton(panel, wxID_ANY, _("Add"));
	wxButton* delBtn = new wxButton(panel, wxID_ANY, _("Remove"));

	row->Add(m_orderField, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
	row->Add(m_orderDir,   0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
	row->Add(addBtn,       0, wxALL, FromDIP(2));
	row->Add(delBtn,       0, wxALL, FromDIP(2));
	sizer->Add(row, 0, wxEXPAND);

	addBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOrderAdd, this);
	delBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOrderRemove, this);

	panel->SetSizer(sizer);
	return panel;
}

wxWindow* ibDialogListSettings::BuildGroupPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	m_groupList = new wxListCtrl(panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_SINGLE_SEL);
	m_groupList->AppendColumn(_("Field"), wxLIST_FORMAT_LEFT, FromDIP(420));
	sizer->Add(m_groupList, 1, wxALL | wxEXPAND, FromDIP(4));

	wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
	m_groupField = new wxComboBox(panel, wxID_ANY);
	FillFieldChoice(m_groupField);
	wxButton* addBtn = new wxButton(panel, wxID_ANY, _("Add"));
	wxButton* delBtn = new wxButton(panel, wxID_ANY, _("Remove"));

	row->Add(m_groupField, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
	row->Add(addBtn,       0, wxALL, FromDIP(2));
	row->Add(delBtn,       0, wxALL, FromDIP(2));
	sizer->Add(row, 0, wxEXPAND);

	addBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnGroupAdd, this);
	delBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnGroupRemove, this);

	panel->SetSizer(sizer);
	return panel;
}

// ---------------------------------------------------------------------------
//  Available fields (from the source metaobject)
// ---------------------------------------------------------------------------

void ibDialogListSettings::FillFieldChoice(wxComboBox* choice)
{
	const ibBackendQueryable* q = (m_list != nullptr) ? m_list->GetSourceQueryable() : nullptr;
	if (q == nullptr || choice == nullptr)
		return;
	// Available fields = the SOURCE QUERYABLE's columns (NOT metaobject attributes).
	for (const ibBackendQueryColumn* col : q->GetColumns()) {
		if (col != nullptr)
			choice->Append(col->GetName());
	}
}

// ---------------------------------------------------------------------------
//  Load / Apply (between the dialog and the runtime ibValueListSettings)
//
//  The Filter tab edits the runtime ibValueFilterList in place (the model is
//  the single source of truth), so it has no load/apply step here — only the
//  Sort / Group tabs (still wxListCtrl-based) do.
// ---------------------------------------------------------------------------

void ibDialogListSettings::LoadFromSettings()
{
	if (m_settings == nullptr)
		return;

	// Filter: just sync the model row count to the live list.
	if (m_filterModel != nullptr)
		m_filterModel->ResetFromList();

	if (ibValueSortList* o = m_settings->GetOrder()) {
		for (size_t i = 0; i < o->Count(); ++i) {
			ibValueSortItem* it = o->GetItem(i);
			if (it == nullptr) continue;
			const long row = m_orderList->InsertItem(m_orderList->GetItemCount(), it->GetField());
			m_orderList->SetItem(row, 1, m_orderDir->GetString(static_cast<int>(it->GetDirection())));
			m_orderList->SetItemData(row, static_cast<long>(it->GetDirection()));
		}
	}
	if (ibValueGroupList* g = m_settings->GetGroup()) {
		for (size_t i = 0; i < g->Count(); ++i)
			m_groupList->InsertItem(m_groupList->GetItemCount(), g->GetField(i));
	}
}

void ibDialogListSettings::ApplyToSettings()
{
	if (m_settings == nullptr)
		return;

	// Filter is already applied to the runtime list by the model — nothing to do.

	if (ibValueSortList* o = m_settings->GetOrder()) {
		o->Clear();
		for (long i = 0; i < m_orderList->GetItemCount(); ++i) {
			const wxString field = m_orderList->GetItemText(i, 0);
			const ibSortDirection dir = static_cast<ibSortDirection>(m_orderList->GetItemData(i));
			o->Add(field, dir);
		}
	}
	if (ibValueGroupList* g = m_settings->GetGroup()) {
		g->Clear();
		for (long i = 0; i < m_groupList->GetItemCount(); ++i)
			g->Add(m_groupList->GetItemText(i, 0));
	}
}

// ---------------------------------------------------------------------------
//  Add / Remove handlers
// ---------------------------------------------------------------------------

void ibDialogListSettings::OnFilterAdd(wxCommandEvent&)
{
	ibValueFilterList* filter = GetFilterList();
	if (filter == nullptr || m_filterAddField == nullptr)
		return;

	const wxString field = m_filterAddField->GetValue();
	if (field.IsEmpty())
		return;

	// New row: default comparison Equal, empty typed value. The field choice
	// fixes the row's type — look the attribute up by name so the value column
	// can edit through the runtime (AdjustValue / choice need the type + model).
	ibValueFilterItem* newItem = filter->Add(field, ibComparisonKind_Equal, ibValue(), true);

	const ibBackendQueryable* q = (m_list != nullptr) ? m_list->GetSourceQueryable() : nullptr;
	if (newItem != nullptr && q != nullptr) {
		for (const ibBackendQueryColumn* col : q->GetColumns()) {
			if (col != nullptr && col->GetName() == field) {
				newItem->SetTypeInfo(col->GetColumnId(), col->GetTypeDesc());
				break;
			}
		}
	}
	// Leave the value TYPE_EMPTY: the Value-column Select button then routes
	// through ShowSelectType (pick the type) before QuickChoice / ProcessChoice,
	// exactly like wxFilterDialog. (Add() seeded it with ibValue() == EMPTY.)

	if (m_filterModel != nullptr)
		m_filterModel->ResetFromList();
}

void ibDialogListSettings::OnFilterRemove(wxCommandEvent&)
{
	ibValueFilterList* filter = GetFilterList();
	if (filter == nullptr || m_filterView == nullptr)
		return;

	const ibDataViewItem& sel = m_filterView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || index > filter->Count())
		return;

	// TODO(spike): ibValueFilterList has no RemoveAt(i) — only Clear()/Add().
	// Rebuild the list without the removed row. Add a RemoveAt(size_t) to the
	// backend collection to drop this rebuild dance.
	std::vector<ibValueFilterItem*> keep;
	for (size_t i = 0; i < filter->Count(); ++i) {
		if (i == index - 1) continue;
		keep.push_back(filter->GetItem(i));
	}
	// Snapshot the survivors before Clear() drops the owning refs.
	struct Saved { wxString field; ibComparisonKind cmp; ibValue value; bool use; ibMetaID model; ibTypeDescription type; };
	std::vector<Saved> saved;
	for (ibValueFilterItem* it : keep) {
		if (it == nullptr) continue;
		saved.push_back({ it->GetField(), it->GetComparison(), it->GetFilterValue(),
			it->GetUse(), it->GetModel(), it->GetTypeDescription() });
	}
	filter->Clear();
	for (const Saved& s : saved) {
		ibValueFilterItem* added = filter->Add(s.field, s.cmp, s.value, s.use);
		if (added != nullptr)
			added->SetTypeInfo(s.model, s.type);
	}

	if (m_filterModel != nullptr)
		m_filterModel->ResetFromList();
}

void ibDialogListSettings::OnFilterItemActivated(ibDataViewEvent& event)
{
	if (m_filterView != nullptr)
		m_filterView->EditItem(event.GetItem(), event.GetDataViewColumn());
	event.Skip();
}

void ibDialogListSettings::OnOrderAdd(wxCommandEvent&)
{
	const wxString field = m_orderField->GetValue();
	if (field.IsEmpty())
		return;
	const int dir = m_orderDir->GetSelection();
	const long row = m_orderList->InsertItem(m_orderList->GetItemCount(), field);
	m_orderList->SetItem(row, 1, m_orderDir->GetString(dir));
	m_orderList->SetItemData(row, dir);    // ibSortDirection
}

void ibDialogListSettings::OnOrderRemove(wxCommandEvent&)
{
	const long sel = m_orderList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel != -1)
		m_orderList->DeleteItem(sel);
}

void ibDialogListSettings::OnGroupAdd(wxCommandEvent&)
{
	const wxString field = m_groupField->GetValue();
	if (field.IsEmpty())
		return;
	m_groupList->InsertItem(m_groupList->GetItemCount(), field);
}

void ibDialogListSettings::OnGroupRemove(wxCommandEvent&)
{
	const long sel = m_groupList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
	if (sel != -1)
		m_groupList->DeleteItem(sel);
}

void ibDialogListSettings::OnOk(wxCommandEvent&)
{
	ApplyToSettings();
	EndModal(wxID_OK);
}

// ---------------------------------------------------------------------------
//  Backend hook
// ---------------------------------------------------------------------------

bool ibDialogListSettings::ShowListSettingsDialog(ibValueDynamicList* list)
{
	if (list == nullptr)
		return false;
	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
	ibDialogListSettings dlg(top, list);
	if (dlg.ShowModal() == wxID_OK) {
		list->RefreshComposerSettings();   // OK -> apply the edits onto the composer (L5)
		return true;
	}
	return false;
}
