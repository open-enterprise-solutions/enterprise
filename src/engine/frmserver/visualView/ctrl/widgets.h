#ifndef _COMMON_H_
#define _COMMON_H_

#include "window.h"
#include "typeControl.h"
#include "core/description/sourceDescription.h"   // ibSourceDescription (full [head, field] binding path)
#include "backend/propertyManager/property/propertyCommandSource.h"   // ibPropertyCommandSource — assign a COMMAND to the button
#include "frmserver/visualView/layers/commandReceiver.h"             // ibFrontendCommandReceiver — the command door the button IS-A (IS-A ibBackendCommandReceiver too)

/////////////////////////////////////////////////////////////////////////////////////
//                                 COMMON ELEMENTS                                 //
/////////////////////////////////////////////////////////////////////////////////////

// ibValueButton's control clsid — kind-typed identity (mirror g_controlTableBoxCLSID), so a control's TYPE is
// read from its clsid, never via dynamic_cast.
constexpr ibClassID g_controlButtonCLSID = control_to_clsid("CT_BUTN");

class ibValueButton : public ibValueWindow, public ibFrontendCommandReceiver {
	public:

	void SetCaption(const ibString& caption) { return m_propertyTitle->SetValue(caption); }
	ibString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueButton();

	//get title
	virtual ibString GetControlTitle() const { return GetCaption(); }

	// The caption, the picture and the representation as they come out once the bound command has filled
	// in what the button leaves unset; hidden when that command no longer resolves, disabled when it
	// modifies data on a view-only form.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// Press — the bound command runs; with Member, one command of its group.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	// The press — runs the bound command.
	void OnButtonPressed();
	// …and when the bound command is a GROUP, the press on one of its leaves: `member` is the leaf's place
	// in the order the frame listed them (WalkGroup). A group runs nothing by itself.
	void OnButtonPressed(size_t member);

	// ⭐ THE GROUP'S SUBMENU, walked ONCE for both ends: the frame (`node` given — the tree with captions and
	// pictures by their ids, a nested group as a sub-node, a leaf carrying its `Member` place) and the press (`node`
	// null — only the leaves, in the same order). Two walks would be two orders, and a press would run a neighbour.
	void WalkGroup(const std::vector<ibFrontendCommandReceiver::ibCommandSubItem>& subs,
		std::vector<ibCommandDescription>& leaves, ibDataNode* node) const;

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(ibT("Button"), _("Button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryButton, ibT("Title"), _("Title"),
		_("The text on the button. Can be written per language and changed from code while the form is open."), ibT("Button"));
	ibPropertyEnum<ibValueEnumRepresentation>* m_propertyRepresentation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumRepresentation>>(m_categoryButton, ibT("Representation"), _("Representation"),
		_("What the button shows: text, picture, or both. Auto: what the bound command prefers (Close is a picture alone, Add a picture with text); picture and text when no command is bound."),
		ibRepresentation::ibRepresentation_Auto);
	ibPropertyPicture* m_propertyPicture = ibPropertyObject::CreateProperty<ibPropertyPicture>(m_categoryButton, ibT("Picture"), _("Picture"),
		_("The button's icon, beside or instead of the text (see Representation)."));
	// A button has NO event — it carries ONLY a bound COMMAND (command-source picker, the command-door twin of a
	// control's data source). A press RUNS the command through ibFrontendCommandReceiver::ExecuteValueByPath. Internal
	// name MUST differ from any category name (wxPropertyGrid keys both in one map); lives in the Button category.
	ibPropertyCommandSource* m_propertyCommand = ibPropertyObject::CreateProperty<ibPropertyCommandSource>(m_categoryButton, ibT("Command"), _("Command"),
		_("The command a press runs: a form command, a standard command of the form or of one of its tables, or a command of the configuration. A button has no click event of its own - the code goes into the command's handler."));

public:
	// ibFrontendCommandReceiver gate — the button IS-A command door; the walk starts at its owner form. WalkCommand
	// (the backend receiver's validate) is inherited from the door — no per-control impl.
	virtual ibValueForm* GetCommandGateForm() const override;
	// the button's bound command-hop path (empty = none) — the click handler runs it.
	const ibCommandDescription& GetCommandDesc() const { return m_propertyCommand->GetValueAsCommandDesc(); }
	// Bind a command to this button (a command drop / the picker): desc = the hop path, display = its caption.
	void SetCommandDesc(const ibCommandDescription& desc, const ibString& display) { m_propertyCommand->SetValue(desc, display); }
};

// STATIC TEXT — a caption, and OPTIONALLY a value read from a source.
//
// Bound to a source it stops being decoration and becomes the cheapest possible view of a value:
// it shows the value's presentation, is shown as a LINK, and a click opens that value's own
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

