#ifndef __TABLE_BOX_H__
#define __TABLE_BOX_H__

#include "backend/model.h"
// ibValueEnumeration template + AddEnumeration come from here; on
// desktop they were pulled in transitively through tableView.h. Web
// build skips tableView.h, so add the direct include.
#include "backend/compiler/enumUnit.h"

#include "frontend/visualView/ctrl/window.h"
#include "frontend/visualView/ctrl/typeControl.h"
#include "backend/sourceDescription.h"   // ibSourceDescription (full [head, field] binding path)

#ifndef OES_USE_WEB
// Desktop pulls in the full wxDataView-based viewer. Web build keeps
// only the structural surface BuildForm calls (SetControlName /
// SetSource / SetVisibleColumn / Create-as-stub) and ifdefs out every
// wxDataView-touching method declaration — the real Create / event
// handlers / model wiring are desktop-only.
#include "frontend/win/ctrls/tableView.h"
#else
// Web build: mirror the dataview-enum literals tableBox.h's enum classes
// reference. These match dataview.h verbatim; keeping them in one place
// avoids dragging the full wxDataView headers into the web compile.
enum ibDataViewSelectionMode {
	ibDataViewSelectCell = 0,
	ibDataViewSelectRow  = 1,
};
enum ibDataViewViewMode {
	ibDataViewTree,
	ibDataViewHierarchical,
	ibDataViewList,
};
// wxDVC_DEFAULT_WIDTH constant from dataview.h used by ibPropertyUInteger
// default in tableBoxColumn. Same value as wx core uses (80).
#ifndef wxDVC_DEFAULT_WIDTH
#define wxDVC_DEFAULT_WIDTH 80
#endif
#endif

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON TABLE & COLUMN
constexpr ibClassID g_controlTableBoxCLSID = control_to_clsid("CT_TABL");
constexpr ibClassID g_controlTableBoxColumnCLSID = control_to_clsid("CT_TBLC");

//********************************************************************************************
//*                                 Value TableBox                                           *
//********************************************************************************************

class ibValueEnumTableBoxSelectionMode :
	public ibValueEnumeration<ibDataViewSelectionMode> {
	public:
	ibValueEnumTableBoxSelectionMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectCell, wxT("SelectCell"), _("Select cell"));
		AddEnumeration(ibDataViewSelectionMode::ibDataViewSelectRow, wxT("SelectRow"), _("Select row"));
	}
private:
};

class ibValueEnumTableBoxViewMode :
	public ibValueEnumeration<ibDataViewViewMode> {
	public:
	ibValueEnumTableBoxViewMode() : ibValueEnumeration() {}
	virtual void CreateEnumeration() {
		AddEnumeration(ibDataViewViewMode::ibDataViewHierarchical, wxT("Hierarchical"), _("Hierarchical"));
		AddEnumeration(ibDataViewViewMode::ibDataViewTree, wxT("Tree"), _("Tree"));
		AddEnumeration(ibDataViewViewMode::ibDataViewList, wxT("List"), _("List"));
	}
private:
};

class ibValueModelTableBox : public ibValueWindowComposite,
	public ibTypeControlFactory, public ibSourceObject {
	public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); ibValueModelTableBox::RefreshModel(true); }
	// Full binding path [headAttrId, tableSection, ...] — the resolve walks the attribute.
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); ibValueModelTableBox::RefreshModel(true); }
	// Designer: rebuild the DEFAULT columns from the bound source explorer (walks the path to the leaf
	// section / list). Shared by the drag-to-create drop and the inspector's Source-change refill.
	void RefillFromSource() override;
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	// This tablebox's own bound path ([headAttr, tableSection] or [headAttr]) — a child column's
	// path is this prefix + its own field id(s); the row-relative tail is what the resolve walks.
	const std::vector<ibSourceHop>& GetSourcePath() const { return m_propertySource->GetValueAsPath(); }
	////////////////////////////////////////////////////////////////////////////////////////

	// Available sources = the owning form's attributes of THIS control's kind (table).
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

	ibValueModelTableBox();
	virtual ~ibValueModelTableBox() {}

	//Get source attribute
	virtual const class ibBackendSourceColumn* GetSourceAttributeObject() const { return m_propertySource->GetSourceAttributeObject(); }
	// Unbound (no source picked) -> the whole composite (chrome + inner) is not rendered
	// (ibValueWindowComposite::UpdateWithLayers gate). Ask the PROPERTY (IsEmptyProperty), NOT
	// GetSourceDesc — the latter walks the source and can be broken; the property flag is cheap and safe.
	virtual bool IsSourceMissing() const override { return m_propertySource->IsEmptyProperty(); }
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_table; }
	virtual ibSourceDataType GetFilterSourceDataType() const { return ibSourceDataType::ibSourceDataType_table; }

	//Get source object
	virtual ibSourceObject* GetSourceObject() const;

	// This tablebox's bound path ([headAttr, tableSection] or [headAttr] for a list) — MUTABLE ref (like
	// GetTypeDesc): read it, or assign to bind (GetSourceDesc() = desc). A child column composes its own
	// path as THIS path + its column id.
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }

#pragma region _source_data_

	//get metaData from object
	virtual const ibValueMetaObjectCompositeData* GetSourceMetaObject() const;
	// ibSourceObject's source metadata — same context this control already exposes via GetMetaData.
	virtual const ibMetaData* GetSourceMetaData() const override { return GetMetaData(); }
	//get ref class
	virtual ibClassID GetSourceClassType() const;
	//Get presentation 
	virtual wxString GetSourceCaption() const { return GetString(); }

#pragma endregion 

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get model
	ibValueModel* GetTableModel() const { return m_tableModel; }

	// Choice mode = this table is a VALUE PICKER (opened to return a selection to a caller). The TableBox owns
	// this affordance (like a form owns Close / Update): when on, GetStandardCommands composes Select FIRST.
	// The front-owned property is the SOLE source of truth — set at form-build from the source explorer's choice
	// flag (autobuild) or by the runtime open-as-choice path (SetChoiceMode). The dumb model carries no choice.
	bool IsChoiceMode() const { return m_propertyChoiceMode->GetValueAsBoolean(); }
	// Set at FORM-BUILD time from the source explorer (a picker source stamps its main table node — see
	// ibSourceExplorer::IsChoiceMode); this is the source-of-truth for the runtime open-as-choice path.
	void SetChoiceMode(bool on = true) { m_propertyChoiceMode->SetValue(on); }

	// Single point: resolve a dot-path column's value for ONE row (called per visible cell from the
	// column renderer's CheckedGetValue). First hop via the DUMB model (GetValueByMetaID), deeper
	// hops walk the reference on the front. Returns false for a plain column (model resolves it).
	bool ResolveCellValue(const ibDataViewItem& item, const class ibValueModelTableBoxColumn* column, wxVariant& out) const;

	// A dot-path column — its binding reaches PAST the tablebox prefix + one row column. Such a
	// column is resolved through the dot (read-only): the renderer suppresses inline editing.
	bool IsPathColumn(const class ibValueModelTableBoxColumn* column) const;

	// A FOREIGN-root column — its path is NOT under this tablebox's own bound prefix, so it is rooted
	// at a different form source: the object ABOVE the table (its header). Such a column lives in the
	// tablebox alongside the real columns but reads from the form, CONSTANT across every row of the
	// tabular section (the header doesn't vary per line). Read-only, like a dot-path column. (Mode 2)
	bool IsForeignColumn(const class ibValueModelTableBoxColumn* column) const;

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const {
		return m_propertySource->GetValueAsTypeDesc();
	}

	//methods & attributes
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	// before/after run 
	virtual bool InitializeControl() { CreateModel(); return true; }

	//get title
	virtual wxString GetControlTitle() const {

		if (!m_propertySource->IsEmptyProperty()) {
			ibValue pvarPropVal;
			if (m_propertySource->GetDataValue(pvarPropVal))
				return _("TableBox") + wxT(": ") + stringUtils::GenerateSynonym(pvarPropVal.GetString());
		}

		return _("TableBox") + wxT(": ") + _("<empty source>");
	}

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//get component type
	virtual int GetComponentType() const { return COMPONENT_TYPE_WINDOW; }

	// A table bound to the form's MAIN attribute doesn't carry its own command bar — the form's
	// toolbar already serves those commands, so a table bar would just duplicate them (Add / Mark
	// as delete twice). Suppress it: no toolbar layer, no "Command interface" node, no AutoFill.
	virtual bool HasCommandBar() const override;

	// This table IS the form's main source — its WHOLE binding path is the main attribute (a single hop). THE
	// authoritative "am I the main view" fact: HasCommandBar reads it (a main view shows no bar of its own —
	// the form toolbar serves it), and the form's command-provider resolve finds it by this (formAction.cpp).
	// A nested source (a tabular section, path [mainAttr, section]) has the main attribute only as its HEAD —
	// not main-bound; it keeps its own bar.
	virtual bool IsMainSourceBound() const override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(ibProperty* property, const wxVariant& oldValue, const wxVariant& newValue);


	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	/**
	* Override actionData
	*/

	virtual ibStandardCommandSet GetStandardCommands(const ibFormID& formType);
	// The command bar calls this (generic id, form). The TableBox reads the rows a command runs against — the
	// SELECTED row plus the create ANCHOR (resolved per view mode, see CallAsAction) — and either runs a view-state
	// command DIRECTLY against the control (Command_*), or forwards the OBJECT command to the model as
	// CallAsCommand(id, {selection, anchor}, form) — the ibStandardCommandTabular contract.
	virtual void CallAsAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);


	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu);
	virtual void ExecuteMenu(ibVisualHost* visualHost, int id);

	//contol value
	virtual bool HasValueInControl() const {
		return m_propertySource->IsEmptyProperty();
	}

	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

	//other
	void AddColumn();
