#ifndef _COMMON_H_
#define _COMMON_H_

#include "window.h"
#include "typeControl.h"

/////////////////////////////////////////////////////////////////////////////////////
//                                 COMMON ELEMENTS                                 //
/////////////////////////////////////////////////////////////////////////////////////
#include <wx/button.h>

class ibValueButton : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueButton);
public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueButton();

	//get title
	virtual wxString GetControlTitle() const { return GetCaption(); }

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

protected:

	//events 
	void OnButtonPressed(wxCommandEvent& event);

private:
	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryButton, wxT("Title"), _("Title"), wxT("Button"));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryButton, wxT("Representation"), _("Representation"), ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryButton, wxT("Picture"), _("Picture"));

	//event
	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_onButtonPressed = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnButtonPressed"), _("Button pressed"), wxArrayString{ wxT("Control") });
};

#include <wx/stattext.h>

class ibValueStaticText : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueStaticText);
public:

	void SetCaption(const wxString& caption) { m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueStaticText();

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated)override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

protected:
	ibPropertyCategory* m_categoryStaticText = ibPropertyObject::CreatePropertyCategory(wxT("StaticText"), _("Static text"));
	ibPropertyBoolean* m_propertyMarkup = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStaticText, wxT("Markup"), _("Markup"), true);
	ibPropertyUInteger* m_propertyWrap = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryStaticText, wxT("Wrap"), _("Wrap"), 0);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryStaticText, wxT("Title"), _("Title"), wxT("Static text"));
};

#include <wx/textctrl.h>

class ibValueTextCtrl : public ibValueWindow,
	public ibTypeControlFactory {
	wxDECLARE_DYNAMIC_CLASS(ibValueTextCtrl);
public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); }
	ibMetaID GetSource(const ibMetaID& id) { return m_propertySource->GetValueAsSource(); }
	////////////////////////////////////////////////////////////////////////////////////////

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	void SetSelectButton(bool caption) { return m_propertySelectButton->SetValue(caption); }
	bool GetSelectButton() const { return m_propertySelectButton->GetValueAsBoolean(); }

	void SetOpenButton(bool caption) { return m_propertyOpenButton->SetValue(caption); }
	bool GetOpenButton() const { return m_propertyOpenButton->GetValueAsBoolean(); }

	void SetClearButton(bool caption) { return m_propertyClearButton->SetValue(caption); }
	bool GetClearButton() const { return m_propertyClearButton->GetValueAsBoolean(); }

	ibValueTextCtrl();

	//Get source object 
	virtual ibSourceObject* GetSourceObject() const;

	//Get source attribute  
	virtual ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const {
		return m_propertySource->GetSourceAttributeObject();
	}

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	//methods & attributes
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//get title
	virtual wxString GetControlTitle() const;

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

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

	virtual bool HasValueInControl() const {
		return m_propertySource->IsEmptyProperty();
	}

	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

public:

	virtual void ChoiceProcessing(ibValue& vSelected);

protected:

	bool TextProcessing(wxTextCtrl* textCtrl, const wxString& strData);

	//Events:
	void OnTextEnter(wxCommandEvent& event);
	void OnTextUpdated(wxCommandEvent& event);

	void OnKillFocus(wxFocusEvent& event);

	void OnSelectButtonPressed(wxCommandEvent& event);
	void OnOpenButtonPressed(wxCommandEvent& event);
	void OnClearButtonPressed(wxCommandEvent& event);

private:

	bool GetChoiceForm(ibPropertyList* property);
	
	bool m_textModified;
	
	ibValue m_selValue;

	ibPropertyCategory* m_categoryText = ibPropertyObject::CreatePropertyCategory(wxT("Textbox"), _("Textbox"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryText, wxT("Title"), _("Title"), wxT(""));
	ibPropertyBoolean* m_propertyPasswordMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, wxT("PasswordMode"), _("Password mode"), _("Mode in which typed characters are replaced with a special character"), false);
	ibPropertyBoolean* m_propertyMultilineMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, wxT("MultilineMode"), _("Multiline mode"), _("Multiline mode"), false);
	ibPropertyBoolean* m_propertyTexteditMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, wxT("TexteditMode"), _("Textedit mode"), _("Whether or not text editing is enabled in the text box "), true);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, wxT("Source"), _("Source"), ibValueTypes::TYPE_STRING);
	ibPropertyList* m_propertyChoiceForm = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryData, wxT("ChoiceForm"), _("Choice form"), &ibValueTextCtrl::GetChoiceForm, wxNOT_FOUND);

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyBoolean* m_propertySelectButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonSelect"), _("Select button"), true);
	ibPropertyBoolean* m_propertyClearButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonClear"), _("Clear button"), true);
	ibPropertyBoolean* m_propertyOpenButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, wxT("ButtonOpen"), _("Open button"), false);

	ibPropertyCategory* m_propertyEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("OnChange"), _("Change"), wxArrayString{ wxT("Control") });
	ibEventControl* m_eventStartChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("StartChoice"), _("Start choice"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventStartListChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("StartListChoice"), _("Start list choice"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventClearing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("Clearing"), _("Clearing"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("Opening"), _("Opening"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
	ibEventControl* m_eventChoiceProcessing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("ChoiceProcessing"), _("Choice processing"), wxArrayString{ wxT("Control"), wxT("ValueSelected"), wxT("StandartProcessing") });

	friend class ibValueForm;
};

#include <wx/combobox.h>

class ibValueComboBox : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueComboBox);
public:

	ibValueComboBox();

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());
};

