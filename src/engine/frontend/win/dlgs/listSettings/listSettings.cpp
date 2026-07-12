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
#include "backend/tableInfo.h"                                  // ibValueModel + ibValueModelColumnCollection (PATH A field source)
#include "backend/system/value/valueDynamicList.h"   // ibValueDynamicList — the dialog is built on it
#include "backend/query/queryable.h"                            // ibBackendQueryable::GetColumns
#include "backend/query/queryColumn.h"                          // ibBackendQueryColumn
#include "backend/srcDataObject.h"                              // ibSourceDataObject::ibSourceExplorer + ConvertToMetaIds (dot-walk)
#include "backend/metaCollection/partial/reference/reference.h" // ibValueReferenceDataObject::Create (reference-as-source)

#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/treectrl.h>
#include <wx/splitter.h>
#include <wx/dnd.h>
#include <wx/menu.h>
#include <wx/imaglist.h>
#include <wx/stattext.h>
#include <wx/app.h>

// Filter-tab column ids (model columns): Use / Field / Comparison / Value
// (historically ported from the old filter dialog's column enum).
enum {
	eFilterUse = 0,
	eFilterField,
	eFilterComparison,
	eFilterValue
};

// Sort / Group tab dataview columns.
enum { eOrderField = 0, eOrderDir };
enum { eGroupField = 0 };

// ---------------------------------------------------------------------------
//  Available-fields tree — the LEFT panel of a settings tab lists the
//  source's fields; a reference field lazily expands into its target's fields
//  (Supplier.Region.Country...). Double-clicking (or Add) puts the field into the
//  tab's list on the right. Modelled on advpropSource's source picker, embedded
//  rather than modal (the header TODO to unify the two into one picker stands).
// ---------------------------------------------------------------------------

// Tree-item payload for one source field: its dot-path (technical names), the leaf
// id + type (so a filter row edits its value through the runtime), and the referenced
// target ids for lazy expansion.
struct ibSourceFieldNode : public wxTreeItemData {
	wxString              m_path;
	ibMetaID              m_leafId = wxNOT_FOUND;
	ibTypeDescription     m_type;
	std::vector<ibMetaID> m_refTypes;   // non-empty => reference field, lazy-expand
	bool                  m_loaded = false;
};

// Append each field of `explorer` under `parent`, carrying the accumulated dot-path.
// A reference field gets a dummy [+] and expands lazily (ExpandSourceFieldNode); a
// tabular section is skipped (a setting binds a scalar / reference field, not a section).
static void AppendSourceFields(wxTreeCtrl* tree, const wxTreeItemId& parent,
	const ibSourceDataObject::ibSourceExplorer& explorer, const wxString& prefix, const ibMetaData* metaData)
{
	for (unsigned int i = 0; i < explorer.GetHelperCount(); ++i) {
		const auto* col = explorer.GetHelper(i);
		if (col == nullptr || col->IsTableSection())
			continue;
		ibSourceFieldNode* data = new ibSourceFieldNode();
		data->m_path     = prefix.IsEmpty() ? col->GetSourceName() : prefix + wxT(".") + col->GetSourceName();
		data->m_leafId   = static_cast<ibMetaID>(col->GetSourceId());
		data->m_type     = col->GetTypeDesc();
		data->m_refTypes = ibValueReferenceDataObject::ConvertToMetaIds(col->GetClsidList(), metaData);
		const wxTreeItemId item = tree->AppendItem(parent, col->GetSourceName(), 0, 0, data);   // icon 0 = attribute
		if (!data->m_refTypes.empty())
			tree->AppendItem(item, wxEmptyString);   // dummy -> [+] (a reference expands into its target's fields)
	}
}

// Lazily build a reference field node's children (its target's fields). An EMPTY typed
// reference-as-source vends the target's explorer; its fields are copied into tree nodes
// synchronously, so the temporary reference can die after.
static void ExpandSourceFieldNode(wxTreeCtrl* tree, const wxTreeItemId& item, const ibMetaData* metaData)
{
	ibSourceFieldNode* data = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(item));
	if (data == nullptr || data->m_refTypes.empty() || data->m_loaded || metaData == nullptr)
		return;
	data->m_loaded = true;
	tree->DeleteChildren(item);   // drop the dummy [+]
	for (const ibMetaID& target : data->m_refTypes) {
		ibValue refValue = ibValueReferenceDataObject::Create(metaData, target);
		ibSourceDataObject* refObj = nullptr;
		refValue.ConvertToValue(refObj);
		if (refObj == nullptr)
			continue;
		if (const auto* refExplorer = refObj->GetSourceExplorer())
			AppendSourceFields(tree, item, *refExplorer, data->m_path, metaData);
	}
}

