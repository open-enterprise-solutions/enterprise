#ifndef _TABLE_H__
#define _TABLE_H__

#include "window.h"
#include "typeControl.h"

#include "frontend/visualView/dvc/dvc.h"
#include "frontend/win/ctrls/dataView.h"

//********************************************************************************************
//*                                 define commom clsid									     *
//********************************************************************************************

//COMMON TABLE & COLUMN
const class_identifier_t g_controlTableBoxCLSID = string_to_clsid("CT_TABL");
const class_identifier_t g_controlTableBoxColumnCLSID = string_to_clsid("CT_TBLC");

//********************************************************************************************
//*                                 Value TableBox                                           *
//********************************************************************************************

#include <wx/headerctrl.h>

class CValueTableBox : public IValueWindow,
	public ITypeControlAttribute, public ISourceObject {
	wxDECLARE_DYNAMIC_CLASS(CValueTableBox);
protected:
	PropertyCategory* m_categoryData = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertySource = IPropertyObject::CreateSourceProperty(m_categoryData, "source");
	PropertyCategory* m_categoryEvent = IPropertyObject::CreatePropertyCategory("event");
	Event* m_eventSelection = IPropertyObject::CreateEvent(m_categoryEvent, { "selection", "selection", _("On double mouse click or pressing of Enter.") }, { {"control"}, {"rowSelected"}, {"standardProcessing"} });
	Event* m_eventOnActivateRow = IPropertyObject::CreateEvent(m_categoryEvent, { "onActivateRow", "onActivateRow", _("When row is activated") }, { {"control"} });
	Event* m_eventBeforeAddRow = IPropertyObject::CreateEvent(m_categoryEvent, { "beforeAddRow", "beforeAddRow", _("When row addition mode is called") }, { {"control"}, {"cancel"}, {"clone"} });
	Event* m_eventBeforeDeleteRow = IPropertyObject::CreateEvent(m_categoryEvent, { "beforeDeleteRow", "beforeDeleteRow", _("When row deletion is called") }, { {"control"}, {"cancel"} });

private:
	IValueModel* m_tableModel;
	IValueModel::IValueModelReturnLine* m_tableCurrentLine;
	wxSize m_dataViewSize;
public:

	CValueTableBox();
	virtual ~CValueTableBox();

	//Get source object 
	virtual ISourceObject* GetSourceObject() const;

	//get metaData from object 
	virtual IMetaObjectGenericData* GetSourceMetaObject() const;

	//get ref class 
	virtual class_identifier_t GetSourceClassType() const;

	//get form owner 
	virtual CValueForm* GetOwnerForm() const {
		return m_formOwner;
	}

	//get model 
	IValueModel* GetModel() const {
		return m_tableModel;
	}

	//get metaData
	virtual IMetaData* GetMetaData() const;

	//methods & attributes
	virtual void PrepareNames() const;                         //этот метод автоматически вызывается для инициализации имен атрибутов и методов

	virtual bool SetPropVal(const long lPropNum, const CValue& varPropVal);        //установка атрибута
	virtual bool GetPropVal(const long lPropNum, CValue& pvarPropVal);                   //значение атрибута

	//control factory 
	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	//get component type 
	virtual int GetComponentType() const {
		return COMPONENT_TYPE_WINDOW;
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);
	virtual void OnPropertyChanged(Property* property, const wxVariant& oldValue, const wxVariant& newValue);


	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	/**
	* Override actionData
	*/

	virtual actionData_t GetActions(const form_identifier_t& formType);
	virtual void ExecuteAction(const action_identifier_t& lNumAction, IBackendValueForm* srcForm);

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

	virtual bool GetControlValue(CValue& pvarControlVal) const;
	virtual bool SetControlValue(const CValue& varControlVal = CValue());

	//other
	void AddColumn();
	void CreateColumnCollection(wxDataViewCtrl* tableCtrl = nullptr);

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

	void OnHeaderResizing(wxHeaderCtrlEvent& event);
	void OnMainWindowClick(wxMouseEvent& event);

#if wxUSE_DRAG_AND_DROP
	void OnItemBeginDrag(wxDataViewEvent& event);
	void OnItemDropPossible(wxDataViewEvent& event);
	void OnItemDrop(wxDataViewEvent& event);
#endif // wxUSE_DRAG_AND_DROP

	void OnCommandMenu(wxCommandEvent& event);
	void OnContextMenu(wxDataViewEvent& event);

	void OnSize(wxSizeEvent& event);

	// the methods to be called from the window event handlers
	void HandleOnScroll(wxScrollWinEvent& event);

	friend class CValueForm;
	friend class CValueTableBoxColumn;
};

