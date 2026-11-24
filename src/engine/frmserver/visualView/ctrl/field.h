#ifndef __FIELD_H__
#define __FIELD_H__

#include "window.h"
#include "typeControl.h"
#include "core/description/sourceDescription.h"   // ibSourceDescription — the binding path
#include "backend/propertyManager/property/propertyChoices.h"   // ibPropertyChoices — a switch's items

#include <memory>

// ⭐ THE FIELD — ONE CONTROL, ITS VIEW A CHILD (docs/private/form-field.md). What every view shares is the field's: the
// source and its type, the title, the value read and written (Max, 2026-11-23: "the source, the title, the basic
// behaviour are the parent's; the children reuse them"). What a view is for — typed into, on or off, shown, one of its
// items picked — is a CHILD the view makes: its own properties, its own events, its part of the frame, its answer to the
// client. The child rents the field's surface — attached, its properties and events show as the field's
// (ibPropertyObject::GetProperty / GetEvent) — and OUTSIDE THERE IS ONE CONTROL: nothing hands the child out, nothing
// outside casts to it.
//
// The protocol names the meaning, not a widget: a node `Field` with its `View`, and each client draws the view with a
// widget of its own (ibProtocol::ibForm::ibProperty::ibFieldView).

constexpr ibClassID g_controlFieldCLSID = control_to_clsid("CT_FILD");

class ibValueField;

// ⭐ WHAT A VIEW DOES — the kind's behaviour as a provider (the template's ibTemplateProvider is the pattern), but one
// per field and holding state: a view's properties and events are the field's, saved with it. Made for a view by
// Create — the one place that knows the views; a new view is a class here and a line there.
class ibFieldViewProvider : public ibPropertyObject {
public:

	virtual ~ibFieldViewProvider() = default;

	// …answering as its field: outside, the field is the one object.
	virtual ibString GetClassName() const override;
	virtual ibString GetObjectTypeName() const override;
	virtual bool IsEditable() const override;
	virtual const ibMetaData* GetMetaData() const override;

	// The view's part of the field's State, and what it does with the client's events on the field.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) {}
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) { return false; }
	virtual bool IsSubscribed(ibProtocol::ibEvent::ibKind event) const { return false; }
	// Read-only by the view's own word (an Input with its text editing off; a Label always), over the binding's.
	virtual bool IsReadOnly() const { return false; }
	// Whether the view edits the source: a field of one that does, with no source, is not shown, and a designer's drop
	// binds it to a fresh attribute. A Label does not — unbound, it is a decoration.
	virtual bool IsBindingView() const { return true; }
	
	// THE CHOICE — an Input's; any other view answers with the field's own, its type's and its attribute's.
	virtual bool HasQuickChoice() const;
	virtual ibSelectMode GetSelectMode() const;
	virtual const ibChoiceTypeLinkDescription& GetTypeLink() const;
	virtual const ibChoiceParametersDescription& GetChoiceParameters() const;
	virtual ibChoiceCondition GetChoiceCondition() const;
	virtual void ChoiceProcessing(ibValue& selected) {}

	// Its properties and events, read from and written into the field's own node.
	virtual bool ReadData(const ibDataNode& node) = 0;
	virtual bool WriteData(ibDataNode& node) const = 0;

	ibValueField* GetField() const { return m_field; }

	// …and from the designer's copy blob, once the field is of the pasted view (ibValueField::PasteData).
	bool PasteData(const ibDataNode& node) { return PasteProperty(node); }

	// The child of a view; null for a view this build does not know.
	static std::unique_ptr<ibFieldViewProvider> Create(ibFieldView view, ibValueField* field);

protected:

	explicit ibFieldViewProvider(ibValueField* field) : m_field(field) {}

	// The field's source changed — a view that holds something of its type checks it.
	virtual void OnViewChanged() = 0;

private:

	ibValueField* const m_field;

	friend class ibValueField;
};

