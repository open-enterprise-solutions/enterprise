#ifndef _COMMON_H_
#define _COMMON_H_

#include "window.h"
#include "attributeControl.h"

/////////////////////////////////////////////////////////////////////////////////////
//                                 COMMON ELEMENTS                                 //
/////////////////////////////////////////////////////////////////////////////////////
#include <wx/button.h>

class CValueButton : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueButton);
protected:
	long m_style = 0;
	wxString m_caption = "MyButton";
	bool m_markup = false;
	bool m_default = false;

	wxSize m_position;
	wxSize m_margins;

	wxBitmap m_bitmap;

public:

	CValueButton();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("button"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

protected:
	//events 
	void OnButtonPressed(wxCommandEvent& event);
};

#include <wx/stattext.h>

class CValueStaticText : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueStaticText);
protected:
	long m_style = 0;
	wxString m_label = "MyLabel";
	bool m_markup = false;
	int m_wrap = 0;

public:

	CValueStaticText();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated)override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("statictext"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#include <wx/textctrl.h>

class CValueTextCtrl : public IValueWindow,
	public IAttributeControl {
	wxDECLARE_DYNAMIC_CLASS(CValueTextCtrl);
private:
	long m_style = 0;
	wxString m_caption = "";
	bool m_passwordMode;
	bool m_multilineMode;
	bool m_textEditMode;
	bool m_selbutton;
	bool m_listbutton;
	bool m_clearbutton;
	CValue m_selValue;
	meta_identifier_t m_choiceForm;
protected:

	OptionList* GetDateTimeFormat(Property*) {
		OptionList* optList = new OptionList;
		optList->AddOption(_("date"), eDateFractions::eDateFractions_Date);
		optList->AddOption(_("date and time"), eDateFractions::eDateFractions_DateTime);
		optList->AddOption(_("time"), eDateFractions::eDateFractions_Time);
		return optList;
	}

	OptionList* GetChoiceForm(Property* property);

public:

	CValueTextCtrl();

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
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override {
		return wxT("textctrl");
	}

	virtual wxString GetObjectTypeName() const override {
		return wxT("widget");
	}

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//read & save property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

public:

	virtual bool HasValueInControl() const { return true; }

	virtual CValue GetControlValue() const;
	virtual void SetControlValue(CValue& vSelected);

public:

	virtual void ChoiceProcessing(CValue& vSelected);

protected:

	bool TextProcessing(const wxString& strData);

	//Events:
	void OnTextEnter(wxCommandEvent& event);
	void OnKillFocus(wxFocusEvent& event);

	void OnSelectButtonPressed(wxCommandEvent& event);
	void OnListButtonPressed(wxCommandEvent& event);
	void OnClearButtonPressed(wxCommandEvent& event);

	friend class CValueForm;
};

#include <wx/combobox.h>

class CValueComboBox : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueComboBox);
protected:
	long m_style = 0;
	wxString m_value = "Combo!";
	wxArrayString m_choices;
	int m_selection;

public:

	CValueComboBox();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("combobox"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#include <wx/choice.h>

class CValueChoice : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueChoice);
protected:
	long m_style = 0;
	wxArrayString m_choices;
	int m_selection;

public:

	CValueChoice();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("choice"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#include <wx/listbox.h>

class CValueListBox : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueListBox);
protected:
	long m_style = 0;
	wxArrayString m_choices;

public:

	CValueListBox();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("listbox"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#include <wx/checkbox.h>

class CValueCheckbox : public IValueWindow,
	public IAttributeControl {
	wxDECLARE_DYNAMIC_CLASS(CValueCheckbox);

	OptionList* GetTitleLocation(Property*) {
		OptionList* optList = new OptionList;
		optList->AddOption(_("Left"), 1);
		optList->AddOption(_("Right"), 2);
		return optList;
	}
protected:
	wxString m_caption = "Check Me!";
	CValue m_selValue;
	int m_titleLocation;
public:

	CValueCheckbox();

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
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("checkbox"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;

public:

	virtual bool HasValueInControl() const { return true; }

	virtual CValue GetControlValue() const;
	virtual void SetControlValue(CValue& vSelected);

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
	long m_style = 0;
	wxString m_caption = "RadioBtn";
	bool m_selected;
public:

	CValueRadioButton();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("radiobutton"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#include <wx/statline.h>

class CValueStaticLine : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueStaticLine);
protected:
	long m_style = wxLI_HORIZONTAL;
public:

	CValueStaticLine();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("staticline"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#include <wx/slider.h>

class CValueSlider : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueSlider);
protected:
	long m_style = wxSL_HORIZONTAL;
	int m_value = 50;
	int m_minValue = 0;
	int m_maxValue = 100;
public:

	CValueSlider();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("slider"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#include <wx/gauge.h>

class CValueGauge : public IValueWindow {
	wxDECLARE_DYNAMIC_CLASS(CValueGauge);
protected:
	long m_style = wxGA_HORIZONTAL;
	int m_range;
	int m_value;
public:

	CValueGauge();

	virtual wxObject* Create(wxObject* parent, IVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, wxWindow* wxparent, IVisualHost* visualHost, bool first—reated) override;
	virtual void Update(wxObject* wxobject, IVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, IVisualHost* visualHost) override;

	virtual wxString GetClassName() const override { return wxT("gauge"); }
	virtual wxString GetObjectTypeName() const override { return wxT("widget"); }

	//load & save object in control 
	virtual bool LoadData(CMemoryReader& reader);
	virtual bool SaveData(CMemoryWriter& writer = CMemoryWriter());

	//property
	virtual void ReadProperty() override;
	virtual void SaveProperty() override;
};

#endif