// Drop target for a tab's right-hand composition panel: a field dragged from the LEFT
// tree (remembered as m_dragItem) is added to that tab's list on drop. The text payload
// only exists to enable DnD; the field itself comes from m_dragItem (same-process drag).
class ibFieldDropTarget : public wxTextDropTarget {
public:
	explicit ibFieldDropTarget(std::function<void()> onDrop) : m_onDrop(std::move(onDrop)) {}
	bool OnDropText(wxCoord, wxCoord, const wxString&) override { if (m_onDrop) m_onDrop(); return true; }
private:
	std::function<void()> m_onDrop;
};

// ---------------------------------------------------------------------------
//  Filter model — virtual-list over the dialog's BUFFER ibValueFilterList.
//
//  Edits the ibValueFilterList held by the dialog's ibValueListSettings buffer
//  (historically ported from the old filter dialog's model). GetValueByRow /
//  SetValueByRow walk Count()/GetItem(i) and the FilterItem getters/setters.
//  Row tag is 1-based (GetID()).
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
			// The Comparison choice column carries the ibComparisonKind as a long (GetValueByRow returns
			// (long)GetComparison; the choice editor's index IS that enum). Dispatch it to the filter item so
			// the picked comparison sticks instead of snapping back to Equal.
			item->SetComparison(static_cast<ibComparisonKind>(variant.GetLong()));
			return true;
		}
		else if (col == eFilterValue) {
			// Value editing goes THROUGH the runtime. The text typed into the
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
//  Edits the ibValueFilterItem (historically ported from the old filter
//  dialog's value renderer). It is both a ibDataViewCustomRenderer (draws the
//  value string, hosts the editor) and an ibControlFrame (so QuickChoice /
//  ProcessChoice can push the chosen value back through GetControlValue /
//  SetControlValue / ChoiceProcessing).
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
				const ibValueMetaObjectAttributeBase* attribute = activeMetaData->FindAnyObjectByFilter<ibValueMetaObjectAttributeBase>(item->GetLeafId(), true);

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

// The two public ctors differ ONLY in how they bind the field source (m_list set or
// not). Everything else — the three tabs, load, OK binding — is identical; the field
// source is read once, in BuildFilterFields(), which dispatches on m_list.

ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibValueDynamicList* list)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  m_model(list), m_list(list), m_settings(new ibValueListSettings())
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	// Tabs are GATED by the model's Features (Max: "turn the flag off → the tab is hidden; the default
	// parameters still change"). Default = all on. The first available tab is the selected one.
	const ibValueModel::Features feats = (m_model != nullptr) ? m_model->GetFeatures() : ibValueModel::Features{};
	notebook->AddPage(BuildQueryPage(notebook), _("Query"), true);   // FIRST tab — arbitrary-query source (dynamic-list ctor only)
	if (feats.Has(ibValueModel::Features::Filters))  notebook->AddPage(BuildFilterPage(notebook), _("Filter"), notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Sorting))  notebook->AddPage(BuildOrderPage(notebook),  _("Sort"),   notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Grouping)) notebook->AddPage(BuildGroupPage(notebook),  _("Group"),  notebook->GetPageCount() == 0);
	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, FromDIP(6));

	wxStdDialogButtonSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	mainSizer->Add(btns, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, FromDIP(6));

	SetSizer(mainSizer);

	// Transactional open: load the dialog's BUFFER from the composer (the store) so it shows the current state.
	if (m_model != nullptr && m_settings != nullptr)
		ibLoadSettingsFromComposer(m_settings, m_model->GetModelComposer());

	LoadFromSettings();

	Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOk, this, wxID_OK);
}