class ibValueField : public ibValueWindow,
	public ibTypeControlFactory {
public:

	ibValueField();

	// Available sources = the owning form's attributes.
	virtual bool GetSourceList(std::vector<ibBackendFormAttributeValue*>& out) const override;
	virtual ibSourceObject* GetSourceObject() const;
	virtual ibSourceDescription& GetSourceDesc() const override { return m_propertySource->GetValueAsSourceDesc(); }
	// Unbound — not shown, where its view edits the source; an unbound Label is a decoration and is shown.
	virtual bool IsUnbound() const override;
	virtual bool IsAvailable() const override { return m_propertySource->IsAvailable() && ibValueWindow::IsAvailable(); }
	// A binding that cannot be written (a dotted reference, a view-only form, a read-only source), or the view's word.
	virtual bool IsReadOnly() const override;

	virtual ibValueForm* GetOwnerForm() const { return m_formOwner; }
	virtual const ibMetaData* GetMetaData() const;
	virtual ibTypeDescription& GetTypeDesc() const { return m_propertySource->GetValueAsTypeDesc(); }

	virtual bool SetPropVal(const long lPropNum, const ibValue& varPropVal);
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal);

	// The Title when it is filled, otherwise the bound field's synonym.
	virtual ibString GetControlTitle() const;

	// Just made by the designer: dropped bound — of the default view; with no source — bound to a fresh attribute where
	// its view binds one.
	virtual void OnCreated(ibVisualHost* host, bool firstCreated) override;
	// A view the source's type does not allow gives way to Input first; then the caption as resolved and whether it is
	// read-only — and the view's part, written by the view.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;
	// The view's.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;
	virtual bool IsSubscribed(ibProtocol::ibEvent::ibKind event) const override;

	virtual ibPicture GetIcon() const;

	// The view first — it makes the child whose properties the rest of the node holds.
	virtual bool ReadData(const ibDataNode& node);
	virtual bool WriteData(ibDataNode& node) const;

	// A view its source's type does not allow is refused.
	virtual bool OnPropertyChanging(ibProperty* property, const ibVariant& newValue) override;
	// View changed — the child of the new view takes its place; a source changed is told to the view
	// (ibFieldViewProvider::OnViewChanged).
	virtual void OnPropertyChanged(ibProperty* property, const ibVariant& oldValue, const ibVariant& newValue) override;

	// The value — as the text field answers: no source, its own.
	virtual bool HasValueInControl() const { return m_propertySource->IsEmptyProperty(); }
	virtual bool GetControlValue(ibValue& pvarControlVal) const;
	virtual bool SetControlValue(const ibValue& varControlVal = ibValue());

	// The source the bound field stands in — the source's question, so the field's.
	virtual ibChoiceHolder GetChoiceHolder() const override;
	// …the rest of the choice is the view's.
	virtual bool HasQuickChoice() const override;
	virtual ibSelectMode GetSelectMode() const override;
	virtual const ibChoiceTypeLinkDescription& GetTypeLink() const override;
	virtual const ibChoiceParametersDescription& GetChoiceParameters() const override;
	virtual ibChoiceCondition GetChoiceCondition() const override;
	virtual void ChoiceProcessing(ibValue& vSelected);

	// Bound by the form's builder — and of the view its type gives.
	void SetSource(const std::vector<ibSourceId>& path);

	static ibPicture GetIconGroup();

protected:

	// The designer's paste makes the field of the pasted view first.
	virtual bool PasteData(const ibDataNode& node) override;