#ifndef OES_USE_WEB
	void CreateColumnCollection(ibDataViewCtrl* tableCtrl = nullptr);
#endif

	void CreateTable(bool recreateModel = false);

	void CreateModel(bool recreateModel = false);
	void RefreshModel(bool recreateModel = false);

	// get current line if exist
	ibValueModel::ibValueModelReturnLine* GetCurrentLine() const { return m_tableCurrentLine; }

	// Single source of truth for programmatic current-line mutation.
	// All non-user-click paths (OnIdle restore from createdValue /
	// ownerControl / changedValue, script-side SetPropVal CurrentRow,
	// paged-bootstrap focus restore) route through here so
	// m_tableCurrentLine, ctrl's selection, optionally ctrl's current
	// row and viewport visibility stay consistent.  Pass nullptr for
	// `line` to clear.  The script Selection event is fired in both
	// directions (user click via OnSelectionChanged, programmatic
	// here) so listeners get a unified signal regardless of source.
	//
	// `focus = false` records the current line + drives selection
	// highlight without grabbing keyboard focus or scrolling the
	// viewport — for cross-table coordinated updates, batch
	// programmatic replays, or any "track current line silently"
	// flow where the user shouldn't see their cursor jump.
	void ApplyCurrentLine(ibValueModel::ibValueModelReturnLine* line,
	                      bool focus = true);

	void SetCalculateColumnPos() { m_need_calculate_pos = true; }

protected:

	virtual void OnChangeChildPosition(ibValueFrame* obj, unsigned int pos) { SetCalculateColumnPos(); }

	void CalculateColumnPos();

	// The TableBox's OWN command handlers — the view-state band it composes runs DIRECTLY against the live control
	// runtime (no model → notifier shim). Desktop-only bodies (the web front runs its own command path).
	void Command_Choose(ibBackendValueForm* srcForm);
	
	void Command_FilterByCurrentColumn();
	void Command_ShowListSettings();
	void Command_ClearFilter();

	void Command_ShowViewMode();

#ifndef OES_USE_WEB
	//events — wxDataView-bound, desktop only
	void OnColumnClick(ibDataViewEvent& event);
	void OnColumnReordered(ibDataViewEvent& event);

	void OnSelectionChanged(ibDataViewEvent& event);

	void OnItemActivated(ibDataViewEvent& event);

	// Double-click / Enter dispatch: choice → select; editable cell → inline editor (front); read-only row → open
	// its value (model's ActivateItem, backend). The Edit COMMAND reaches inline editing via the eStartEditingFlag
	// bit on its id (intercepted in CallAsAction) instead.
	void ActivateRow(const ibDataViewItem& item);
	// Open the inline cell editor on `item`'s first editable cell (front). false if no cell is editable (a list).
	// Called by double-click's ActivateRow AND by CallAsAction when the executed id carries the eStartEditingFlag bit.
	bool EditCurrentRow(const ibDataViewItem& item);
	void OnItemCollapsed(ibDataViewEvent& event);
	void OnItemExpanded(ibDataViewEvent& event);
	void OnItemCollapsing(ibDataViewEvent& event);
	void OnItemExpanding(ibDataViewEvent& event);
	void OnItemStartEditing(ibDataViewEvent& event);
	void OnItemEditingStarted(ibDataViewEvent& event);
	void OnItemEditingDone(ibDataViewEvent& event);
	void OnItemValueChanged(ibDataViewEvent& event);

	void OnItemStartInserting(ibDataViewEvent& event);
	void OnItemStartAdding(ibDataViewEvent& event);
	void OnItemStartDeleting(ibDataViewEvent& event);

	void OnViewSet(ibDataViewEvent& event);

	void OnHeaderResizing(ibHeaderGenericCtrlEvent& event);
	void OnMainWindowClick(wxMouseEvent& event);