// General-model ctor — same body as the dynamic-list ctor, but m_list stays null
// (no source explorer / no composer). The Filter "Add" picker's fields come from the
// model's columns (PATH A — BuildFilterFieldsFromColumns). m_list-specific code below
// (FillFieldChoice, the source-explorer field builder, RefreshComposerSettings) is
// guarded on m_list != nullptr, so the dialog runs correctly with m_list == null.
ibDialogListSettings::ibDialogListSettings(wxWindow* parent, ibValueModel* model)
	: wxDialog(parent, wxID_ANY, _("List settings"), wxDefaultPosition, wxSize(660, 450),
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	  m_model(model), m_list(nullptr), m_settings(new ibValueListSettings())
{
	wxBoxSizer* mainSizer = new wxBoxSizer(wxVERTICAL);

	wxNotebook* notebook = new wxNotebook(this, wxID_ANY);
	// Tabs are GATED by the model's Features (Max: "turn the flag off → the tab is hidden; the default
	// parameters still change"). Default = all on. The first available tab is the selected one.
	const ibValueModel::Features feats = (m_model != nullptr) ? m_model->GetFeatures() : ibValueModel::Features{};
	if (feats.Has(ibValueModel::Features::Filters))  notebook->AddPage(BuildFilterPage(notebook), _("Filter"), notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Sorting))  notebook->AddPage(BuildOrderPage(notebook),  _("Sort"),   notebook->GetPageCount() == 0);
	if (feats.Has(ibValueModel::Features::Grouping)) notebook->AddPage(BuildGroupPage(notebook),  _("Group"),  notebook->GetPageCount() == 0);
	mainSizer->Add(notebook, 1, wxALL | wxEXPAND, FromDIP(6));

	wxStdDialogButtonSizer* btns = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	mainSizer->Add(btns, 0, wxLEFT | wxRIGHT | wxBOTTOM | wxALIGN_RIGHT, FromDIP(6));

	SetSizer(mainSizer);

	// Transactional open: load the dialog's edit BUFFER (m_settings) FROM the composer (the committed store)
	// so it shows the current Filter / Sort / Group; the user edits the buffer; OK commits it back
	// (ShowListSettingsDialog), Cancel discards. The fetch reads the composer, never this buffer.
	if (m_model != nullptr && m_settings != nullptr)
		ibLoadSettingsFromComposer(m_settings, m_model->GetModelComposer());

	LoadFromSettings();

	Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOk, this, wxID_OK);
}

ibValueFilterList* ibDialogListSettings::GetFilterList() const
{
	return m_settings != nullptr ? m_settings->GetFilter() : nullptr;
}

ibValueSortList* ibDialogListSettings::GetOrderList() const
{
	return m_settings != nullptr ? m_settings->GetOrder() : nullptr;
}

ibValueGroupList* ibDialogListSettings::GetGroupList() const
{
	return m_settings != nullptr ? m_settings->GetGroup() : nullptr;
}

// ---------------------------------------------------------------------------
//  Pages
// ---------------------------------------------------------------------------

// The FIRST tab (dynamic-list only) — arbitrary-query source: an enable flag + the query text. When off, the list
// takes its picked metaobject source; when on, this TEXT is the source (composer.FromText). Edits the list's OWN
// UseCustomQuery / CustomQuery properties (serialised), NOT the settings buffer; applied to the list on OK.
wxWindow* ibDialogListSettings::BuildQueryPage(wxWindow* parent)
{
	wxPanel* page = new wxPanel(parent, wxID_ANY);
	wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

	m_queryUseCheck = new wxCheckBox(page, wxID_ANY, _("Arbitrary query"));
	sizer->Add(m_queryUseCheck, 0, wxALL, FromDIP(6));

	m_queryText = new wxTextCtrl(page, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxHSCROLL);
	m_queryText->Enable(false);   // meaningful only when the flag is on
	sizer->Add(m_queryText, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(6));

	m_queryUseCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& e) {
		if (m_queryText != nullptr) m_queryText->Enable(e.IsChecked());
	});

	page->SetSizer(sizer);
	return page;
}

wxWindow* ibDialogListSettings::BuildFilterPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	// Draggable split: LEFT = available-fields tree (dot-walkable), RIGHT = the composed filter.
	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (a reference field expands into its target's fields) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_filterFieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_filterFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: composed filter — Use (toggle) / Field / Comparison (choice) / Value ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	m_filterView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxDV_ROW_LINES | wxDV_SINGLE);
	m_filterView->SetBackgroundColour(rightPane->GetBackgroundColour());
	m_filterView->SetForegroundColour(rightPane->GetForegroundColour());
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
		eFilterComparison, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_filterView->AppendColumn(cmpColumn);
	ibDataViewColumn* valColumn = new ibDataViewColumn(_("Value"),
		new ibFilterValueRenderer(this), eFilterValue, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_filterView->AppendColumn(valColumn);
	rightSizer->Add(m_filterView, 1, wxALL | wxEXPAND, FromDIP(4));

	// Add (from the selected field) / Remove a filter row; double-clicking a field on the
	// LEFT adds it too. Selecting the field fixes the row's type — Use / Comparison / Value
	// are then edited inside the row.
	wxBoxSizer* btnRow = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addBtn = new wxButton(rightPane, wxID_ANY, _("Add"));
	wxButton* delBtn = new wxButton(rightPane, wxID_ANY, _("Remove"));
	btnRow->AddStretchSpacer(1);
	btnRow->Add(addBtn, 0, wxALL, FromDIP(2));
	btnRow->Add(delBtn, 0, wxALL, FromDIP(2));
	rightSizer->Add(btnRow, 0, wxEXPAND);
	rightPane->SetSizer(rightSizer);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(180));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	panel->SetSizer(panelSizer);

	// Populate the available-fields tree + wire it: references expand lazily, double-click adds.
	PopulateFieldTree(m_filterFieldTree);
	m_filterFieldTree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogListSettings::OnFieldTreeExpanding, this);
	m_filterFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogListSettings::OnFilterFieldActivated, this);
	m_filterFieldTree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogListSettings::OnFieldTreeBeginDrag, this);
	rightPane->SetDropTarget(new ibFieldDropTarget([this]{ AddFilterForField(m_dragItem); }));
	addBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnFilterAdd, this);
	delBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnFilterRemove, this);

	// Model edits the dialog's BUFFER ibValueFilterList directly (committed to
	// the composer on OK).
	m_filterModel = new ibFilterModel(this);
	m_filterView->AssociateModel(m_filterModel);
	m_filterView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogListSettings::OnListContextMenu, this);

	return panel;
}

