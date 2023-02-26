#ifndef _COMMON_H_
#define _COMMON_H_

#include "window.h"
#include "controlAttribute.h"

/////////////////////////////////////////////////////////////////////////////////////
//                                 COMMON ELEMENTS                                 //
/////////////////////////////////////////////////////////////////////////////////////
#include <wx/button.h>

class CValueButton : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueButton);
protected:
	PropertyCategory* m_categoryButton = IPropertyObject::CreatePropertyCategory({ "button", "button" });
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryButton, "caption", PropertyType::PT_WXSTRING, _("My button"));
	Property* m_propertyIcon = IPropertyObject::CreateProperty(m_categoryButton, "icon", PropertyType::PT_BITMAP);
	//event
	PropertyCategory* m_categoryEvent = IPropertyObject::CreatePropertyCategory("Event");
	Event* m_onButtonPressed = IPropertyObject::CreateEvent(m_categoryEvent, { "onButtonPressed", "onButtonPressed", _("On button pressed") }, { {"control"} });

public:

	void SetCaption(const wxString& caption) {
		return m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	CValueButton();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("button");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

protected:
	//events 
	void OnButtonPressed(wxCommandEvent& event);
};

#include <wx/stattext.h>

class CValueStaticText : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueStaticText);
protected:
	PropertyCategory* m_categoryStaticText = IPropertyObject::CreatePropertyCategory({ "staticText", _("static text") });
	Property* m_propertyMarkup = IPropertyObject::CreateProperty(m_categoryStaticText, "markup", PropertyType::PT_BOOL, false);
	Property* m_propertyWrap = IPropertyObject::CreateProperty(m_categoryStaticText, "wrap", PropertyType::PT_INT, 0);
	Property* m_propertyLabel = IPropertyObject::CreateProperty(m_categoryStaticText, "label", PropertyType::PT_WXSTRING, _("My label"));
public:

	CValueStaticText();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("statictext");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/textctrl.h>

class CValueTextCtrl : public IValueWindow,
	public ITypeControl {
	wxDECLARE_DYNAMIC_CLASS(CValueTextCtrl);
protected:
	CValue m_selValue; bool m_textModified; 
protected:
	OptionList* GetChoiceForm(PropertyOption* property);
protected:
	PropertyCategory* m_categoryText = IPropertyObject::CreatePropertyCategory({ "textControl",  _("text control") });
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryText, "caption", PropertyType::PT_WXSTRING);
	Property* m_propertyPasswordMode = IPropertyObject::CreateProperty(m_categoryText, "password_mode", PropertyType::PT_BOOL, false);
	Property* m_propertyMultilineMode = IPropertyObject::CreateProperty(m_categoryText, "multiline_mode", PropertyType::PT_BOOL, false);
	Property* m_propertyTexteditMode = IPropertyObject::CreateProperty(m_categoryText, "textedit_mode", PropertyType::PT_BOOL, true);

	PropertyCategory* m_categorySource = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertySource = IPropertyObject::CreateSourceProperty(m_categorySource, { "source" });
	Property* m_propertyChoiceForm = IPropertyObject::CreateProperty(m_categorySource, { "choice_form", "choice form" }, &CValueTextCtrl::GetChoiceForm, wxNOT_FOUND);

	PropertyCategory* m_categoryButton = IPropertyObject::CreatePropertyCategory("button");
	Property* m_propertySelectButton = IPropertyObject::CreateProperty(m_categoryButton, "button_select", PropertyType::PT_BOOL, true);
	Property* m_propertyClearButton = IPropertyObject::CreateProperty(m_categoryButton, "button_clear", PropertyType::PT_BOOL, true);
	Property* m_propertyOpenButton = IPropertyObject::CreateProperty(m_categoryButton, "button_open", PropertyType::PT_BOOL, false);

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

	CValueTextCtrl();

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

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("textctrl");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
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

	virtual bool HasValueInControl() const {
		return true;
	}

	virtual bool GetControlValue(CValue& pvarControlVal) const;
	virtual bool SetControlValue(const CValue& varControlVal = CValue());

public:

	virtual void ChoiceProcessing(CValue& vSelected);

protected:

	bool TextProcessing(wxTextCtrl* textCtrl, const wxString& strData);

	//Events:
	void OnTextEnter(wxCommandEvent& event);
	void OnTextUpdated(wxCommandEvent& event);

	void OnKillFocus(wxFocusEvent& event);

	void OnSelectButtonPressed(wxCommandEvent& event);
	void OnOpenButtonPressed(wxCommandEvent& event);
	void OnClearButtonPressed(wxCommandEvent& event);

	friend class CValueForm;
};

#include <wx/combobox.h>

class CValueComboBox : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueComboBox);
public:

	CValueComboBox();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("combobox");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/choice.h>

class CValueChoice : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueChoice);
public:

	CValueChoice();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("choice");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/listbox.h>

