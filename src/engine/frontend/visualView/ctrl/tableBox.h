#ifndef __TABLE_BOX_H__
#define __TABLE_BOX_H__

#include "backend/tableInfo.h"

#include "frontend/visualView/ctrl/window.h"
#include "frontend/visualView/ctrl/typeControl.h"

#include "frontend/win/ctrls/tableView.h"

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON TABLE & COLUMN
const ibClassID g_controlTableBoxCLSID = string_to_clsid("CT_TABL");
const ibClassID g_controlTableBoxColumnCLSID = string_to_clsid("CT_TBLC");

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
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumTableBoxSelectionMode);
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
	wxDECLARE_DYNAMIC_CLASS(ibValueEnumTableBoxViewMode);
};

class ibValueModelTableBox : public ibValueWindow,
	public ibTypeControlFactory, public ibSourceObject {
	wxDECLARE_DYNAMIC_CLASS(ibValueModelTableBox);
public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); ibValueModelTableBox::RefreshModel(true); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	////////////////////////////////////////////////////////////////////////////////////////

	ibValueModelTableBox();
	virtual ~ibValueModelTableBox() {}

	//Get source attribute  
	virtual ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const { return m_propertySource->GetSourceAttributeObject(); }
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_table; }
	virtual ibSourceDataType GetFilterSourceDataType() const { return ibSourceDataType::ibSourceDataType_table; }

	//Get source object 
	virtual ibSourceObject* GetSourceObject() const;

#pragma region _source_data_

	//get metaData from object 
	virtual ibValueMetaObjectCompositeData* GetSourceMetaObject() const;
	//get ref class 
	virtual ibClassID GetSourceClassType() const;
	//Get presentation 
	virtual wxString GetSourceCaption() const { return GetString(); }

#pragma endregion 

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get model 
	ibValueModel* GetModel() const { return m_tableModel; }

	//get metaData
	virtual ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const {
		return m_propertySource->GetValueAsTypeDesc();
	}

	//methods & attributes
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

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
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//get component type 
	virtual int GetComponentType() const { return COMPONENT_TYPE_WINDOW; }

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
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

	/**
	* Override actionData
	*/

	virtual ibActionCollection GetActionCollection(const ibFormID& formType);
	virtual void ExecuteAction(const ibActionID& lNumAction, ibBackendValueForm* srcForm);

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu);
	virtual void ExecuteMenu(ibVisualHost* visualHost, int id);

	// filter data 
	virtual bool FilterSource(const ibSourceExplorer& src, const ibMetaID& id) const;

	//contol value
	virtual bool HasValueInControl() const {
		return m_propertySource->IsEmptyProperty();
	}

	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

	//other
	void AddColumn();
	void CreateColumnCollection(ibDataViewCtrl* tableCtrl = nullptr);

	void CreateTable(bool recreateModel = false);

	void CreateModel(bool recreateModel = false);
	void RefreshModel(bool recreateModel = false);

	// get current line if exist 
	ibValueModel::ibValueModelReturnLine* GetCurrentLine() const { return m_tableCurrentLine; }

	void SetCalculateColumnPos() { m_need_calculate_pos = true; }

protected:

	virtual void OnChangeChildPosition(ibValueFrame* obj, unsigned int pos) { SetCalculateColumnPos(); }

	void CalculateColumnPos();

	//events 
	void OnColumnClick(ibDataViewEvent& event);
	void OnColumnReordered(ibDataViewEvent& event);

	void OnSelectionChanged(ibDataViewEvent& event);

	void OnItemActivated(ibDataViewEvent& event);
	void OnItemCollapsed(ibDataViewEvent& event);
	void OnItemExpanded(ibDataViewEvent& event);
	void OnItemCollapsing(ibDataViewEvent& event);
	void OnItemExpanding(ibDataViewEvent& event);
	void OnItemStartEditing(ibDataViewEvent& event);
	void OnItemEditingStarted(ibDataViewEvent& event);
	void OnItemEditingDone(ibDataViewEvent& event);
	void OnItemValueChanged(ibDataViewEvent& event);

	void OnItemStartInserting(ibDataViewEvent& event);
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

	void OnSize(wxSizeEvent& event);
	void OnIdle(wxIdleEvent& event);

	// the methods to be called from the window event handlers
	void HandleOnScroll(wxScrollWinEvent& event);

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
	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventSelection = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("Selection"), _("Selection"), _("On double mouse click or pressing of Enter."), wxArrayString{ wxT("Control"), wxT("RowSelected"), wxT("StandardProcessing") });
	ibEventControl* m_eventOnActivateRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnActivateRow"), _("Activate row"), _("When row is activated"), wxArrayString{ {wxT("Control")} });
	ibEventControl* m_eventBeforeAddRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("BeforeAddRow"), _("Before add row"), _("When row addition mode is called"), wxArrayString{ wxT("Control"), wxT("Cancel"), wxT("Clone") });
	ibEventControl* m_eventBeforeDeleteRow = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("BeforeDeleteRow"), _("Before delete row"), _("When row deletion is called"), wxArrayString{ wxT("Control"), wxT("Cancel") });

#pragma endregion 

	bool m_dataViewCreated, m_dataViewUpdated,
		m_dataViewSelected, m_dataViewSizeChanged;

	bool m_need_calculate_pos;

	wxSize m_dataViewSize;

	ibValuePtr<ibValueModel> m_tableModel;
	ibValuePtr<ibValueModel::ibValueModelReturnLine> m_tableCurrentLine;
};

class ibValueModelTableBoxColumn : public ibValueControl,
	public ibTypeControlFactory {
	wxDECLARE_DYNAMIC_CLASS(ibValueModelTableBoxColumn);
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

	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }

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

	ibValueModelTableBase::ibValueModelReturnLine* GetCurrentLine() const {
		const ibValueModelTableBox* tableBox = GetOwner();
		return tableBox != nullptr ?
			tableBox->GetCurrentLine() : nullptr;
	}

	///////////////////////////////////////////////////////////////////////

	ibValueModelTableBoxColumn();

	//Get source object 
	virtual ibSourceObject* GetSourceObject() const { return GetOwner(); }

	//Get source attribute  
	virtual ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const { return m_propertySource->GetSourceAttributeObject(); }
	virtual ibSelectorDataType GetFilterDataType() const { return ibSelectorDataType::ibSelectorDataType_reference; }
	virtual ibSourceDataType GetFilterSourceDataType() const { return ibSourceDataType::ibSourceDataType_tableColumn; }

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	//get title
	virtual wxString GetControlTitle() const;

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
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
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

public:

	//filter source
	virtual bool FilterSource(const ibSourceExplorer& src, const ibMetaID& id) const;

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