// ---- Sort model — virtual list over the dialog's BUFFER sort list (Field + editable Direction). ----
class ibDialogListSettings::ibOrderModel : public ibDataViewVirtualListModel {
	ibDialogListSettings* m_dialog;
public:
	explicit ibOrderModel(ibDialogListSettings* dialog) : ibDataViewVirtualListModel(), m_dialog(dialog) {}
	ibValueSortList* GetOrder() const { return m_dialog->GetOrderList(); }
	void ResetFromList() { ibValueSortList* o = GetOrder(); Reset(o != nullptr ? (unsigned int)o->Count() : 0u); }
	virtual void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		ibValueSortList* o = GetOrder();
		if (o == nullptr) return;
		ibValueSortItem* it = o->GetItem(row);
		if (it == nullptr) return;
		if (col == eOrderField)    variant = it->GetField();
		else if (col == eOrderDir) variant = (long)it->GetDirection();
	}
	virtual bool SetValueByRow(const wxVariant& variant, unsigned row, unsigned col) override {
		ibValueSortList* o = GetOrder();
		if (o == nullptr) return false;
		ibValueSortItem* it = o->GetItem(row);
		if (it == nullptr) return false;
		if (col == eOrderDir) {   // the Direction choice carries the ibSortDirection as its index
			it->SetDirection(static_cast<ibSortDirection>(variant.GetLong()));
			return true;
		}
		return false;
	}
};

// ---- Group model — virtual list over the dialog's BUFFER group list (Field). ----
class ibDialogListSettings::ibGroupModel : public ibDataViewVirtualListModel {
	ibDialogListSettings* m_dialog;
public:
	explicit ibGroupModel(ibDialogListSettings* dialog) : ibDataViewVirtualListModel(), m_dialog(dialog) {}
	ibValueGroupList* GetGroup() const { return m_dialog->GetGroupList(); }
	void ResetFromList() { ibValueGroupList* g = GetGroup(); Reset(g != nullptr ? (unsigned int)g->Count() : 0u); }
	virtual void GetValueByRow(wxVariant& variant, unsigned row, unsigned col) const override {
		ibValueGroupList* g = GetGroup();
		if (g == nullptr) return;
		if (col == eGroupField) variant = g->GetField(row);
	}
	virtual bool SetValueByRow(const wxVariant&, unsigned, unsigned) override { return false; }
};

wxWindow* ibDialogListSettings::BuildOrderPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (dot-walkable) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_orderFieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_orderFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the sort list — Field + editable Direction (choice), model-driven ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	m_orderView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	m_orderView->SetBackgroundColour(rightPane->GetBackgroundColour());
	m_orderView->SetForegroundColour(rightPane->GetForegroundColour());
	m_orderView->AppendTextColumn(_("Field"), eOrderField, wxDATAVIEW_CELL_INERT, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	wxArrayString dirChoices;
	dirChoices.push_back(_("Ascending"));    // order MUST match enum ibSortDirection
	dirChoices.push_back(_("Descending"));
	ibDataViewColumn* dirColumn = new ibDataViewColumn(_("Direction"),
		new ibDataViewChoiceByIndexRenderer(dirChoices, wxDATAVIEW_CELL_EDITABLE, wxAlignment::wxALIGN_LEFT),
		eOrderDir, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	m_orderView->AppendColumn(dirColumn);
	rightSizer->Add(m_orderView, 1, wxALL | wxEXPAND, FromDIP(4));

	wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addBtn = new wxButton(rightPane, wxID_ANY, _("Add"));
	wxButton* delBtn = new wxButton(rightPane, wxID_ANY, _("Remove"));
	row->AddStretchSpacer(1);
	row->Add(addBtn, 0, wxALL, FromDIP(2));
	row->Add(delBtn, 0, wxALL, FromDIP(2));
	rightSizer->Add(row, 0, wxEXPAND);
	rightPane->SetSizer(rightSizer);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(180));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	panel->SetSizer(panelSizer);

	PopulateFieldTree(m_orderFieldTree);
	m_orderFieldTree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogListSettings::OnFieldTreeExpanding, this);
	m_orderFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogListSettings::OnOrderFieldActivated, this);
	m_orderFieldTree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogListSettings::OnFieldTreeBeginDrag, this);
	rightPane->SetDropTarget(new ibFieldDropTarget([this]{ AddOrderForField(m_dragItem); }));
	addBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOrderAdd, this);
	delBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnOrderRemove, this);

	m_orderModel = new ibOrderModel(this);
	m_orderView->AssociateModel(m_orderModel);
	m_orderView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogListSettings::OnListContextMenu, this);
	m_orderModel->ResetFromList();

	return panel;
}