private:

	ibFieldView GetView() const { return m_propertyView->GetValueAsEnum(); }
	// The child of `view` takes the place of the one there: what the two have by the same name crosses over.
	void SetView(ibFieldView view);
	// The view the source's type gives — one Boolean a Flag, one schedule a Label, any other an Input — set through the
	// door.
	void SetDefaultView();
	// Whether the source's type allows `view` (Max, 2026-11-23).
	bool IsViewAllowed(ibFieldView view) const;

	// methods & attributes: Value (the view's properties are the field's, ibValueFrame::FillFrameProperties)
	void FillControlMembers(ibMemberTable& helper) const;

	std::unique_ptr<ibFieldViewProvider> m_provider;
	ibValue m_selValue;   // the value of a field with no source

	ibPropertyCategory* m_categoryField = ibPropertyObject::CreatePropertyCategory(ibT("Field"), _("Field"));
	// The views offered are those the source's type allows (IsViewAllowed, the property's filter).
	ibPropertyEnum<ibValueEnumFieldView, &ibValueField::IsViewAllowed>* m_propertyView = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumFieldView, &ibValueField::IsViewAllowed>>(m_categoryField, ibT("View"), ibGETTEXT_IN_CONTEXT("field", "View"),
		_("What the field is for: Input - typed into; Flag - on or off; Label - shown, not edited; Switch - one of the values set in its Choices picked; Gauge - how far a number has come of its range; Slider - a number moved between two ends. Allowed by the source's type: Input and Label for any, Flag for a Boolean, Gauge and Slider for a Number, Switch for one type of a number, a string, a date, a Boolean or an enumeration; a source of another type turns the field into an Input. Each view has its own properties and events; the source, the title and the value are the field's in every view."),
		ibFieldView::ibFieldView_Input);
	ibPropertyTString* m_propertyTitle = ibPropertyObject::CreateProperty<ibPropertyTString>(m_categoryField, ibT("Title"), _("Title"),
		_("The field's caption. Empty: the bound field's synonym. Can be written per language and changed from code while the form is open."), ibT(""));
	ibPropertyEnum<ibValueEnumTitleLocation>* m_propertyTitleLocation = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumTitleLocation>>(m_categoryField, ibT("TitleLocation"), _("Title location"),
		_("Which side of the field the caption sits on: left (the default) - in the column the form lines its fields' titles up in - or right, beside it, as a flag's; none - no caption shown."),
		ibTitleLocation::eLeft);

	ibPropertyCategory* m_categoryData = ibPropertyObject::CreatePropertyCategory(ibT("Data"), _("Data"));
	ibPropertySource* m_propertySource = ibPropertyObject::CreateProperty<ibPropertySource>(m_categoryData, ibT("Source"), _("Source"),
		_("The value the field shows and edits: a form attribute, a field of the form's object, or a field reachable from them by a dotted path (read-only). Its type is the source's, whatever the view: a flag shows whether the value is true and writes a Boolean."),
		ibValueTypes::TYPE_STRING);
	// The person's own "only shown" — the field's, in every view (Max, 2026-11-24: "a field must have ReadOnly").
	ibPropertyBoolean* m_propertyReadOnly = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryData, ibT("ReadOnly"), _("Read only"),
		_("Whether the value is only shown: seen, selected and copied, not changed - in every view. Off by default; a field bound through a reference path, or on a form opened for viewing, is read-only whatever this says. Can be changed from code while the form is open."), false);

	friend class ibValueForm;
};

// ⭐ INPUT — typed into: the text field, its properties, its events and its handling of the client, standing in the
// field's slot.
class ibFieldViewInput : public ibFieldViewProvider {
public:

	explicit ibFieldViewInput(ibValueField* field) : ibFieldViewProvider(field) {}

	// The value as text through the field's format and its mask, the mask in effect, and the value itself.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;
	// Input — typing started; Change — the text, or the value with its type, committed; Select / Open / Clear — the
	// field's three buttons.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;
	// Input while the form's object is not modified yet.
	virtual bool IsSubscribed(ibProtocol::ibEvent::ibKind event) const override;

	// The field's own word over its type's and its attribute's (issue #259), and its own links (#616).
	virtual bool HasQuickChoice() const override;
	virtual ibSelectMode GetSelectMode() const override;
	virtual const ibChoiceTypeLinkDescription& GetTypeLink() const override { return m_propertyTypeLink->GetValueAsLinkDesc(); }
	virtual const ibChoiceParametersDescription& GetChoiceParameters() const override { return m_propertyChoiceParameters->GetValueAsParametersDesc(); }
	virtual ibChoiceCondition GetChoiceCondition() const override;
	virtual void ChoiceProcessing(ibValue& selected) override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

	// What is offered and hidden is asked of the field's type.
	virtual void OnPropertyRefresh() override;

protected:

	// The choice form kept where it is one of the type's forms, cleared where it is not.
	virtual void OnViewChanged() override;

private:

	// What the person did in the field. Typed text arrives as it stands and is parsed by the value's type; a value with
	// its type (ibValue::Serialize — a date from a calendar) is the same commit. A read-only field takes none of it.
	bool TextProcessing(const ibString& typed);
	bool ValueProcessing(const ibDataNode& node);
	void OnTextUpdated();
	void OnSelectButtonPressed();
	void OnOpenButtonPressed();
	void OnClearButtonPressed();

