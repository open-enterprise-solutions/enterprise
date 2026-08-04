#ifndef _COMMON_H_
#define _COMMON_H_

#include "window.h"
#include "typeControl.h"
#include "backend/sourceDescription.h"   // ibSourceDescription (full [head, field] binding path)
#include "backend/propertyManager/property/propertyCommandSource.h"   // ibPropertyCommandSource — assign a COMMAND to the button
#include "frontend/visualView/layers/commandReceiver.h"             // ibFrontendCommandReceiver — the command door the button IS-A (IS-A ibBackendCommandReceiver too)

/////////////////////////////////////////////////////////////////////////////////////
//                                 COMMON ELEMENTS                                 //
/////////////////////////////////////////////////////////////////////////////////////
#include <wx/button.h>

// ibValueButton's control clsid — kind-typed identity (mirror g_controlTableBoxCLSID), so a control's TYPE is
// read from its clsid, never via dynamic_cast.
constexpr ibClassID g_controlButtonCLSID = control_to_clsid("CT_BUTN");

class ibValueButton : public ibValueWindow, public ibFrontendCommandReceiver {
	public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueButton();

	//get title
	virtual wxString GetControlTitle() const { return GetCaption(); }

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;


protected:

	//events
	void OnButtonPressed(wxCommandEvent& event);

private:
	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(wxT("Button"), _("Button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryButton, wxT("Title"), _("Title"), wxT("Button"));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryButton, wxT("Representation"), _("Representation"), ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryButton, wxT("Picture"), _("Picture"));
	// A button has NO event — it carries ONLY a bound COMMAND (command-source picker, the command-door twin of a
	// control's data source). A press RUNS the command through ibFrontendCommandReceiver::ExecuteValueByPath. Internal
	// name MUST differ from any category name (wxPropertyGrid keys both in one map); lives in the Button category.
	ibPropertyCommandSource* m_propertyCommand = ibPropertyObject::CreateProperty<ibPropertyCommandSource>(m_categoryButton, wxT("Command"), _("Command"));

public:
	// ibFrontendCommandReceiver gate — the button IS-A command door; the walk starts at its owner form. WalkCommand
	// (the backend receiver's validate) is inherited from the door — no per-control impl.
	virtual ibValueForm* GetCommandGateForm() const override;
	// the button's bound command-hop path (empty = none) — the click handler runs it.
	const ibCommandDescription& GetCommandDesc() const { return m_propertyCommand->GetValueAsCommandDesc(); }
	// Bind a command to this button (a command drop / the picker): desc = the hop path, display = its caption.
	void SetCommandDesc(const ibCommandDescription& desc, const wxString& display) { m_propertyCommand->SetValue(desc, display); }
};

#include <wx/stattext.h>

// STATIC TEXT — a caption, and OPTIONALLY a value read from a source.
//
// Bound to a source it stops being decoration and becomes the cheapest possible view of a value:
// it shows the value's presentation, draws itself as a LINK, and a click opens that value's own
// form. Any value in the base reachable by a dotted path can be put behind it, so "show me the
// organisation of this exchange, and let me open it" costs one control and no code.
//
// The source is OPTIONAL, which is the whole difference from a text box: an unbound static text is
// still a caption and still renders. That is also why it never auto-provisions an attribute the
// way the bindable controls do — a caption that silently invented a form attribute would be a
// surprise, not a convenience.
class ibValueStaticText : public ibValueWindow,
	public ibTypeControlFactory {
	public:

	void SetCaption(const wxString& caption) { m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); }
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); }
	////////////////////////////////////////////////////////////////////////////////////////

	// Available sources = the owning form's attributes of THIS control's kind.
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

	//Get source object
	virtual ibSourceObject* GetSourceObject() const;

	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }

	// NEVER "missing" — an unbound static text is a caption, and captions render. Answering true
	// here would hide every plain label on every form (window.cpp gates Show on it).
	virtual bool IsSourceMissing() const override { return false; }

	//Get source attribute
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const {
		return m_propertySource->GetSourceAttributeObject();
	}

	//get form owner
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get type description
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	// Read-only by nature: this control shows a value, it does not edit one. SetControlValue is
	// therefore absent rather than a no-op — a writer that silently does nothing is worse than one
	// that is not there.
	virtual bool HasValueInControl() const { return !m_propertySource->IsEmptyProperty(); }
	virtual bool GetControlValue(ibValue& pvarControlVal) const;

	// THE CAPTION — the same rule the text box and the checkbox follow: the Title property when it
	// is filled, otherwise the bound field's synonym. So a bound static text is captioned by the
	// metadata and nobody has to type "Counterparty" next to a field already called that.
	virtual wxString GetControlTitle() const;

	ibValueStaticText();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated)override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

