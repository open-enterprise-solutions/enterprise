#ifndef _TABLE_H__
#define _TABLE_H__

#include "window.h"
#include "attributeControl.h"

#include "frontend/visualView/dvc/dvc.h"
#include "common/tableInfo.h"

class CValueTableBox : public IValueWindow,
	public IAttributeControl, public ISourceObject {
	wxDECLARE_DYNAMIC_CLASS(CValueTableBox);
protected:
	PropertyCategory* m_categoryData = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertySource = IPropertyObject::CreateSourceProperty(m_categoryData, "source");
	PropertyCategory* m_categoryEvent = IPropertyObject::CreatePropertyCategory("Event");
	Event* m_eventSelection = IPropertyObject::CreateEvent(m_categoryEvent, { "selection", "selection", _("On double mouse click or pressing of Enter.") }, { {"control"}, {"rowSelected"}, {"standardProcessing"} });
	Event* m_eventOnActivateRow = IPropertyObject::CreateEvent(m_categoryEvent, { "onActivateRow", "onActivateRow", _("When row is activated") }, { {"control"} });
	Event* m_eventBeforeAddRow = IPropertyObject::CreateEvent(m_categoryEvent, { "beforeAddRow", "beforeAddRow", _("When row addition mode is called") }, { {"control"}, {"cancel"}, {"clone"} });
	Event* m_eventBeforeDeleteRow = IPropertyObject::CreateEvent(m_categoryEvent, { "beforeDeleteRow", "beforeDeleteRow", _("When row deletion is called") }, { {"control"}, {"cancel"} });

private:
	IValueModel* m_tableModel;
	IValueModel::IValueModelReturnLine* m_tableCurrentLine;
public:

	CValueTableBox();
	virtual ~CValueTableBox();

	//Get source object 
	virtual ISourceObject* GetSourceObject() const;

	//get metadata from object 
	virtual IMetaObjectWrapperData* GetMetaObject() const;

	//Get ref class 
	virtual CLASS_ID GetClassType() const;

	//get form owner 
	virtual CValueForm* GetOwnerForm() const {
		return m_formOwner;
	}

	//get metadata
	virtual IMetadata* GetMetadata() const {
		return GetMetaData();
	}

	//methods & attributes
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	virtual void SetAttribute(attributeArg_t& aParams, CValue& cVal);        //установка атрибута
	virtual CValue GetAttribute(attributeArg_t& aParams);                   //значение атрибута

	//control factory 
	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("tablebox");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("container");
	}

	//get component type 
	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_WINDOW_TABLE;
	}

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property);

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	/**
	* Override actions
	*/

	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& action, CValueForm* srcForm);

	/**
	* Support default menu
	*/
	virtual void PrepareDefaultMenu(wxMenu* m_menu);
	virtual void ExecuteMenu(IVisualHost* visualHost, int id);

	// filter data 
	virtual bool FilterSource(const CSourceExplorer& src, const meta_identifier_t& id);

	//contol value
	virtual bool HasValueInControl() const {
		return true;
	}

	virtual CValue GetControlValue() const;
	virtual void SetControlValue(CValue& vSelected = CValue());

	//other
	void AddColumn();
	void CreateColumns(wxDataViewCtrl* tableCtrl = NULL);

	void CreateTable();

	void CreateModel();
	void RefreshModel();

protected:

	//events 
	void OnColumnClick(wxDataViewEvent& event);
	void OnColumnReordered(wxDataViewEvent& event);

	void OnSelectionChanged(wxDataViewEvent& event);

	void OnItemActivated(wxDataViewEvent& event);
	void OnItemCollapsed(wxDataViewEvent& event);
	void OnItemExpanded(wxDataViewEvent& event);
	void OnItemCollapsing(wxDataViewEvent& event);
	void OnItemExpanding(wxDataViewEvent& event);
	void OnItemStartEditing(wxDataViewEvent& event);
	void OnItemEditingStarted(wxDataViewEvent& event);
	void OnItemEditingDone(wxDataViewEvent& event);
	void OnItemValueChanged(wxDataViewEvent& event);
	void OnItemStartDeleting(wxDataViewEvent& event);