	// The format and the mask written on the attribute the field is bound to, and THE MASK IN EFFECT — the field's own,
	// else the bound attribute's, else the one its type gives (docs/private/input-mask.md); none in multi-line mode.
	const ibTranslateString& GetSourceFormat() const;
	ibString GetSourceMask() const;
	class ibInputMask GetMask() const;

	bool GetChoiceForm(ibPropertyList* property);

	ibPropertyCategory* m_categoryInput = ibPropertyObject::CreatePropertyCategory(ibT("Input"), ibGETTEXT_IN_CONTEXT("field view", "Input"));
	ibPropertyBoolean* m_propertyPasswordMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInput, ibT("PasswordMode"), _("Password mode"), _("Whether the field hides what is typed behind placeholder characters, for secrets such as passwords. Off by default."), false);
	ibPropertyBoolean* m_propertyMultilineMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInput, ibT("MultilineMode"), _("Multiline mode"), _("Whether the field accepts several lines of text (Enter starts a new line) - for comments and addresses. Off by default: Enter finishes the input."), false);
	ibPropertyBoolean* m_propertyTexteditMode = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryInput, ibT("TexteditMode"), _("Textedit mode"), _("Whether the value can be typed into the field. Off: it can only be picked with the Select button or cleared. On by default; a field bound through a reference path is read-only whatever this says."), true);
	ibPropertyFormat* m_propertyFormat = ibPropertyObject::CreateProperty<ibPropertyFormat>(m_categoryInput, ibT("Format"), _("Format"),
		_("How the field shows its value, written per language: digits after the point, separators, a date pattern. Empty: the bound attribute's format, and without one a number shows as many digits after the point as its type keeps."), ibT(""));
	ibPropertyString* m_propertyMask = ibPropertyObject::CreateProperty<ibPropertyString>(m_categoryInput, ibT("Mask"), _("Mask"),
		_("What can be typed into the field, written as an attribute's Mask is: 0 9 # L l A a C c for the positions, > < <> for the case, \\ for a character as written, ! first for a value's shape (!#9999.99 a number, !dd.MM.yyyy a date); then ;1 or ;0 and the character an empty position shows. Empty: the bound attribute's mask, and without one a number takes as many digits as its type keeps and a date its digits. Not used in multi-line mode."), ibT(""));

	ibPropertyCategory* m_categoryChoice = ibPropertyObject::CreatePropertyCategory(ibT("Choice"), _("Choice"));
	ibPropertyList* m_propertyChoiceForm = ibPropertyObject::CreateProperty<ibPropertyList>(m_categoryChoice, ibT("ChoiceForm"), _("Choice form"),
		_("Which form opens when the user presses Select: one of the forms of the value's type. Empty: the type's default choice form."),
		&ibFieldViewInput::GetChoiceForm, ibNOT_FOUND);
	ibPropertyEnum<ibValueEnumQuickChoiceMode>* m_propertyQuickChoice = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumQuickChoiceMode>>(m_categoryChoice, ibT("QuickChoice"), _("Quick choice"),
		_("How a reference is picked into the field when Select is pressed. Auto: as its type says (the catalog's or the document's own Quick choice). Use: a drop-down of the matching items. Don't use: the list form."),
		ibQuickChoiceMode::ibQuickChoiceMode_Auto);
	ibPropertyEnum<ibValueEnumChoiceFoldersAndItems>* m_propertyChoiceFoldersAndItems = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumChoiceFoldersAndItems>>(m_categoryChoice, ibT("ChoiceFoldersAndItems"), _("Choice folders and items"),
		_("For a field referring to a hierarchical catalog: what may be picked into it - items, folders, or both. Auto: what the bound attribute's Select says."),
		ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Auto);
	// ⭐ THE FIELD'S OWN LINKS (#616) — shown on the field, they answer for it (variantChoiceLink.cpp, ControlOf).
	ibPropertyChoiceLink* m_propertyTypeLink = ibPropertyObject::CreateProperty<ibPropertyChoiceLink>(m_categoryChoice, ibT("TypeLink"), _("Link by type"),
		_("The field whose value decides the TYPE of a value written into this one: a characteristic gives the type it declares, a field holding a type description gives that, and any other field gives the type of the value standing in it. Offered: what the form reaches - its attributes and their fields. Empty: only the bound field's own link applies."));
	ibPropertyChoiceParameters* m_propertyChoiceParameters = ibPropertyObject::CreateProperty<ibPropertyChoiceParameters>(m_categoryChoice, ibT("ChoiceParameters"), _("Choice parameters"),
		_("What is shown in the list when a reference is chosen into the field, on top of what the bound field itself declares: a row per parameter - which field of the chosen object it filters, which field the form reaches gives the value (an attribute or a field of one), and what becomes of a chosen value when that field changes."));

	ibPropertyCategory* m_categoryButton = ibPropertyObject::CreatePropertyCategory(ibT("Button"), _("Button"));
	ibPropertyBoolean* m_propertySelectButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, ibT("ButtonSelect"), _("Select button"),
		_("Whether the field shows the Select button (...), which opens a choice form or list to pick the value from. On by default."), true);
	ibPropertyBoolean* m_propertyClearButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, ibT("ButtonClear"), _("Clear button"),
		_("Whether the field shows the Clear button (X), which empties the value. On by default."), true);
	ibPropertyBoolean* m_propertyOpenButton = ibPropertyObject::CreateProperty<ibPropertyBoolean>(m_categoryButton, ibT("ButtonOpen"), _("Open button"),
		_("Whether the field shows the Open button, which opens the referenced object's form. Off by default."), false);

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("OnChange"), _("Change"), std::vector<ibString>{ ibT("Control") });
	ibEventControl* m_eventStartChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("StartChoice"), _("Start choice"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventStartListChoice = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("StartListChoice"), _("Start list choice"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventClearing = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("Clearing"), _("Clearing"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("Opening"), _("Opening"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
	ibEventControl* m_eventChoiceProcessing = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("ChoiceProcessing"), _("Choice processing"), std::vector<ibString>{ ibT("Control"), ibT("ValueSelected"), ibT("StandardProcessing") });
};

// ⭐ FLAG — on or off: the value's truth shown, a Boolean written on a toggle.
class ibFieldViewFlag : public ibFieldViewProvider {
public:

	explicit ibFieldViewFlag(ibValueField* field) : ibFieldViewProvider(field) {}

	// Checked — the value's truth. None in the designer.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;
	// Toggle — the box clicked (Checked: the state the person toggled it to).
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	// Nothing it holds is of the source's type.
	virtual void OnViewChanged() override {}

private:

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("OnChange"), _("Change"), std::vector<ibString>{ ibT("Control") });
};

// ⭐ LABEL — shown, not edited: unbound, its title is its text (a decoration); bound, the value's presentation beside the
// title, a link that opens the value when there is one.
class ibFieldViewLabel : public ibFieldViewProvider {
public:

	explicit ibFieldViewLabel(ibValueField* field) : ibFieldViewProvider(field) {}

	// Text — the value's presentation, bound only; Link — whether it leads somewhere.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;
	// Open — the link clicked.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;
	virtual bool IsReadOnly() const override { return true; }
	virtual bool IsBindingView() const override { return false; }

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	// Nothing it holds is of the source's type.
	virtual void OnViewChanged() override {}

private:

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	// The text field's event, under the same name and with the same standard processing.
	ibEventControl* m_eventOpening = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("Opening"), _("Opening"), std::vector<ibString>{ ibT("Control"), ibT("StandardProcessing") });
};