wxWindow* ibDialogListSettings::BuildGroupPage(wxWindow* parent)
{
	wxPanel* panel = new wxPanel(parent);
	wxSplitterWindow* splitter = new wxSplitterWindow(panel, wxID_ANY,
		wxDefaultPosition, wxDefaultSize, wxSP_LIVE_UPDATE | wxSP_3DSASH);
	splitter->SetMinimumPaneSize(FromDIP(120));

	// ---- LEFT pane: available fields (dot-walkable) ----
	wxPanel* leftPane = new wxPanel(splitter);
	wxBoxSizer* leftSizer = new wxBoxSizer(wxVERTICAL);
	leftSizer->Add(new wxStaticText(leftPane, wxID_ANY, _("Available fields")), 0, wxALL, FromDIP(4));
	m_groupFieldTree = new wxTreeCtrl(leftPane, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTR_HAS_BUTTONS | wxTR_SINGLE | wxTR_HIDE_ROOT | wxTR_LINES_AT_ROOT | wxTR_NO_LINES | wxTR_TWIST_BUTTONS);
	leftSizer->Add(m_groupFieldTree, 1, wxLEFT | wxRIGHT | wxBOTTOM | wxEXPAND, FromDIP(4));
	leftPane->SetSizer(leftSizer);

	// ---- RIGHT pane: the grouping list ----
	wxPanel* rightPane = new wxPanel(splitter);
	wxBoxSizer* rightSizer = new wxBoxSizer(wxVERTICAL);
	m_groupView = new ibDataViewCtrl(rightPane, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_ROW_LINES | wxDV_SINGLE);
	m_groupView->SetBackgroundColour(rightPane->GetBackgroundColour());
	m_groupView->SetForegroundColour(rightPane->GetForegroundColour());
	m_groupView->AppendTextColumn(_("Field"), eGroupField, wxDATAVIEW_CELL_INERT, wxNOT_FOUND, wxAlignment::wxALIGN_LEFT);
	rightSizer->Add(m_groupView, 1, wxALL | wxEXPAND, FromDIP(4));

	wxBoxSizer* row = new wxBoxSizer(wxHORIZONTAL);
	wxButton* addBtn = new wxButton(rightPane, wxID_ANY, _("Add"));
	wxButton* delBtn = new wxButton(rightPane, wxID_ANY, _("Remove"));
	row->AddStretchSpacer(1);
	row->Add(addBtn, 0, wxALL, FromDIP(2));
	row->Add(delBtn, 0, wxALL, FromDIP(2));
	rightSizer->Add(row, 0, wxEXPAND);
	rightPane->SetSizer(rightSizer);

	splitter->SplitVertically(leftPane, rightPane, FromDIP(180));
	wxBoxSizer* panelSizer = new wxBoxSizer(wxVERTICAL);
	panelSizer->Add(splitter, 1, wxEXPAND);
	panel->SetSizer(panelSizer);

	PopulateFieldTree(m_groupFieldTree);
	m_groupFieldTree->Bind(wxEVT_TREE_ITEM_EXPANDING, &ibDialogListSettings::OnFieldTreeExpanding, this);
	m_groupFieldTree->Bind(wxEVT_TREE_ITEM_ACTIVATED, &ibDialogListSettings::OnGroupFieldActivated, this);
	m_groupFieldTree->Bind(wxEVT_TREE_BEGIN_DRAG, &ibDialogListSettings::OnFieldTreeBeginDrag, this);
	rightPane->SetDropTarget(new ibFieldDropTarget([this]{ AddGroupForField(m_dragItem); }));
	addBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnGroupAdd, this);
	delBtn->Bind(wxEVT_BUTTON, &ibDialogListSettings::OnGroupRemove, this);

	m_groupModel = new ibGroupModel(this);
	m_groupView->AssociateModel(m_groupModel);
	m_groupView->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, &ibDialogListSettings::OnListContextMenu, this);
	m_groupModel->ResetFromList();

	return panel;
}