#include <wx/choice.h>

class ibValueChoice : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueChoice);
public:

	ibValueChoice();

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());
};

#include <wx/listbox.h>

class ibValueListBox : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueListBox);

public:

	ibValueListBox();

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());
};

#include <wx/checkbox.h>

class ibValueCheckbox : public ibValueWindow,
	public ibTypeControlFactory {
	wxDECLARE_DYNAMIC_CLASS(ibValueCheckbox);
public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	////////////////////////////////////////////////////////////////////////////////////////

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueCheckbox();

	//get source object 
	virtual ibSourceObject* GetSourceObject() const;

	//get source attribute  
	virtual ibValueMetaObjectAttributeBase* GetSourceAttributeObject() const {
		return m_propertySource->GetSourceAttributeObject();
	}

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_boolean;
	}

	//methods & attributes
	virtual void PrepareNames() const;                         // this method is automatically called to initialize attribute and method names.

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//get title
	virtual wxString GetControlTitle() const;

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const wxVariant& newValue);


	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

public:

	virtual bool HasValueInControl() const {
		return m_propertySource->IsEmptyProperty();
	}

	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

protected:

	//events 
	void OnClickedCheckbox(wxCommandEvent& event);

private:

	ibValue m_selValue = false;

	ibPropertyCategory* m_categoryCheckBox = ibPropertyObject::CreatePropertyCategory(wxT("Checkbox"), _("Checkbox"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCheckBox, wxT("Title"), _("Title"), wxT(""));
	ibPropertyEnum<ibValueEnumTitleLocation>* m_propertyTitleLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTitleLocation>>(m_categoryCheckBox, wxT("TitleLocation"), _("Title location"), ibTitleLocation::eLeft);

	ibPropertyCategory* m_categorySource = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryCheckBox, wxT("Source"), _("Source"), ibValueTypes::TYPE_BOOLEAN);

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_onCheckboxClicked = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnCheckboxClicked"), _("Checkbox clicked"), wxArrayString{ wxT("Control") });

	friend class ibValueForm;
};

#include <wx/radiobut.h>

class ibValueRadioButton : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueRadioButton);
public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueRadioButton();

	//get title
	virtual wxString GetControlTitle() const { return GetCaption(); }

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

private:
	ibPropertyCategory* m_categoryRadioButton = ibPropertyObject::CreatePropertyCategory(wxT("RadioButton"), _("Radio button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryRadioButton, wxT("Title"), _("Title"), wxT("Radio button"));
	ibPropertyBoolean* m_propertySelected = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryRadioButton, wxT("Selected"), _("Selected"));
};

#include <wx/statline.h>

class ibValueStaticLine : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueStaticLine);
public:

	ibValueStaticLine();

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

private:
	ibPropertyCategory* m_categoryStaticLine = ibPropertyObject::CreatePropertyCategory(wxT("StaticLine"), _("Static line"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryStaticLine, wxT("Orient"), _("Orient"), wxHORIZONTAL);
};

#include <wx/slider.h>

class ibValueSlider : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueSlider);
public:

	ibValueSlider();

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

private:
	ibPropertyCategory* m_categorySlider = ibPropertyObject::CreatePropertyCategory(wxT("Slider"), _("Slider"));
	ibPropertyInteger* m_propertyMinValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("MinValue"), _("Min value"), 0);
	ibPropertyInteger* m_propertyMaxValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("MaxValue"), _("Max value"), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("Value"), _("Value"), 50);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySlider, wxT("Orient"), _("Orient"), wxHORIZONTAL);
};

#include <wx/gauge.h>

class ibValueGauge : public ibValueWindow {
	wxDECLARE_DYNAMIC_CLASS(ibValueGauge);
public:

	ibValueGauge();

	//control factory
	virtual wxObject* Create(wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost, bool firstСreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, wxWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool LoadData(ibReaderMemory& reader);
	virtual bool SaveData(ibWriterMemory writer = ibWriterMemory());

private:
	ibPropertyCategory* m_categoryGauge = ibPropertyObject::CreatePropertyCategory(wxT("Gauge"), _("Gauge"));
	ibPropertyInteger* m_propertyRange = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, wxT("Range"), _("Range"), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, wxT("Value"), _("Value"), 30);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryGauge, wxT("Orient"), _("Orient"), wxHORIZONTAL);
};

#endif