	void SetCaption(const ibString& caption) { m_propertyTitle->SetValue(caption); }
	ibString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	////////////////////////////////////////////////////////////////////////////////////////
	void SetSource(const ibMetaID& id) { m_propertySource->SetValue(id); }
	void SetSource(const std::vector<ibSourceId>& path) { m_propertySource->SetValue(ibSourceDescription(path)); }
	////////////////////////////////////////////////////////////////////////////////////////

	// Available sources = the owning form's attributes of THIS control's kind.
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;

	//Get source object
	virtual ibSourceObject* GetSourceObject() const;

	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }

	// NEVER "unbound" — a static text with no binding is a caption, and captions render. Answering true
	// here would hide every plain label on every form (window.cpp gates Visible on it).
	virtual bool IsUnbound() const override { return false; }
	// A bound field this base does not use takes the text with it.
	virtual bool IsAvailable() const override { return m_propertySource->IsAvailable() && ibValueWindow::IsAvailable(); }

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
	virtual ibString GetControlTitle() const;

	ibValueStaticText();

	// The caption as resolved, the value's presentation beside it and whether that value is a link.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// Open — the link clicked.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:

	// The click — opens the bound value's own form. Nothing opens when there is no source or the
	// value is empty: a caption is not a link.
	void OnHyperlinkClicked();

protected:

	ibPropertyCategory* m_categoryStaticText = ibPropertyObject::CreatePropertyCategory(ibT("StaticText"), _("Static text"));
	ibPropertyBoolean* m_propertyMarkup = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryStaticText, ibT("Markup"), _("Markup"),
		_("Kept for older forms and saved with the form; it has no effect - the caption is shown as plain text."), true);
	ibPropertyUInteger* m_propertyWrap = ibPropertyObject::CreateProperty<ibPropertyUInteger>(m_categoryStaticText, ibT("Wrap"), _("Wrap"),
		_("Kept for older forms and saved with the form; it has no effect. For several lines, put line breaks into the title."), 0);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryStaticText, ibT("Title"), _("Title"),
		_("The caption text. Empty on a bound static text: the bound field's synonym. Can be written per language and changed from code while the form is open."),
		ibT("Static text"));
	// Where the caption sits relative to the value — the checkbox's property, same enum and same
	// default, because a form mixing the two must not have to explain why they differ.
	ibPropertyEnum<ibValueEnumTitleLocation>* m_propertyTitleLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTitleLocation>>(m_categoryStaticText, ibT("TitleLocation"), _("Title location"),
		_("Which side of the value the caption sits on when the static text is bound: left (the default) or right."),
		ibTitleLocation::eLeft);

	// ANY type — a static text shows whatever it is pointed at, so nothing is filtered out here.
	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(ibT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, ibT("Source"), _("Source"),
		_("Optional: a value to show beside the caption - a form attribute or any field reachable from it by a dotted path. A non-empty value is drawn as a link that opens its own form. Unbound, the static text is a plain caption."),
		ibValueTypes::TYPE_EMPTY);

	// The SAME event the text box raises before it opens a value, with the same standard-processing
	// switch: a configuration that wants to open something else — or nothing — says so here, and
	// does not have to learn a second name because this control is a label rather than an editor.
	ibPropertyCategory* m_propertyEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, ibT("Opening"), _("Opening"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
};

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

	void SetCaption(const ibString& caption) { return m_propertyTitle->SetValue(caption); }
	ibString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

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
	// Unbound (no source picked) -> not shown (ibValueWindow::Update writes Visible off). Ask the PROPERTY
	// directly (IsEmptyProperty), NOT GetSourceDesc — the latter resolves / walks the source and can be
	// broken (a dangling binding); the flag on the stored property is cheap and always safe.
	virtual bool IsUnbound() const override { return m_propertySource->IsEmptyProperty(); }
	// Bound to a field this base does not use -> not available.
	virtual bool IsAvailable() const override { return m_propertySource->IsAvailable() && ibValueWindow::IsAvailable(); }

	// Read-only at run time: text editing switched off, or a binding that cannot be written (a dotted
	// reference, a view-only form, a read-only source). One flag for the whole field: it stops typing,
	// Select and Clear alike; Open only reads, and stays.
	virtual bool IsReadOnly() const override;

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
	virtual ibString GetControlTitle() const;

	// Just made by the designer, with no source — bound to a fresh attribute (the desktop's).
	virtual void OnCreated(ibVisualHost* host, bool firstCreated) override;
	// The caption as resolved, the value as text through the field's format, and whether it is read-only.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	// Input — the person started changing the text (nothing committed; the object is modified from the first
	// keystroke); Change — the text as it stands is committed (Text: the client sends it on Enter, or on
	// leaving a field that was typed into); Select / Open / Clear — the field's three buttons.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;
	// Input while the form's object is not modified yet: the first keystroke modifies it, and the «*» of its tab comes
	// with that frame. Any later one changes nothing until the next call.
	virtual bool IsSubscribed(ibProtocol::ibEvent::ibKind event) const override;

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual void OnPropertyRefresh() override;

	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue);

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