// ---------------------------------------------------------------------------
//  Load / Apply (between the dialog UI and the dialog's BUFFER ibValueListSettings)
//
//  Every tab (Filter / Sort / Group) is model-driven and edits its buffer list in
//  place, so there is no per-tab copy step — Load just syncs the model row counts,
//  Apply is a no-op. The buffer is committed to the composer on OK.
// ---------------------------------------------------------------------------

void ibDialogListSettings::LoadFromSettings()
{
	// Query tab (dynamic-list only) — load the list's arbitrary-query flag + text into the controls.
	if (m_list != nullptr && m_queryUseCheck != nullptr) {
		m_queryUseCheck->SetValue(m_list->IsArbitraryQuery());
		if (m_queryText != nullptr) {
			m_queryText->SetValue(m_list->GetArbitraryQueryText());
			m_queryText->Enable(m_list->IsArbitraryQuery());
		}
	}
	if (m_settings == nullptr)
		return;
	// Every tab's model binds straight to its buffer list (Filter / Order / Group) — just
	// sync the row counts to what ibLoadSettingsFromComposer put in the buffer.
	if (m_filterModel != nullptr)
		m_filterModel->ResetFromList();
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
}

void ibDialogListSettings::ApplyToSettings()
{
	// Query tab (dynamic-list only) — apply the arbitrary-query flag + text onto the list, then re-apply the source.
	if (m_list != nullptr && m_queryUseCheck != nullptr) {
		m_list->SetArbitraryQuery(m_queryUseCheck->GetValue());
		if (m_queryText != nullptr)
			m_list->SetArbitraryQueryText(m_queryText->GetValue());
		m_list->ApplySource();
	}
	// Every tab edits its buffer list IN PLACE through its model — nothing to copy back here.
	// The buffer is committed to the composer on OK (ibCommitSettingsToComposer).
}

// ---------------------------------------------------------------------------
//  Add / Remove handlers
// ---------------------------------------------------------------------------

// The config metaData that resolves reference targets — the dynamic list's own, else the ACTIVE
// config. Without a valid metaData, ConvertToMetaIds returns nothing and every field looks like a
// leaf (no [+]) — which is exactly the "flat list" bug.
const ibMetaData* ibDialogListSettings::SourceMetaData() const
{
	if (m_list != nullptr)
		if (const ibMetaData* md = m_list->GetSourceMetaData())
			return md;
	return activeMetaData;
}

// Root an available-fields tree (shared by all three tabs). ALWAYS via a source EXPLORER when the
// thing IS a source (dynamic list OR a source-model like a value-table), so a REFERENCE column gets
// a [+] and expands into its target's fields — exactly like advpropSource's picker. Only a
// non-source model falls back to flat columns (and even those get a [+] when their type is a reference).
void ibDialogListSettings::PopulateFieldTree(wxTreeCtrl* tree)
{
	if (tree == nullptr)
		return;
	// Attribute icon (index 0) on every field — same icon the source-explorer picker uses.
	wxImageList* imgs = new wxImageList(16, 16);
	imgs->Add(ibValue::GetIconGroup());
	tree->AssignImageList(imgs);

	const ibMetaData* metaData = SourceMetaData();
	const wxTreeItemId root = tree->AddRoot(wxEmptyString);

	if (m_list != nullptr) {
		if (const auto* explorer = m_list->GetSourceExplorer())
			AppendSourceFields(tree, root, *explorer, wxEmptyString, metaData);
		return;
	}
	if (m_model != nullptr) {
		// A model that IS a source (value-table, …) vends a self-describing explorer — use it so
		// its reference columns expand, just like the dynamic-list path above.
		if (ibSourceDataObject* src = dynamic_cast<ibSourceDataObject*>(m_model)) {
			if (const auto* explorer = src->GetSourceExplorer()) {
				AppendSourceFields(tree, root, *explorer, wxEmptyString, metaData);
				return;
			}
		}
		// Non-source model — flat columns, but a reference-typed column still gets its [+].
		ibValueModel::ibValueModelColumnCollection* columns = m_model->GetColumnCollection();
		if (columns == nullptr)
			return;
		for (unsigned int i = 0; i < columns->GetColumnCount(); ++i) {
			const auto* col = columns->GetColumnInfo(i);
			if (col == nullptr)
				continue;
			ibSourceFieldNode* data = new ibSourceFieldNode();
			data->m_path     = col->GetColumnName();
			data->m_leafId   = static_cast<ibMetaID>(col->GetColumnID());
			data->m_type     = col->GetColumnType();
			data->m_refTypes = ibValueReferenceDataObject::ConvertToMetaIds(col->GetColumnType().GetClsidList(), metaData);
			const wxTreeItemId item = tree->AppendItem(root, col->GetColumnName(), 0, 0, data);
			if (!data->m_refTypes.empty())
				tree->AppendItem(item, wxEmptyString);   // dummy -> [+]
		}
	}
}