#if wxUSE_DRAG_AND_DROP
	void OnItemBeginDrag(wxDataViewEvent& event);
	void OnItemDropPossible(wxDataViewEvent& event);
	void OnItemDrop(wxDataViewEvent& event);
#endif // wxUSE_DRAG_AND_DROP

	void OnCommandMenu(wxCommandEvent& event);
	void OnContextMenu(wxDataViewEvent& event);

	friend class CValueForm;
	friend class CValueTableBoxColumn;

protected:

	bool m_refreshModel;
};

class CValueTableBoxColumn : public IValueControl,
	public IAttributeControl {
	wxDECLARE_DYNAMIC_CLASS(CValueTableBoxColumn);
protected:

	OptionList* GetChoiceForm(PropertyOption* property);
	OptionList* GetAlign(PropertyOption* property) {
		OptionList* optList = new OptionList();
		optList->AddOption(_("left"), wxALIGN_LEFT);
		optList->AddOption(_("center"), wxALIGN_CENTER);
		optList->AddOption(_("right"), wxALIGN_RIGHT);
		return optList;
	}

	PropertyCategory* m_categoryInfo = IPropertyObject::CreatePropertyCategory("Info");
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryInfo, "caption", PropertyType::PT_WXSTRING);
	Property* m_propertyPasswordMode = IPropertyObject::CreateProperty(m_categoryInfo, "password_mode", PropertyType::PT_BOOL, false);
	Property* m_propertyMultilineMode = IPropertyObject::CreateProperty(m_categoryInfo, "multiline_mode", PropertyType::PT_BOOL, false);
	Property* m_propertyTexteditMode = IPropertyObject::CreateProperty(m_categoryInfo, "textedit_mode", PropertyType::PT_BOOL, true);

	PropertyCategory* m_categoryData = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertySource = IPropertyObject::CreateSourceProperty(m_categoryData, "source");
	Property* m_propertyChoiceForm = IPropertyObject::CreateProperty(m_categoryData, "choice_form", &CValueTableBoxColumn::GetChoiceForm, wxNOT_FOUND);

	PropertyCategory* m_categoryButton = IPropertyObject::CreatePropertyCategory("Button");
	Property* m_propertySelectButton = IPropertyObject::CreateProperty(m_categoryButton, "button_select", PropertyType::PT_BOOL, true);
	Property* m_propertyListButton = IPropertyObject::CreateProperty(m_categoryButton, "button_list", PropertyType::PT_BOOL, false);
	Property* m_propertyClearButton = IPropertyObject::CreateProperty(m_categoryButton, "button_clear", PropertyType::PT_BOOL, true);

	PropertyCategory* m_categoryStyle = IPropertyObject::CreatePropertyCategory("Style");
	Property* m_propertyWidth = IPropertyObject::CreateProperty(m_categoryStyle, "width", PropertyType::PT_UINT, wxDVC_DEFAULT_WIDTH);
	Property* m_propertyAlign = IPropertyObject::CreateProperty(m_categoryStyle, "align", &CValueTableBoxColumn::GetAlign, wxALIGN_LEFT);
	Property* m_propertyIcon = IPropertyObject::CreateProperty(m_categoryStyle, "icon", PropertyType::PT_BITMAP);
	Property* m_propertyVisible = IPropertyObject::CreateProperty(m_categoryStyle, "visible", PropertyType::PT_BOOL, true);
	Property* m_propertyResizable = IPropertyObject::CreateProperty(m_categoryStyle, "resizable", PropertyType::PT_BOOL, true);
	Property* m_propertySortable = IPropertyObject::CreateProperty(m_categoryStyle, "sortable", PropertyType::PT_BOOL, false);
	Property* m_propertyReorderable = IPropertyObject::CreateProperty(m_categoryStyle, "reorderable", PropertyType::PT_BOOL, true);

	PropertyCategory* m_propertyEvent = IPropertyObject::CreatePropertyCategory("Event");
	Event* m_eventOnChange = IPropertyObject::CreateEvent(m_propertyEvent, "onChange", { "control" });
	Event* m_eventStartChoice = IPropertyObject::CreateEvent(m_propertyEvent, "startChoice", { "control", "standartProcessing" });
	Event* m_eventStartListChoice = IPropertyObject::CreateEvent(m_propertyEvent, "startListChoice", { "control", "standartProcessing" });
	Event* m_eventClearing = IPropertyObject::CreateEvent(m_propertyEvent, "clearing", { "control", "standartProcessing" });
	Event* m_eventOpening = IPropertyObject::CreateEvent(m_propertyEvent, "opening", { "control", "standartProcessing" });
	Event* m_eventChoiceProcessing = IPropertyObject::CreateEvent(m_propertyEvent, "choiceProcessing", { "control", "valueSelected", "standartProcessing" });