public:

	virtual bool HasValueInControl() const {
		return m_propertySource->IsEmptyProperty();
	}

	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

	// A control on a form stands on the form's source: that is what holds this field and its neighbours.
	virtual ibChoiceHolder GetChoiceHolder() const override;

public:

	virtual void ChoiceProcessing(ibValue& vSelected);

private:

	// What the user did in the field. Typed text arrives as it stands and is parsed by the value's type;
	// a read-only field takes none of it.
	void OnTextEnter(const ibString& text);
	void OnTextUpdated();

	void OnSelectButtonPressed();
	void OnOpenButtonPressed();
	void OnClearButtonPressed();

protected:

	bool TextProcessing(const ibString& strData);
	// …and a value the client sent with its type (ibValue::Serialize — a date from its calendar): the same commit.
	bool ValueProcessing(const class ibDataNode& node);

	// The format written on the attribute the field is bound to — what it shows its value with when it has
	// none of its own. Empty when the source is no attribute.
	const ibTranslateString& GetSourceFormat() const;

private:

	bool GetChoiceForm(ibPropertyList* property);

	ibValue m_selValue;

	ibPropertyCategory* m_categoryText = ibPropertyObject::CreatePropertyCategory(ibT("Textbox"), _("Textbox"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryText, ibT("Title"), _("Title"),
		_("The field's caption, shown beside it. Empty: the bound field's synonym. Can be written per language."), ibT(""));
	ibPropertyBoolean* m_propertyPasswordMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, ibT("PasswordMode"), _("Password mode"), _("Whether the field hides what is typed behind placeholder characters, for secrets such as passwords. Off by default."), false);
	ibPropertyBoolean* m_propertyMultilineMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, ibT("MultilineMode"), _("Multiline mode"), _("Whether the field accepts several lines of text (Enter starts a new line) - for comments and addresses. Off by default: Enter finishes the input."), false);
	ibPropertyBoolean* m_propertyTexteditMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryText, ibT("TexteditMode"), _("Textedit mode"), _("Whether the value can be typed into the field. Off: it can only be picked with the Select button or cleared. On by default; a field bound through a reference path is read-only whatever this says."), true);
	ibPropertyFormat* m_propertyFormat = ibPropertyObject::CreateProperty<ibPropertyFormat>(m_categoryText, ibT("Format"), _("Format"),
		_("How the field shows its value, written per language: digits after the point, separators, a date pattern. Empty: the bound attribute's format, and without one a number shows as many digits after the point as its type keeps."), ibT(""));

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(ibT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, ibT("Source"), _("Source"),
		_("The value the field shows and edits: a form attribute, a field of the form's object, or a field reachable from them by a dotted path (read-only). The value's type decides how input is parsed and which choice form Select opens."),
		ibValueTypes::TYPE_STRING);
	ibPropertyList* m_propertyChoiceForm = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryData, ibT("ChoiceForm"), _("Choice form"),
		_("Which form opens when the user presses Select: one of the forms of the value's type. Empty: the type's default choice form."),
		&ibValueTextCtrl::GetChoiceForm, wxNOT_FOUND);

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(ibT("Button"), _("Button"));
	ibPropertyBoolean* m_propertySelectButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, ibT("ButtonSelect"), _("Select button"),
		_("Whether the field shows the Select button (...), which opens a choice form or list to pick the value from. On by default."), true);
	ibPropertyBoolean* m_propertyClearButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, ibT("ButtonClear"), _("Clear button"),
		_("Whether the field shows the Clear button (X), which empties the value. On by default."), true);
	ibPropertyBoolean* m_propertyOpenButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, ibT("ButtonOpen"), _("Open button"),
		_("Whether the field shows the Open button, which opens the referenced object's form. Off by default."), false);

	ibPropertyCategory* m_propertyEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, ibT("OnChange"), _("Change"), std::vector<ibString>{ ibT("Control") });
	ibEventControl* m_eventStartChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, ibT("StartChoice"), _("Start choice"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventStartListChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, ibT("StartListChoice"), _("Start list choice"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventClearing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, ibT("Clearing"), _("Clearing"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, ibT("Opening"), _("Opening"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventChoiceProcessing = ibPropertyObject::CreateEvent<ibEventControl>(m_propertyEvent, ibT("ChoiceProcessing"), _("Choice processing"), std::vector<ibString>{ ibT("Control"), ibT("ValueSelected"), ibT("StandardProcessing") });

	friend class ibValueForm;
};

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

	void SetCaption(const ibString& caption) { return m_propertyTitle->SetValue(caption); }
	ibString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueCheckbox();

	//get source object
	virtual ibSourceObject* GetSourceObject() const;

	// Own bound source path ([headAttr, field]) — MUTABLE ref (like GetTypeDesc): read it, or assign to
	// bind (GetSourceDesc() = desc). No separate setter.
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }
	// Unbound (no source picked) -> not shown. Ask the PROPERTY (IsEmptyProperty), NOT GetSourceDesc —
	// the latter walks the source and can be broken; the property flag is cheap and always safe.
	virtual bool IsUnbound() const override { return m_propertySource->IsEmptyProperty(); }
	// Bound to a field this base does not use -> not available.
	virtual bool IsAvailable() const override { return m_propertySource->IsAvailable() && ibValueWindow::IsAvailable(); }

	// Read-only at run time: a binding that cannot be written (a dotted reference, a view-only form, a
	// read-only source). The value is shown, a toggle is refused — NOT disabled, which is availability
	// (for action buttons, not data controls).
	virtual bool IsReadOnly() const override;

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
	virtual ibString GetControlTitle() const;

	// Just made by the designer, with no source — bound to a fresh attribute (the desktop's).
	virtual void OnCreated(ibVisualHost* host, bool firstCreated) override;
	// The caption as resolved, whether the box is checked and whether it is read-only.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	/**
	* Property events
	*/
	virtual void OnPropertyCreated(ibProperty* property);
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue);


	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

public:

	virtual bool HasValueInControl() const {
		return m_propertySource->IsEmptyProperty();
	}

	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

	// Toggle — the box clicked (Checked: the state the person toggled it to).
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

private:

	// The click, with the state the user toggled the box to.
	void OnClickedCheckbox(bool checked);

	ibValue m_selValue = false;

	ibPropertyCategory* m_categoryCheckBox = ibPropertyObject::CreatePropertyCategory(ibT("Checkbox"), _("Checkbox"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryCheckBox, ibT("Title"), _("Title"),
		_("The checkbox's caption. Empty: the bound field's synonym. Can be written per language."), ibT(""));
	ibPropertyEnum<ibValueEnumTitleLocation>* m_propertyTitleLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTitleLocation>>(m_categoryCheckBox, ibT("TitleLocation"), _("Title location"),
		_("Which side of the box the caption sits on: left (the default) or right."),
		ibTitleLocation::eLeft);

	ibPropertyCategory* m_categorySource = ibPropertyObject::CreatePropertyCategory(ibT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categorySource, ibT("Source"), _("Source"),
		_("The boolean value the checkbox shows and toggles: a form attribute or a field of the form's object. Bound through a reference path, or on a view-only form, it shows the value but cannot be toggled. Unbound, the checkbox is not shown at run time."),
		ibValueTypes::TYPE_BOOLEAN);

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	ibEventControl* m_onCheckboxClicked = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("OnCheckboxClicked"), _("Checkbox clicked"), std::vector<ibString>{ ibT("Control") });

	friend class ibValueForm;
};