// Lazily expand a reference field — shared by all three tabs' field trees (the tree that fired the
// event is the one to expand); metaData = the config that resolves reference targets.
void ibDialogListSettings::OnFieldTreeExpanding(wxTreeEvent& evt)
{
	wxTreeCtrl* tree = wxDynamicCast(evt.GetEventObject(), wxTreeCtrl);
	if (tree != nullptr)
		ExpandSourceFieldNode(tree, evt.GetItem(), SourceMetaData());
}

// Start dragging a field out of the LEFT tree; dropping on the tab's right panel adds it
// (the dropped field is the remembered m_dragItem — same-process drag, text payload is a stub).
void ibDialogListSettings::OnFieldTreeBeginDrag(wxTreeEvent& evt)
{
	wxTreeCtrl* tree = wxDynamicCast(evt.GetEventObject(), wxTreeCtrl);
	if (tree == nullptr)
		return;
	ibSourceFieldNode* node = dynamic_cast<ibSourceFieldNode*>(tree->GetItemData(evt.GetItem()));
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;   // only a real field is draggable
	m_dragItem = evt.GetItem();
	wxTextDataObject data(node->m_path);
	wxDropSource source(tree);
	source.SetData(data);
	source.DoDragDrop(wxDrag_CopyOnly);
}

// Right-click on a composition list (Filter / Sort / Group) — a command menu to add the
// currently-selected available field or remove the selected row, routed by which view fired.
void ibDialogListSettings::OnListContextMenu(ibDataViewEvent& evt)
{
	const wxObject* src = evt.GetEventObject();
	wxMenu menu;
	menu.Append(wxID_ADD, _("New element"));
	menu.Append(wxID_REMOVE, _("Remove"));
	if (src == m_filterView) {
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)  { AddFilterForField(m_filterFieldTree->GetSelection()); }, wxID_ADD);
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnFilterRemove(e); }, wxID_REMOVE);
	}
	else if (src == m_orderView) {
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)  { AddOrderForField(m_orderFieldTree->GetSelection()); }, wxID_ADD);
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnOrderRemove(e); }, wxID_REMOVE);
	}
	else if (src == m_groupView) {
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent&)  { AddGroupForField(m_groupFieldTree->GetSelection()); }, wxID_ADD);
		menu.Bind(wxEVT_MENU, [this](wxCommandEvent& e) { OnGroupRemove(e); }, wxID_REMOVE);
	}
	else
		return;
	PopupMenu(&menu);
}

// Add a filter row on the chosen field-tree node — its dot-path + leaf id/type come
// straight from the node, so even a DEEP path (Supplier.Region.Country) resolves its
// value editor. New row: default comparison Equal, empty typed value (the Value-column
// Select button then routes through ShowSelectType before QuickChoice / ProcessChoice).
void ibDialogListSettings::AddFilterForField(const wxTreeItemId& item)
{
	ibSourceFieldNode* node = item.IsOk()
		? dynamic_cast<ibSourceFieldNode*>(m_filterFieldTree->GetItemData(item)) : nullptr;
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;
	ibValueFilterList* filter = GetFilterList();
	if (filter == nullptr)
		return;
	ibValueFilterItem* newItem = filter->Add(node->m_path, ibComparisonKind_Equal, ibValue(), true);
	if (newItem != nullptr)
		newItem->SetTypeInfo(node->m_leafId, node->m_type);
	if (m_filterModel != nullptr)
		m_filterModel->ResetFromList();
}