// ⭐ SWITCH — one of its Choices picked: the items set on the field, each the value it writes and the text it shows; the
// item whose value the field holds is the chosen one.
class ibFieldViewSwitch : public ibFieldViewProvider {
public:

	explicit ibFieldViewSwitch(ibValueField* field) : ibFieldViewProvider(field) {}

	// Choices — a child per item, its Caption, Selected on the one whose value the field holds.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;
	// Change — an item picked (Member: its place among the Choices); a place it does not have, or a read-only field,
	// writes nothing.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;
	// A switch of no items picks nothing — the one it shows says it is empty.
	virtual bool IsReadOnly() const override { return !m_propertyChoices->GetValueAsChoicesDesc().IsOk(); }

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	// The items kept of the field's type, those of another cleared.
	virtual void OnViewChanged() override;

private:

	// The value of the item at `place`, made by the field's application; false past the items, or for a value of a type
	// the application no longer has.
	bool GetChoiceValue(size_t place, ibValue& value) const;

	ibPropertyCategory* m_categorySwitch = ibPropertyObject::CreatePropertyCategory(ibT("Switch"), ibGETTEXT_IN_CONTEXT("field view", "Switch"));
	ibPropertyChoices* m_propertyChoices = ibPropertyObject::CreateProperty<ibPropertyChoices>(m_categorySwitch, ibT("Choices"), _("Choices"),
		_("The items the switch offers, in order: the value each writes into the field and the text it shows. A value is of the field's type - a string, a number, a date, a Boolean or an enumeration's member. Empty text: the value's own presentation."));
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySwitch, ibT("Orient"), _("Orient"),
		_("How the items stand: side by side (the default) or one under another."),
		ibOrientation_Horizontal);

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("OnChange"), _("Change"), std::vector<ibString>{ ibT("Control") });
};