#if wxUSE_DRAG_AND_DROP
	void OnItemBeginDrag(ibDataViewEvent& event);
	void OnItemDropPossible(ibDataViewEvent& event);
	void OnItemDrop(ibDataViewEvent& event);
#endif // wxUSE_DRAG_AND_DROP

	void OnCommandMenu(wxCommandEvent& event);
	void OnContextMenu(ibDataViewEvent& event);
#endif // !OES_USE_WEB

private:

#pragma region __property_define_h__

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyBoolean* m_propertyHeader = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("Header"), _("Header"), wxT(""), true);
	ibPropertyUInteger* m_propertyHeaderHeight = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("HeaderHeight"), _("Header height"), wxT(""), 1);
	ibPropertyBoolean* m_propertyFooter = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("Footer"), _("Footer"), wxT(""), false);
	ibPropertyUInteger* m_propertyFooterHeight = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FooterHeight"), _("Footer height"), wxT(""), 1);
	ibPropertyUInteger* m_propertyFreezeRow = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FrezeeRow"), _("Frezee row"), wxT(""), 0);
	ibPropertyUInteger* m_propertyFreezeCol = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryInfo, wxT("FrezeeCol"), _("Frezee column"), wxT(""), 0);
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, wxT("Source"), _("Source"));
	ibPropertyEnum<ibValueEnumTableBoxSelectionMode>* m_propertyRowSelectionMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxSelectionMode>>(m_categoryData, wxT("RowSelectionMode"), _("Row selection mode"), ibDataViewSelectionMode::ibDataViewSelectCell);
	ibPropertyEnum<ibValueEnumTableBoxViewMode>* m_propertyViewMode = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTableBoxViewMode>>(m_categoryData, wxT("ViewMode"), _("View mode"), ibDataViewViewMode::ibDataViewHierarchical);
	// Value-picker flag: when set (or when the bound list-model is a picker), the Select command is composed FIRST.
	ibPropertyBoolean* m_propertyChoiceMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, wxT("ChoiceMode"), _("Choice mode"), wxT(""), false);
	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventSelection = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("Selection"), _("Selection"), _("On double mouse click or pressing of Enter."), wxArrayString{ wxT("Control"), wxT("RowSelected"), wxT("StandardProcessing") });
	ibEventControl* m_eventOnActivateRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnActivateRow"), _("Activate row"), _("When row is activated"), wxArrayString{ {wxT("Control")} });
	ibEventControl* m_eventBeforeAddRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("BeforeAddRow"), _("Before add row"), _("When row addition mode is called"), wxArrayString{ wxT("Control"), wxT("Cancel"), wxT("Clone") });
	ibEventControl* m_eventBeforeDeleteRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("BeforeDeleteRow"), _("Before delete row"), _("When row deletion is called"), wxArrayString{ wxT("Control"), wxT("Cancel") });
	// After-add / after-delete pair — fires for GUI-driven add / delete
	// (via wxEVT_DATAVIEW_ITEM_START_ADDING / _START_DELETING handlers)
	// and for owner-driven createdValue path in OnUpdated that lands a fresh
	// row through ApplyCurrentLine.  Lets script observe creation regardless
	// of source.  _START_INSERTING (Insert / Copy at position) does NOT fire
	// OnAddRow — Copy semantics are distinct from Add.
	ibEventControl* m_eventOnAddRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnAddRow"), _("On add row"), _("When a new row has been inserted"), wxArrayString{ wxT("Control"), wxT("RowAdded") });
	ibEventControl* m_eventOnDeleteRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnDeleteRow"), _("On delete row"), _("When a row has been deleted"), wxArrayString{ wxT("Control"), wxT("RowDeleted") });

#pragma endregion 

	bool m_dataViewCreated, m_dataViewSelected;

	bool m_need_calculate_pos;

	ibValuePtr<ibValueModel> m_tableModel;
	ibValuePtr<ibValueModel::ibValueModelReturnLine> m_tableCurrentLine;
};