void ibDialogListSettings::OnFilterAdd(wxCommandEvent&)           { AddFilterForField(m_filterFieldTree->GetSelection()); }
void ibDialogListSettings::OnFilterFieldActivated(wxTreeEvent& e) { AddFilterForField(e.GetItem()); }

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
	struct Saved { wxString field; ibComparisonKind cmp; ibValue value; bool use; ibMetaID leafId; ibTypeDescription type; };
	std::vector<Saved> saved;
	for (ibValueFilterItem* it : keep) {
		if (it == nullptr) continue;
		saved.push_back({ it->GetField(), it->GetComparison(), it->GetFilterValue(),
			it->GetUse(), it->GetLeafId(), it->GetTypeDescription() });
	}
	filter->Clear();
	for (const Saved& s : saved) {
		ibValueFilterItem* added = filter->Add(s.field, s.cmp, s.value, s.use);
		if (added != nullptr)
			added->SetTypeInfo(s.leafId, s.type);
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

// Add the chosen available field to the sort list (default Ascending; direction is edited
// inline in the Direction column). Mutates the dialog's BUFFER list, then refreshes the model.
void ibDialogListSettings::AddOrderForField(const wxTreeItemId& item)
{
	ibSourceFieldNode* node = item.IsOk()
		? dynamic_cast<ibSourceFieldNode*>(m_orderFieldTree->GetItemData(item)) : nullptr;
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;
	ibValueSortList* o = GetOrderList();
	if (o == nullptr)
		return;
	o->Add(node->m_path);
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
}

void ibDialogListSettings::OnOrderAdd(wxCommandEvent&)           { AddOrderForField(m_orderFieldTree->GetSelection()); }
void ibDialogListSettings::OnOrderFieldActivated(wxTreeEvent& e) { AddOrderForField(e.GetItem()); }

void ibDialogListSettings::OnOrderRemove(wxCommandEvent&)
{
	ibValueSortList* o = GetOrderList();
	if (o == nullptr || m_orderView == nullptr)
		return;
	const ibDataViewItem& sel = m_orderView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || index > o->Count())
		return;
	// No RemoveAt on the sort list — rebuild without the removed row.
	struct Saved { wxString field; ibSortDirection dir; };
	std::vector<Saved> keep;
	for (size_t i = 0; i < o->Count(); ++i) {
		if (i == index - 1) continue;
		if (ibValueSortItem* it = o->GetItem(i))
			keep.push_back({ it->GetField(), it->GetDirection() });
	}
	o->Clear();
	for (const Saved& s : keep)
		o->Add(s.field, s.dir);
	if (m_orderModel != nullptr)
		m_orderModel->ResetFromList();
}

// Add the chosen available field to the grouping list (BUFFER + model refresh).
void ibDialogListSettings::AddGroupForField(const wxTreeItemId& item)
{
	ibSourceFieldNode* node = item.IsOk()
		? dynamic_cast<ibSourceFieldNode*>(m_groupFieldTree->GetItemData(item)) : nullptr;
	if (node == nullptr || node->m_leafId == wxNOT_FOUND)
		return;
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr)
		return;
	g->Add(node->m_path);
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
}

void ibDialogListSettings::OnGroupAdd(wxCommandEvent&)           { AddGroupForField(m_groupFieldTree->GetSelection()); }
void ibDialogListSettings::OnGroupFieldActivated(wxTreeEvent& e) { AddGroupForField(e.GetItem()); }

void ibDialogListSettings::OnGroupRemove(wxCommandEvent&)
{
	ibValueGroupList* g = GetGroupList();
	if (g == nullptr || m_groupView == nullptr)
		return;
	const ibDataViewItem& sel = m_groupView->GetSelection();
	if (!sel.IsOk())
		return;
	const size_t index = reinterpret_cast<size_t>(sel.GetID());   // 1-based
	if (index == 0 || index > g->Count())
		return;
	// No RemoveAt on the group list — rebuild without the removed row.
	struct Saved { wxString field; ibQueryDimUnfold kind; };
	std::vector<Saved> keep;
	for (size_t i = 0; i < g->Count(); ++i) {
		if (i == index - 1) continue;
		keep.push_back({ g->GetField(i), g->GetKind(i) });
	}
	g->Clear();
	for (const Saved& s : keep)
		g->Add(s.field, s.kind);
	if (m_groupModel != nullptr)
		m_groupModel->ResetFromList();
}

void ibDialogListSettings::OnOk(wxCommandEvent&)
{
	ApplyToSettings();   // UI → buffer
	// COMMIT the buffer onto the composer (the store) + refresh — the whole transaction lands atomically on OK.
	// Cancel never reaches here, so the composer stays untouched.
	if (m_model != nullptr && m_settings != nullptr) {
		ibCommitSettingsToComposer(m_model->GetModelComposer(), m_settings);
		m_model->NotifyReset();
	}
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

bool ibDialogListSettings::ShowListSettingsDialog(ibValueModel* model)
{
	if (model == nullptr)
		return false;
	// A dynamic list IS-A ibValueModel: route it through the list overload so it keeps
	// its source-explorer field picker (PATH B) and composer refresh. Any other model
	// uses the column-based field source (PATH A); OnOk commits the dialog's buffer onto
	// the model's composer (ibCommitSettingsToComposer + NotifyReset).
	if (ibValueDynamicList* list = dynamic_cast<ibValueDynamicList*>(model))
		return ShowListSettingsDialog(list);

	wxWindow* top = (wxTheApp != nullptr) ? wxTheApp->GetTopWindow() : nullptr;
	ibDialogListSettings dlg(top, model);
	// OK commits the dialog's buffer onto the composer + refreshes (OnOk); Cancel leaves the composer untouched.
	return dlg.ShowModal() == wxID_OK;
}