// ⭐ GAUGE — how far a number has come of its Range: the value shown, not edited.
class ibFieldViewGauge : public ibFieldViewProvider {
public:

	explicit ibFieldViewGauge(ibValueField* field) : ibFieldViewProvider(field) {}

	// Position — the value, from 0 to the range. None in the designer.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;
	virtual bool IsReadOnly() const override { return true; }

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	// Nothing it holds is of the source's type.
	virtual void OnViewChanged() override {}

private:

	ibPropertyCategory* m_categoryGauge = ibPropertyObject::CreatePropertyCategory(ibT("Gauge"), ibGETTEXT_IN_CONTEXT("field view", "Gauge"));
	ibPropertyInteger* m_propertyRange = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categoryGauge, ibT("Range"), _("Range"),
		_("The value at which the progress bar is full. Default 100, so the value reads as a percentage."), 100);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categoryGauge, ibT("Orient"), _("Orient"),
		_("The progress bar's direction: horizontal (the default, filling left to right) or vertical."), ibOrientation_Horizontal);
};

// ⭐ SLIDER — a number moved between its ends: the value where the thumb stands, a Number written where it is let go.
class ibFieldViewSlider : public ibFieldViewProvider {
public:

	explicit ibFieldViewSlider(ibValueField* field) : ibFieldViewProvider(field) {}

	// Position — the value, between the ends. None in the designer.
	virtual void OnUpdate(ibDataNode& state, ibVisualHost* host) override;
	// Change — the thumb let go (Position: where), held between the ends; a read-only field writes nothing.
	virtual bool OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args) override;

	virtual bool ReadData(const ibDataNode& node) override;
	virtual bool WriteData(ibDataNode& node) const override;

protected:

	// Nothing it holds is of the source's type.
	virtual void OnViewChanged() override {}

private:

	ibPropertyCategory* m_categorySlider = ibPropertyObject::CreatePropertyCategory(ibT("Slider"), ibGETTEXT_IN_CONTEXT("field view", "Slider"));
	ibPropertyInteger* m_propertyMinValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, ibT("MinValue"), _("Min value"),
		_("The slider's smallest value, at its start. Default 0."), 0);
	ibPropertyInteger* m_propertyMaxValue = ibPropertyObject::CreateProperty<ibPropertyInteger>(m_categorySlider, ibT("MaxValue"), _("Max value"),
		_("The slider's largest value, at its end. Default 100."), 100);
	ibPropertyEnum<ibValueEnumOrient>* m_propertyOrient = ibPropertyObject::CreateProperty<ibPropertyEnum<ibValueEnumOrient>>(m_categorySlider, ibT("Orient"), _("Orient"),
		_("The slider's direction: horizontal (the default) or vertical."), ibOrientation_Horizontal);

	ibPropertyCategory* m_categoryEvent = ibPropertyObject::CreatePropertyCategory(ibT("Event"), _("Event"));
	ibEventControl* m_eventOnChange = ibPropertyObject::CreateEvent<ibEventControl>(m_categoryEvent, ibT("OnChange"), _("Change"), std::vector<ibString>{ ibT("Control") });
};

#endif