protected:

	// The click — opens the bound value's own form. Nothing happens when there is no source: a
	// caption is not a link, and the widget is not in link mode to begin with.
	void OnHyperlinkClicked(wxCommandEvent& event);

	ibPropertyCategory* m_categoryStaticText = ibPropertyObject::CreatePropertyCategory(wxT("StaticText"), _("Static text"));
	ibPropertyBoolean* m_propertyMarkup = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStaticText, wxT("Markup"), _("Markup"), true);
	ibPropertyUInteger* m_propertyWrap = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryStaticText, wxT("Wrap"), _("Wrap"), 0);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryStaticText, wxT("Title"), _("Title"), wxT("Static text"));
	// Where the caption sits relative to the value — the checkbox's property, same enum and same
	// default, because a form mixing the two must not have to explain why they differ.
	ibPropertyEnum<ibValueEnumTitleLocation>* m_propertyTitleLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTitleLocation>>(m_categoryStaticText, wxT("TitleLocation"), _("Title location"), ibTitleLocation::eLeft);

	// ANY type — a static text shows whatever it is pointed at, so nothing is filtered out here.
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(wxT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, wxT("Source"), _("Source"), ibValueTypes::TYPE_EMPTY);

	// The SAME event the text box raises before it opens a value, with the same standard-processing
	// switch: a configuration that wants to open something else — or nothing — says so here, and
	// does not have to learn a second name because this control is a label rather than an editor.
	ibPropertyCategory* m_propertyEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, wxT("Opening"), _("Opening"), wxArrayString{ wxT("Control"), wxT("StandartProcessing") });
};

#include <wx/textctrl.h>

constexpr ibClassID g_controlTextCtrlCLSID = control_to_clsid("CT_TXTC");

class ibValueTextCtrl : public ibValueWindow,
	public ibTypeControlFactory {
	public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); }
	// Full binding path [headAttrId, field, ...] — the resolve walks the attribute (gate)
	// then the metadata, so auto-built controls must carry the attribute head.
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); }
	ibMetaID GetSource(const ibMetaID& id) { return m_propertySource->GetValueAsSource(); }
	////////////////////////////////////////////////////////////////////////////////////////

	// Available sources = the owning form's attributes of THIS control's kind.
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

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

	// Own bound source path ([headAttr, field]) — MUTABLE ref (like GetTypeDesc): read it, or assign to
	// bind (GetSourceDesc() = desc). No separate setter.
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }
	// Unbound (no source picked) -> not rendered (ibValueWindow::UpdateWindow gate). Ask the PROPERTY
	// directly (IsEmptyProperty), NOT GetSourceDesc — the latter resolves / walks the source and can be
	// broken (a dangling binding); the flag on the stored property is cheap and always safe.
	virtual bool IsSourceMissing() const override { return m_propertySource->IsEmptyProperty(); }

	//Get source attribute
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const {
		return m_propertySource->GetSourceAttributeObject();
	}

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	//methods & attributes
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//get title
	virtual wxString GetControlTitle() const;

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

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

#ifdef OES_USE_WEB
	// Web-side commit handler. Bound to wxEVT_TEXT on the ibWebTextCtrl
	// in Create(). Reads the new string from event.GetString(),
	// coerces it to the backing ibValue type (mirrors the desktop
	// TextProcessing path), writes through SetControlValue and fires
	// the OnChange script via CallAsEvent — same single source of
	// truth as OnTextEnter / OnKillFocus on desktop.
	void OnWebTextChanged(wxCommandEvent& event);
#endif


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
	public:

	ibValueComboBox();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;
};

#include <wx/choice.h>

class ibValueChoice : public ibValueWindow {
	public:

	ibValueChoice();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;
};

#include <wx/listbox.h>

class ibValueListBox : public ibValueWindow {
	public:

public:

	ibValueListBox();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;
};

#include <wx/checkbox.h>

constexpr ibClassID g_controlCheckboxCLSID = control_to_clsid("CT_CHKB");