class CValueTableBoxColumn : public IValueControl,
	public ITypeControlAttribute {
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

	PropertyCategory* m_categoryInfo = IPropertyObject::CreatePropertyCategory("info");
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryInfo, "caption", PropertyType::PT_WXSTRING);
	Property* m_propertyPasswordMode = IPropertyObject::CreateProperty(m_categoryInfo, "password_mode", PropertyType::PT_BOOL, false);
	Property* m_propertyMultilineMode = IPropertyObject::CreateProperty(m_categoryInfo, "multiline_mode", PropertyType::PT_BOOL, false);
	Property* m_propertyTexteditMode = IPropertyObject::CreateProperty(m_categoryInfo, "textedit_mode", PropertyType::PT_BOOL, true);

	PropertyCategory* m_categoryData = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertySource = IPropertyObject::CreateSourceProperty(m_categoryData, "source");
	Property* m_propertyChoiceForm = IPropertyObject::CreateProperty(m_categoryData, { "choice_form", "choice form" }, &CValueTableBoxColumn::GetChoiceForm, wxNOT_FOUND);

	PropertyCategory* m_categoryButton = IPropertyObject::CreatePropertyCategory("button");
	Property* m_propertySelectButton = IPropertyObject::CreateProperty(m_categoryButton, "button_select", PropertyType::PT_BOOL, true);
	Property* m_propertyClearButton = IPropertyObject::CreateProperty(m_categoryButton, "button_clear", PropertyType::PT_BOOL, true);
	Property* m_propertyOpenButton = IPropertyObject::CreateProperty(m_categoryButton, "button_open", PropertyType::PT_BOOL, false);

	PropertyCategory* m_categoryStyle = IPropertyObject::CreatePropertyCategory("style");
	Property* m_propertyWidth = IPropertyObject::CreateProperty(m_categoryStyle, "width", PropertyType::PT_UINT, wxDVC_DEFAULT_WIDTH);
	Property* m_propertyAlign = IPropertyObject::CreateProperty(m_categoryStyle, "align", &CValueTableBoxColumn::GetAlign, wxALIGN_LEFT);
	Property* m_propertyIcon = IPropertyObject::CreateProperty(m_categoryStyle, "icon", PropertyType::PT_BITMAP);
	Property* m_propertyVisible = IPropertyObject::CreateProperty(m_categoryStyle, "visible", PropertyType::PT_BOOL, true);
	Property* m_propertyResizable = IPropertyObject::CreateProperty(m_categoryStyle, "resizable", PropertyType::PT_BOOL, true);
	//Property* m_propertySortable = IPropertyObject::CreateProperty(m_categoryStyle, "sortable", PropertyType::PT_BOOL, false);
	Property* m_propertyReorderable = IPropertyObject::CreateProperty(m_categoryStyle, "reorderable", PropertyType::PT_BOOL, true);

	PropertyCategory* m_propertyEvent = IPropertyObject::CreatePropertyCategory("event");
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

	void SetOpenButton(bool caption) {
		return m_propertyOpenButton->SetValue(caption);
	}

	bool GetOpenButton() const {
		return m_propertyOpenButton->GetValueAsBoolean();
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

	//void SetSortableColumn(bool sortable = true) const {
	//	m_propertySortable->SetValue(sortable);
	//}

	//bool GetSortableColumn() const {
	//	return m_propertySortable->GetValueAsBoolean();
	//}

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

	IValueTable::IValueModelReturnLine* GetReturnLine() const {
		CValueTableBox* tableBox = GetOwner();
		wxASSERT(tableBox);
		return tableBox->m_tableCurrentLine;
	}

	///////////////////////////////////////////////////////////////////////

	CValueTableBoxColumn();

	//Get source object 
	virtual ISourceObject* GetSourceObject() const;

	//get form owner 
	virtual CValueForm* GetOwnerForm() const {
		return m_formOwner;
	}

	//get metaData
	virtual IMetaData* GetMetaData() const;

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool firstCreated) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual bool CanDeleteControl() const;

	//get component type 
	virtual int GetComponentType() const {
		return COMPONENT_TYPE_ABSTRACT;
	}

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual void OnPropertyRefresh(class wxPropertyGridManager* pg, class wxPGProperty* pgProperty, Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

public:

	//filter source
	virtual bool FilterSource(const CSourceExplorer& src, const meta_identifier_t& id);

	//get control value
	virtual bool SetControlValue(const CValue& varControlVal = CValue());
	virtual bool GetControlValue(CValue& pvarControlVal) const;

	//choice processing
	virtual void ChoiceProcessing(CValue& vSelected);

protected:

	// text processing
	bool TextProcessing(wxTextCtrl* textCtrl, const wxString& strData);

	//events
	void OnSelectButtonPressed(wxCommandEvent& event);
	void OnOpenButtonPressed(wxCommandEvent& event);
	void OnClearButtonPressed(wxCommandEvent& event);

	void OnTextEnter(wxCommandEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	friend class CValueForm;
	friend class CValueTableBox;
	friend class CValueViewRenderer;
};

#endif 