class ibValueRadioButton : public ibValueWindow {
	public:

	void SetCaption(const ibString& caption) { return m_propertyTitle->SetValue(caption); }
	ibString GetCaption() const { return m_propertyTitle->GetValueAsTranslateString(); }

	ibValueRadioButton();

	//get title
	virtual ibString GetControlTitle() const { return GetCaption(); }

	// The caption in the session's language, and whether it is shown selected.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//load & save object in control
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categoryRadioButton = ibPropertyObject::CreatePropertyCategory(ibT("RadioButton"), _("Radio button"));
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryRadioButton, ibT("Title"), _("Title"),
		_("The radio button's caption. Can be written per language."), ibT("Radio button"));
	ibPropertyBoolean* m_propertySelected = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryRadioButton, ibT("Selected"), _("Selected"),
		_("Whether the radio button is shown selected. A radio button has no data source of its own."));
};

class ibValueStaticLine : public ibValueWindow {
	public:

	ibValueStaticLine();

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categoryStaticLine = ibPropertyObject::CreatePropertyCategory(ibT("StaticLine"), _("Static line"));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryStaticLine, ibT("Orient"), _("Orient"),
		_("The line's direction: horizontal (the default), separating stacked parts of a form, or vertical, separating side-by-side ones. Set Stretch to Expand for the line to run the full width or height."),
		wxHORIZONTAL);
};