public:

	void SetCaption(const wxString& caption) {
		return m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	void SetPasswordMode(bool caption) {
		return m_propertyPasswordMode->SetValue(caption);
	}

	bool GetPasswordMode() const {
		return m_propertyPasswordMode->GetValueAsBoolean();
	}

	void SetMultilineMode(bool caption) {
		return m_propertyMultilineMode->SetValue(caption);
	}

	bool GetMultilineMode() const {
		return m_propertyMultilineMode->GetValueAsBoolean();
	}

	void SetTexteditMode(bool caption) {
		return m_propertyTexteditMode->SetValue(caption);
	}

	bool GetTextEditMode() const {
		return m_propertyTexteditMode->GetValueAsBoolean();
	}

	void SetSelectButton(bool caption) {
		return m_propertySelectButton->SetValue(caption);
	}

	bool GetSelectButton() const {
		return m_propertySelectButton->GetValueAsBoolean();
	}

	void SetListButton(bool caption) {
		return m_propertyListButton->SetValue(caption);
	}

	bool GetListButton() const {
		return m_propertyListButton->GetValueAsBoolean();
	}

	void SetClearButton(bool caption) {
		return m_propertyClearButton->SetValue(caption);
	}

	bool GetClearButton() const {
		return m_propertyClearButton->GetValueAsBoolean();
	}

	void SetVisibleColumn(bool visible = true) const {
		m_propertyVisible->SetValue(visible);
	}

	bool GetVisibleColumn() const {
		return m_propertyVisible->GetValueAsBoolean();
	}

	void SetSortableColumn(bool sortable = true) const {
		m_propertySortable->SetValue(sortable);
	}

	bool GetSortableColumn() const {
		return m_propertySortable->GetValueAsBoolean();
	}

	void SetWidthColumn(int width) const {
		m_propertyWidth->SetValue(width);
	}

	int GetWidthColumn() const {
		return m_propertyWidth->GetValueAsInteger();
	}

	///////////////////////////////////////////////////////////////////////

	CValueTableBox* GetOwner() const {
		return dynamic_cast<CValueTableBox*>(m_parent);
	}
	///////////////////////////////////////////////////////////////////////

	CValueTableBoxColumn();

	//Get source object 
	virtual ISourceObject* GetSourceObject() const;

	//get form owner 
	virtual CValueForm* GetOwnerForm() const {
		return m_formOwner;
	}

	//get metadata
	virtual IMetadata* GetMetadata() const {
		return GetMetaData();
	}

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstCreated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("tableboxColumn");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("tableboxColumn");
	}

	virtual bool CanDeleteControl() const;

	//get component type 
	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_TABLE_COLUMN;
	}

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

public:

	//filter source
	virtual bool FilterSource(const CSourceExplorer& src, const meta_identifier_t& id);

	//get control value
	virtual CValue GetControlValue() const;
	virtual void SetControlValue(CValue& vSelected = CValue());

	//choice processing
	virtual void ChoiceProcessing(CValue& vSelected);

protected:

	// text processing
	bool TextProcessing(CValue& selValue, const wxString& strData);

	//events
	void OnSelectButtonPressed(wxCommandEvent& event);
	void OnListButtonPressed(wxCommandEvent& event);
	void OnClearButtonPressed(wxCommandEvent& event);

	void OnTextEnter(wxCommandEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	friend class CValueForm;
	friend class CValueTableBox;
	friend class CValueViewRenderer;
};

#endif 