class ibValueModelTableBoxColumn : public ibValueControl,
	public ibTypeControlFactory {
	public:
protected:

	bool GetChoiceForm(ibPropertyList* property);

public:

	////////////////////////////////////////////////////////////////////////////////////////

	ibFormID GetModelColumn() const {
		const ibFormID& id = m_model_id != wxNOT_FOUND ? m_model_id : GetSource();
		return id != wxNOT_FOUND ? id : m_controlId;
	}
	void SetModelColumn(const ibFormID& id) { m_model_id = id; }

	////////////////////////////////////////////////////////////////////////////////////////

	void SetSource(const ibSourceId& id) { m_propertySource->SetValue(id); }
	// Full binding path [headAttrId, tableSection, column, ...] — the resolve walks the attribute.
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); }
	// Type-carrying entry: the hops (with any pinned composite types) go in as-is — no id-stripping.
	void SetSource(const ibSourceDescription& desc) { m_propertySource->SetValue(desc); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	// The column's FULL binding path (tablebox prefix + the column's own field id(s)); the
	// tablebox strips its own prefix to get the row-relative tail it walks per row.
	const std::vector<ibSourceHop>& GetSourcePath() const { return m_propertySource->GetValueAsPath(); }

	// The column's bound source as a composer FIELD — its dotted NAME (e.g. "Product.SKU"), row-relative to
	// the bound table. Universal: whatever addresses a column by field (sort, filter, group) uses it. Straight
	// off m_propertySource. MakeString renders the FULL form-rooted path; the composer's queryable is the bound
	// TABLE, so drop the tablebox's own prefix (one name segment per prefix id) to make it row-relative — the
	// same seam ResolveCellValue uses. prefix 0 (model bound directly) drops nothing.
	wxString GetSourceFieldName() const {
		wxString name = m_propertySource->GetValueAsString();
		const ibValueModelTableBox* owner = GetOwner();
		for (size_t prefix = owner != nullptr ? owner->GetSourcePath().size() : 0; prefix > 0; --prefix)
			name = name.AfterFirst(wxT('.'));
		return name;
	}

	////////////////////////////////////////////////////////////////////////////////////////

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	void SetPasswordMode(bool caption) { return m_propertyPasswordMode->SetValue(caption); }
	bool GetPasswordMode() const { return m_propertyPasswordMode->GetValueAsBoolean(); }

	void SetMultilineMode(bool caption) { return m_propertyMultilineMode->SetValue(caption); }
	bool GetMultilineMode() const { return m_propertyMultilineMode->GetValueAsBoolean(); }

	void SetTexteditMode(bool caption) { return m_propertyTexteditMode->SetValue(caption); }
	bool GetTextEditMode() const { return m_propertyTexteditMode->GetValueAsBoolean(); }

	void SetSelectButton(bool caption) { return m_propertySelectButton->SetValue(caption); }
	bool GetSelectButton() const { return m_propertySelectButton->GetValueAsBoolean(); }

	void SetOpenButton(bool caption) { return m_propertyOpenButton->SetValue(caption); }
	bool GetOpenButton() const { return m_propertyOpenButton->GetValueAsBoolean(); }

	void SetClearButton(bool caption) { return m_propertyClearButton->SetValue(caption); }
	bool GetClearButton() const { return m_propertyClearButton->GetValueAsBoolean(); }

	void SetVisibleColumn(bool visible = true) const { m_propertyVisible->SetValue(visible); }
	bool GetVisibleColumn() const { return m_propertyVisible->GetValueAsBoolean(); }

	void SetWidthColumn(int width) const { m_propertyWidth->SetValue(width); }
	int GetWidthColumn() const { return m_propertyWidth->GetValueAsUInteger(); }

	///////////////////////////////////////////////////////////////////////

	ibValueModelTableBox* GetOwner() const { return m_parent->ConvertToType<ibValueModelTableBox>(); }

	ibValueModel::ibValueModelReturnLine* GetCurrentLine() const {
		const ibValueModelTableBox* tableBox = GetOwner();
		return tableBox != nullptr ?
			tableBox->GetCurrentLine() : nullptr;
	}

	///////////////////////////////////////////////////////////////////////

	ibValueModelTableBoxColumn();

	//Get source object
	virtual ibSourceObject* GetSourceObject() const { return GetOwner(); }

	// Own bound source path ([headAttr, table, column]) — its leaf is this column. MUTABLE ref (like
	// GetTypeDesc): read it, or assign to bind (GetSourceDesc() = desc). No separate setter.
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }

	//Get source attribute
	virtual const class ibBackendSourceColumn* GetSourceAttributeObject() const { return m_propertySource->GetSourceAttributeObject(); }
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_reference; }
	virtual ibSourceDataType GetFilterSourceDataType() const { return ibSourceDataType::ibSourceDataType_tableColumn; }

	// Available sources = the owning form's attributes of THIS control's kind (table).
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	//get title
	virtual wxString GetControlTitle() const;

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	virtual bool CanDeleteControl() const;

	//get component type 
	virtual int GetComponentType() const { return COMPONENT_TYPE_ABSTRACT; }

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertyRefresh() override;
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

