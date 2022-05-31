#ifndef _TABLE_H__
#define _TABLE_H__

#include "window.h"
#include "attributeControl.h"

#include "frontend/visualView/dvc/dvc.h"
#include "common/tableInfo.h"

class CValueTableBox : public IValueWindow,
	public IAttributeControl {
	wxDECLARE_DYNAMIC_CLASS(CValueTableBox);
private:
	IValueTable* m_tableModel;
	IValueTable::IValueTableReturnLine* m_tableCurrentLine;
public:

	CValueTableBox();
	virtual ~CValueTableBox();

	//Get source object 
	virtual ISourceDataObject* GetSourceObject() const;

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

	virtual wxString GetClassName() const override { return wxT("tablebox"); }
	virtual wxString GetObjectTypeName() const override { return wxT("container"); }

	//get component type 
	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_WINDOW_TABLE;
	}
	
	/**
	* Property events
	*/
	virtual void OnPropertyChanged(Property* property);

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

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
	virtual bool HasValueInControl() const { return true; }

	virtual CValue GetControlValue() const;
	virtual void SetControlValue(CValue& vSelected);

	//other
	void AddColumn();
	void CreateColumns(wxDataViewCtrl* tableCtrl = NULL);

	void CreateTable();

	void CreateModel();
	void RefreshModel();

	//variant
	virtual bool LoadFromVariant(const wxVariant& variant);

	virtual void SaveToVariant(wxVariant& variant, IMetadata* metaData) const;

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
private:

	OptionList* GetAlign(Property* property)
	{
		OptionList* optList = new OptionList();
		optList->AddOption("Left", wxALIGN_LEFT);
		optList->AddOption("Center", wxALIGN_CENTER);
		optList->AddOption("Right", wxALIGN_RIGHT);
		return optList;
	}

	OptionList* GetDateTimeFormat(Property*) {
		OptionList* optList = new OptionList;
		optList->AddOption(_("date"), eDateFractions::eDateFractions_Date);
		optList->AddOption(_("date and time"), eDateFractions::eDateFractions_DateTime);
		optList->AddOption(_("time"), eDateFractions::eDateFractions_Time);
		return optList;
	}

	OptionList* GetChoiceForm(Property* property);

private:
	wxString m_caption;
	bool m_selbutton;
	bool m_listbutton;
	bool m_clearbutton;
	bool m_passwordMode;
	bool m_multilineMode;
	bool m_textEditMode;
	bool m_markup;
	int m_width;
	wxAlignment m_align;
	wxBitmap m_icon;
	bool m_visible;
	bool m_resizable;
	bool m_sortable;
	bool m_reorderable;
public:
	meta_identifier_t m_choiceForm;
public:

	CValueTableBoxColumn();

	//Get source object 
	virtual ISourceDataObject* GetSourceObject() const;

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

	virtual wxString GetClassName() const override { return wxT("tableboxColumn"); }
	virtual wxString GetObjectTypeName() const override { return wxT("tableboxColumn"); }

	virtual bool CanDeleteControl() const;

	//get component type 
	virtual int GetComponentType() const override {
		return COMPONENT_TYPE_TABLE_COLUMN;
	}

	virtual bool IsItem() override {
		return false;
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//read&save propery 
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

public:

	//filter source
	virtual bool FilterSource(const CSourceExplorer& src, const meta_identifier_t& id);

	//get control value
	virtual CValue GetControlValue() const;
	virtual void SetControlValue(CValue& vSelected);

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