class CValueListBox : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueListBox);

public:

	CValueListBox();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("listbox");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/checkbox.h>

enum enTitleLocation {
	eLeft = 1,
	eRight
};

class CValueCheckbox : public IValueWindow,
	public ITypeControl {
	wxDECLARE_DYNAMIC_CLASS(CValueCheckbox);

	OptionList* GetTitleLocation(PropertyOption*) {
		OptionList* optList = new OptionList;
		optList->AddOption(_("left"), enTitleLocation::eLeft);
		optList->AddOption(_("right"), enTitleLocation::eRight);
		return optList;
	}

	CValue m_selValue = false;

	PropertyCategory* m_categoryCheckBox = IPropertyObject::CreatePropertyCategory({ "checkbox", _("checkbox") });
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryCheckBox, "caption", PropertyType::PT_WXSTRING, _("Check Me!"));
	Property* m_propertyTitle = IPropertyObject::CreateProperty(m_categoryCheckBox, "title_location", &CValueCheckbox::GetTitleLocation, enTitleLocation::eRight);

	PropertyCategory* m_categorySource = IPropertyObject::CreatePropertyCategory("data");
	Property* m_propertySource = IPropertyObject::CreateSourceProperty(m_categoryCheckBox, "source");

	PropertyCategory* m_categoryEvent = IPropertyObject::CreatePropertyCategory({ "event", "event" });
	Event* m_onCheckboxClicked = IPropertyObject::CreateEvent(m_categoryEvent, "onCheckboxClicked", { { "control" } });

public:

	void SetCaption(const wxString& caption) {
		return m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	CValueCheckbox();

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

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("checkbox");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(Property* property);
	virtual bool OnPropertyChanging(Property* property, const wxVariant& newValue);


	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

public:

	virtual bool HasValueInControl() const { return true; }

	virtual bool GetControlValue(CValue& pvarControlVal) const;
	virtual bool SetControlValue(const CValue& varControlVal = CValue());

protected:

	//events 
	void OnClickedCheckbox(wxCommandEvent& event);

protected:

	friend class CValueForm;
};

#include <wx/radiobut.h>

class CValueRadioButton : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueRadioButton);
protected:
	PropertyCategory* m_categoryRadioButton = IPropertyObject::CreatePropertyCategory({ "radioButton", _("radio button") });
	Property* m_propertyCaption = IPropertyObject::CreateProperty(m_categoryRadioButton, "caption", PropertyType::PT_WXSTRING, _("RadioBtn"));
	Property* m_propertySelected = IPropertyObject::CreateProperty(m_categoryRadioButton, "selected", PropertyType::PT_BOOL);
public:

	void SetCaption(const wxString& caption) {
		return m_propertyCaption->SetValue(caption);
	}

	wxString GetCaption() const {
		return m_propertyCaption->GetValueAsString();
	}

	CValueRadioButton();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("radiobutton");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/statline.h>

class CValueStaticLine : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueStaticLine);
protected:
	PropertyCategory* m_categoryStaticLine = IPropertyObject::CreatePropertyCategory({ "staticLine", "static line" });
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categoryStaticLine, "orient", &CValueStaticLine::GetOrient, wxLI_HORIZONTAL);
public:

	CValueStaticLine();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("staticline");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/slider.h>

class CValueSlider : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueSlider);
protected:
	PropertyCategory* m_categorySlider = IPropertyObject::CreatePropertyCategory({ "slider", _("slider") });
	Property* m_propertyMinValue = IPropertyObject::CreateProperty(m_categorySlider, "minvalue", PropertyType::PT_INT, 0);
	Property* m_propertyMaxValue = IPropertyObject::CreateProperty(m_categorySlider, "maxvalue", PropertyType::PT_INT, 100);
	Property* m_propertyValue = IPropertyObject::CreateProperty(m_categorySlider, "value", PropertyType::PT_INT, 50);
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categorySlider, "orient", &IValueFrame::GetOrient, wxGA_HORIZONTAL);
public:

	CValueSlider();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("slider");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#include <wx/gauge.h>

class CValueGauge : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueGauge);
protected:
	PropertyCategory* m_categoryGauge = IPropertyObject::CreatePropertyCategory({ "gauge", _("gauge") });
	Property* m_propertyRange = IPropertyObject::CreateProperty(m_categoryGauge, "range", PropertyType::PT_INT, 100);
	Property* m_propertyValue = IPropertyObject::CreateProperty(m_categoryGauge, "value", PropertyType::PT_INT, 30);
	Property* m_propertyOrient = IPropertyObject::CreateProperty(m_categoryGauge, "orient", &IValueFrame::GetOrient, wxGA_HORIZONTAL);
public:

	CValueGauge();

	virtual wxObject* Create(wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("gauge");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//support icons
	virtual wxIcon GetIcon();
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());
};

#endif