public:

	//get control value
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());
	virtual bool GetControlValue(ibValue& pvarControlVal) const;

	//choice processing
	virtual void ChoiceProcessing(ibValue& vSelected);

private:

	//events
	void OnSelectButtonPressed(wxCommandEvent& event);
	void OnOpenButtonPressed(wxCommandEvent& event);
	void OnClearButtonPressed(wxCommandEvent& event);

	void OnTextEnter(wxCommandEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	// text processing
	bool TextProcessing(wxTextCtrl* textCtrl, const wxString& strData);

	ibFormID m_model_id;

	ibPropertyCategory* m_categoryInfo = ibPropertyObject::CreatePropertyCategory(wxT("Info"), _("Info"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("Title"), _("Title"), wxT(""));
	ibPropertyBoolean* m_propertyPasswordMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("PasswordMode"), _("Password mode"), _("Mode in which typed characters are replaced with a special character"), false);
	ibPropertyBoolean* m_propertyMultilineMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("MultilineMode"), _("Multiline mode"), _("Multiline mode"), false);
	ibPropertyBoolean* m_propertyTexteditMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInfo, wxT("TexteditMode"), _("Textedit mode"), _("Whether or not text editing is enabled in the text box "), true);

	ibPropertyTString* m_propertyFooterText = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryInfo, wxT("FooterText"), _("Footer text"), wxT(""));

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, wxT("Source"), _("Source"), ibValueTypes::TYPE_STRING);
	ibPropertyList* m_propertyChoiceForm = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryData, wxT("ChoiceForm"), _("Choice form"), &ibValueModelTableBoxColumn::GetChoiceForm);

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyBoolean* m_propertySelectButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonSelect"), _("Select button"), true);
	ibPropertyBoolean* m_propertyClearButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonClear"), _("Clear button"), true);
	ibPropertyBoolean* m_propertyOpenButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonOpen"), _("Open button"), false);

	ibPropertyCategory* m_categoryStyle = ibPropertyObject::CreatePropertyCategory(wxT("Style"), _("Style"));
	ibPropertyUInteger* m_propertyWidth = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryStyle, wxT("Width"), _("Width"), wxDVC_DEFAULT_WIDTH);
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyHeaderAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("HeaderAlign"), _("Header align"), wxALIGN_LEFT);
	ibPropertyEnum<ibValueEnumHorizontalAlignment>* m_propertyFooterAlign = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumHorizontalAlignment>>(m_categoryStyle, wxT("FooterAlign"), _("Footer align"), wxALIGN_LEFT);
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryStyle, wxT("Representation"), _("Representation"), ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyHeaderPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryStyle, wxT("HeaderPicture"), _("Header picture"));
	ibPropertyPicture* m_propertyFooterPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryStyle, wxT("FooterPicture"), _("Footer picture"));

	ibPropertyBoolean* m_propertyVisible = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Visible"), _("Visible"), true);
	ibPropertyBoolean* m_propertyResizable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Resizable"), _("Resizable"), true);
	//ibPropertyBoolean* m_propertySortable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Sortable"), _("Sortable"), false);
	ibPropertyBoolean* m_propertyReorderable = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStyle, wxT("Reorderable"), _("Reorderable"), true);

	ibPropertyCategory* m_propertyEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("OnChange"), _("Change"), wxArrayString{ wxT("Control") });
	ibEventControl* m_eventStartChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("StartChoice"), _("Start choice"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventStartListChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("StartListChoice"), _("Start list choice"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventClearing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("Clearing"), _("Clearing"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("Opening"), _("Opening"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventChoiceProcessing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("ChoiceProcessing"), _("Choice processing"), wxArrayString{ wxT("Control"), wxT("ValueSelected"), wxT("StandartProcessing") });

	friend class ibDataViewValueRenderer;
};

#endif 