class ibValueCheckbox : public ibValueWindow,
	public ibTypeControlFactory {
	public:

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); }
	// Full binding path [headAttrId, field, ...] — see ibValueModelText::SetSource.
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); }
	ibMetaID GetSource() const { return m_propertySource->GetValueAsSource(); }
	////////////////////////////////////////////////////////////////////////////////////////

	// Available sources = the owning form's attributes of THIS control's kind.
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueCheckbox();

	//get source object
	virtual ibSourceObject* GetSourceObject() const;

	// Own bound source path ([headAttr, field]) — MUTABLE ref (like GetTypeDesc): read it, or assign to
	// bind (GetSourceDesc() = desc). No separate setter.
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }
	// Unbound (no source picked) -> not rendered. Ask the PROPERTY (IsEmptyProperty), NOT GetSourceDesc —
	// the latter walks the source and can be broken; the property flag is cheap and always safe.
	virtual bool IsSourceMissing() const override { return m_propertySource->IsEmptyProperty(); }

	//get source attribute
	virtual const ibBackendSourceColumn* GetSourceAttributeObject() const {
		return m_propertySource->GetSourceAttributeObject();
	}

	//get form owner 
	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }

	//get metaData
	virtual const ibMetaData* GetMetaData() const;

	//get type description 
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	virtual ibSelectorDataType GetFilterDataType() const {
		return ibSelectorDataType::ibSelectorDataType_boolean;
	}

	//methods & attributes
	void FillControlMembers(ibMemberTable& helper) const;   // bound in ctor (was PrepareNames)

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);        //setting attribute
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);                   //attribute value

	//get title
	virtual wxString GetControlTitle() const;

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
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
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

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
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categorySource, wxT("Source"), _("Source"), ibValueTypes::TYPE_BOOLEAN);

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(wxT("Event"), _("Event"));
	ibEventControl* m_onCheckboxClicked = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, wxT("OnCheckboxClicked"), _("Checkbox clicked"), wxArrayString{ wxT("Control") });

	friend class ibValueForm;
};

#include <wx/radiobut.h>

class ibValueRadioButton : public ibValueWindow {
	public:

	void SetCaption(const wxString& caption) { return m_propertyTitle->SetValue(caption); }
	wxString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueRadioButton();

	//get title
	virtual wxString GetControlTitle() const { return GetCaption(); }

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categoryRadioButton = ibPropertyObject::CreatePropertyCategory(wxT("RadioButton"), _("Radio button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryRadioButton, wxT("Title"), _("Title"), wxT("Radio button"));
	ibPropertyBoolean* m_propertySelected = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryRadioButton, wxT("Selected"), _("Selected"));
};

#include <wx/statline.h>

class ibValueStaticLine : public ibValueWindow {
	public:

	ibValueStaticLine();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categoryStaticLine = ibPropertyObject::CreatePropertyCategory(wxT("StaticLine"), _("Static line"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryStaticLine, wxT("Orient"), _("Orient"), wxHORIZONTAL);
};

#include <wx/slider.h>

class ibValueSlider : public ibValueWindow {
	public:

	ibValueSlider();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categorySlider = ibPropertyObject::CreatePropertyCategory(wxT("Slider"), _("Slider"));
	ibPropertyInteger* m_propertyMinValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("MinValue"), _("Min value"), 0);
	ibPropertyInteger* m_propertyMaxValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("MaxValue"), _("Max value"), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, wxT("Value"), _("Value"), 50);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySlider, wxT("Orient"), _("Orient"), wxHORIZONTAL);
};

#include <wx/gauge.h>

class ibValueGauge : public ibValueWindow {
	public:

	ibValueGauge();

	//control factory
	virtual wxObject* Create(ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void OnCreated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost, bool firstCreated) override;
	virtual void Update(wxObject* wxobject, ibVisualHost* visualHost) override;
	virtual void OnUpdated(wxObject* wxobject, ibFrontendWindow* wxparent, ibVisualHost* visualHost) override;
	virtual void Cleanup(wxObject* obj, ibVisualHost* visualHost) override;

	//support icons
	virtual wxIcon GetIcon() const;
	static wxIcon GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categoryGauge = ibPropertyObject::CreatePropertyCategory(wxT("Gauge"), _("Gauge"));
	ibPropertyInteger* m_propertyRange = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, wxT("Range"), _("Range"), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, wxT("Value"), _("Value"), 30);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryGauge, wxT("Orient"), _("Orient"), wxHORIZONTAL);
};

#endif