class ibValueSlider : public ibValueWindow {
	public:

	ibValueSlider();

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categorySlider = ibPropertyObject::CreatePropertyCategory(ibT("Slider"), _("Slider"));
	ibPropertyInteger* m_propertyMinValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, ibT("MinValue"), _("Min value"),
		_("The slider's smallest value, at its start. Default 0."), 0);
	ibPropertyInteger* m_propertyMaxValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, ibT("MaxValue"), _("Max value"),
		_("The slider's largest value, at its end. Default 100."), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, ibT("Value"), _("Value"),
		_("Where the slider's thumb starts, between the min and max values. Default 50."), 50);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySlider, ibT("Orient"), _("Orient"),
		_("The slider's direction: horizontal (the default) or vertical."), wxHORIZONTAL);
};

class ibValueGauge : public ibValueWindow {
	public:

	ibValueGauge();

	//support icons
	virtual ibPicture GetIcon() const;
	static ibPicture GetIconGroup();

	//load & save object in control 
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

private:
	ibPropertyCategory* m_categoryGauge = ibPropertyObject::CreatePropertyCategory(ibT("Gauge"), _("Gauge"));
	ibPropertyInteger* m_propertyRange = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, ibT("Range"), _("Range"),
		_("The value at which the progress bar is full. Default 100, so the value reads as a percentage."), 100);
	ibPropertyInteger* m_propertyValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, ibT("Value"), _("Value"),
		_("How far the progress bar is filled, from 0 to the range. Default 30."), 30);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryGauge, ibT("Orient"), _("Orient"),
		_("The progress bar's direction: horizontal (the default, filling left to right) or vertical."), wxHORIZONTAL);
};

#endif