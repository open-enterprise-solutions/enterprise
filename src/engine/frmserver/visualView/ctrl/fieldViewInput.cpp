#include "field.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"     // ibDataNode — the state written, an event's arguments
#include "core/formatString.h"              // ibFormatString — what the field shows its value through
#include "core/inputMask.h"                 // ibInputMask — what can be typed into it
#include "backend/appData.h"
#include "backend/backend_exception.h"      // ibCoreException — a value of a type the application lacks
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/metadataSerialization.h"  // ibDeserializeValue — a value the client sent with its type
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message — a value typed in part is said

//****************************************************************************
//*                                 Input                                    *
//****************************************************************************

// The text field standing in the field's slot: it asks the field where the field answers — the value, the source, the
// type, the title.

bool ibFieldViewInput::GetChoiceForm(ibPropertyList* property)
{
	const ibValueField* const field = GetField();
	const ibMetaData* const metaData = GetMetaData();
	if (metaData == nullptr)
		return true;

	const ibValueMetaObjectRecordDataRef* metaObject = nullptr;
	if (!field->HasValueInControl()) {
		// The bound field resolved application-wide — a dotted path's leaf lives in a referenced type (WalkSource).
		const ibBackendSourceColumn* attribute = field->WalkSource(field->GetSourceDesc());
		if (attribute != nullptr) {
			const ibCtorMetaValueType* so = metaData->GetTypeCtor(attribute->GetTypeDesc().GetFirstClsid());
			if (so != nullptr)
				metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
		}
	}
	else {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(field->GetFirstClsid());
		if (so != nullptr)
			metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
	}

	if (metaObject != nullptr) {
		for (auto form : metaObject->GetFormArrayObject())
			property->AppendItem(form->GetSynonym(), form->GetMetaID(), form->GetClassType(), form);
	}

	return true;
}

// The format of the field the binding names — asked of the source that holds it, by the binding's leaf.
const ibTranslateString& ibFieldViewInput::GetSourceFormat() const
{
	return GetField()->GetChoiceHolder().GetFormat(GetField()->GetSourceDesc().GetLeaf());
}

ibString ibFieldViewInput::GetSourceMask() const
{
	return GetField()->GetChoiceHolder().GetMask(GetField()->GetSourceDesc().GetLeaf());
}

ibInputMask ibFieldViewInput::GetMask() const
{
	ibInputMask mask;
	if (m_propertyMultilineMode->GetValueAsBoolean())
		return mask;
	ibString text = m_propertyMask->GetValueAsString();
	if (text.IsEmpty())
		text = GetSourceMask();
	if (!text.IsEmpty())
		mask = ibInputMask::Parse(text);
	else
		ibInputMask::FromTypeDesc(GetField()->GetTypeDesc(), mask);
	return mask;
}

void ibFieldViewInput::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibValueField* const field = GetField();

	// What can be typed into it — the mask in effect, resolved here: the client resolves nothing. In the designer as
	// well, where the field shows no value and the mask's skeleton stands in for one.
	const ibInputMask mask = GetMask();
	if (!mask.IsEmpty())
		state.SetValue(ibProtocol::ibForm::ibState::Mask, mask.Render());

	// The value as text, through the field's own format or, without one, the bound attribute's. Not in the designer:
	// there the field shows no value.
	if (appData->DesignerMode())
		return;

	ibValue value;
	field->GetControlValue(value);

	ibString text;
	const ibTranslateString& format = m_propertyFormat->GetValueAsFormatString();
	if (mask.IsEmpty())
		ibValueField::GetFormatFromColumn(!format.IsEmpty() ? format : GetSourceFormat(), field->GetTypeDesc()).Apply(value, text);
	else if (value.GetType() == ibValueTypes::TYPE_STRING)
		text = mask.Apply(value.GetString());   // a string shows through its mask: `5551234567` as `+1 (555) 123-45-67`
	else if (value.GetType() != ibValueTypes::TYPE_DATE || !value.IsEmpty()) {
		// (an empty date shows no text: the client draws the mask's skeleton in place of 01.01.0001)
		ibValueField::GetFormatFromColumn(!format.IsEmpty() ? format : GetSourceFormat(), field->GetTypeDesc()).Apply(value, text);
		// …and where that is not the text the mask is typed over (a long date, digit groups), the one the editor starts
		// from: the value through the format its type gives.
		static const ibTranslateString s_typeFormat;
		ibString editText;
		ibValueField::GetFormatFromColumn(s_typeFormat, field->GetTypeDesc()).Apply(value, editText);
		if (editText != text)
			state.SetValue(ibProtocol::ibForm::ibState::EditText, editText);
	}
	state.SetValue(ibProtocol::ibForm::ibState::Text, text);
	// …and the value itself, with its type (ibValue::Serialize): a client edits by the type — a date as a date — and may
	// answer a Change with the value instead of text. One that does not travel (an open object) is shown by its text.
	if (value.IsTransferable())
		value.Serialize(state.Child(ibProtocol::ibForm::ibState::Value));
}

//*******************************************************************
//*							 Choice     	                        *
//*******************************************************************

// THE FIELD'S OWN WORD over what its type says (issue #259): Use and Don't use say it outright, Auto asks the type.
bool ibFieldViewInput::HasQuickChoice() const
{
	switch (m_propertyQuickChoice->GetValueAsEnum()) {
	case ibQuickChoiceMode::ibQuickChoiceMode_Use: return true;
	case ibQuickChoiceMode::ibQuickChoiceMode_DontUse: return false;
	case ibQuickChoiceMode::ibQuickChoiceMode_Auto: break;
	}
	return ibFieldViewProvider::HasQuickChoice();
}

// …and over what its attribute's Select says: a form may let folders be picked into a field whose attribute takes items.
ibSelectMode ibFieldViewInput::GetSelectMode() const
{
	switch (m_propertyChoiceFoldersAndItems->GetValueAsEnum()) {
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Items: return ibSelectMode::ibSelectMode_Items;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Folders: return ibSelectMode::ibSelectMode_Folders;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_FoldersAndItems: return ibSelectMode::ibSelectMode_FoldersAndItems;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Auto: break;
	}
	return ibFieldViewProvider::GetSelectMode();
}

// …and its own rows over its field's, read in the form (#616).
ibChoiceCondition ibFieldViewInput::GetChoiceCondition() const
{
	const ibValueField* const field = GetField();
	return ibChoiceLinkResolver::Resolve(field->GetChoiceHolder(), field->GetSourceDesc().GetLeaf(), GetChoiceParameters(),
		ibChoiceHolder(field->GetOwnerForm()));
}

void ibFieldViewInput::ChoiceProcessing(ibValue& selected)
{
	ibValueField* const field = GetField();
	ibValue standardProcessing = true;
	field->CallAsEvent(m_eventChoiceProcessing, field->GetValue(), selected, standardProcessing);
	if (standardProcessing.GetBoolean()) {
		field->SetControlValue(selected);
		field->CallAsEvent(m_eventOnChange, field->GetValue());
	}
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

bool ibFieldViewInput::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	switch (event) {
	case ibProtocol::ibEvent::ibKind::Input:  OnTextUpdated(); return true;
	case ibProtocol::ibEvent::ibKind::Change: {
		// The value with its type when the client has one (Value), else the text it typed.
		if (const auto value = args.FindChild(ibProtocol::ibEvent::Value))
			ValueProcessing(*value);
		else
			TextProcessing(args.GetValue<ibString>(ibProtocol::ibEvent::Text));
		return true;
	}
	case ibProtocol::ibEvent::ibKind::Select: OnSelectButtonPressed(); return true;
	case ibProtocol::ibEvent::ibKind::Open:   OnOpenButtonPressed(); return true;
	case ibProtocol::ibEvent::ibKind::Clear:  OnClearButtonPressed(); return true;
	default:                                  return false;
	}
}

bool ibFieldViewInput::IsSubscribed(ibProtocol::ibEvent::ibKind event) const
{
	if (event != ibProtocol::ibEvent::ibKind::Input)
		return false;
	// What OnTextUpdated would modify, and only while it is not.
	const ibValueField* const field = GetField();
	ibValueForm* const form = field->GetOwnerForm();
	ibSourceDataObject* const sourceObject = form != nullptr ? form->GetSourceObject() : nullptr;
	return !field->IsReadOnly() && m_propertyTexteditMode->GetValueAsBoolean()
		&& sourceObject != nullptr && sourceObject->ModifiesData() && !sourceObject->IsModified();
}

// The typed text becomes a value of the field's type, or is refused. A refused text leaves the value as it was, and the
// next Update shows that value again in place of what was typed. A read-only field takes no text at all, nor one whose
// text edit mode is off: its value is picked (Select) or cleared, never typed (Max, 2026-11-24).
bool ibFieldViewInput::TextProcessing(const ibString& typed)
{
	ibValueField* const field = GetField();
	if (field->IsReadOnly() || !m_propertyTexteditMode->GetValueAsBoolean())
		return false;

	// THROUGH THE MASK FIRST — what was typed into its positions, read back (docs/private/input-mask.md). A value typed
	// in part is refused and the person is told why: the positions they left empty would be lost otherwise.
	ibString strData, refusal;
	if (!GetMask().Strip(typed, strData, &refusal)) {
		ibValueSystemFunction::Message(refusal, ibStatusMessage::ibStatusMessage_Warning);
		return false;
	}

	const ibMetaData* const metaData = GetMetaData();
	ibASSERT(metaData);
	if (metaData == nullptr)
		return false;
	ibValue selValue; field->GetControlValue(selValue);
	const ibValue& newValue = metaData->CreateObject(selValue.GetClassType());
	if (newValue.GetType() == ibValueTypes::TYPE_EMPTY)
		return false;
	if (strData.Length() > 0) {
		std::vector<ibValue> listValue;
		if (newValue.FindValue(strData, GetChoiceCondition(), listValue)) {
			field->SetControlValue(listValue.at(0));
		}
		else {
			// A number or a date its type cannot read — a 30th of February — is said: the field shows the value it had
			// again, and nothing else would tell why.
			if (newValue.GetType() == ibValueTypes::TYPE_NUMBER || newValue.GetType() == ibValueTypes::TYPE_DATE)
				ibValueSystemFunction::Message(ibString::Format(_("The value typed cannot be read: %s"), strData),
					ibStatusMessage::ibStatusMessage_Warning);
			return false;
		}
	}
	else {
		field->SetControlValue(newValue);
	}

	field->CallAsEvent(m_eventOnChange, field->GetValue());
	return true;
}

// The value the client sent with its type becomes the field's — made by the application (a reference is its), and only
// of a type the field admits; anything else is refused as a text that is no value is.
bool ibFieldViewInput::ValueProcessing(const ibDataNode& node)
{
	ibValueField* const field = GetField();
	if (field->IsReadOnly())
		return false;

	const ibMetaData* const metaData = GetMetaData();
	if (metaData == nullptr)
		return false;
	ibValue newValue;
	try {
		newValue = ibDeserializeValue(node, metaData);
	}
	catch (const ibCoreException&) {
		return false;   // a type this application does not have
	}
	if (!field->GetTypeValueDesc().ContainType(newValue.GetClassType()))
		return false;

	field->SetControlValue(newValue);
	field->CallAsEvent(m_eventOnChange, field->GetValue());
	return true;
}

// Typing (or the text cleared by hand) — nothing is committed yet, but the form's object is modified from the first
// keystroke.
void ibFieldViewInput::OnTextUpdated()
{
	const ibValueField* const field = GetField();
	if (field->IsReadOnly() || !m_propertyTexteditMode->GetValueAsBoolean())
		return;

	ibValueForm* const form = field->GetOwnerForm();
	ibSourceDataObject* const sourceObject = form != nullptr ? form->GetSourceObject() : nullptr;
	if (sourceObject != nullptr && sourceObject->ModifiesData())
		sourceObject->Modify(true);
}

void ibFieldViewInput::OnSelectButtonPressed()
{
	ibValueField* const field = GetField();
	if (field->IsReadOnly())
		return;

	// The script may take the choice over entirely (StartChoice + standard processing off) — that decision belongs to
	// the field, not to the route.
	ibValue standardProcessing = true;
	field->CallAsEvent(m_eventStartChoice, field->GetValue(), standardProcessing);
	if (!standardProcessing.GetBoolean())
		return;

	// THE ONE ROUTE (ibTypeControlFactory::ChooseValue), with the form the author picked (null = the metaobject's own).
	const ibMetaID& formId = m_propertyChoiceForm->GetValueAsInteger();
	const ibMetaData* const metaData = GetMetaData();
	const ibValueMetaObject* choiceForm = (formId != ibNOT_FOUND && metaData != nullptr)
		? metaData->FindAnyObjectByFilter(formId) : nullptr;
	ibTypeControlFactory::ChooseValue(field, choiceForm);
}

void ibFieldViewInput::OnOpenButtonPressed()
{
	ibValueField* const field = GetField();
	ibValue standardProcessing = true;
	field->CallAsEvent(m_eventOpening, field->GetValue(), standardProcessing);
	if (standardProcessing.GetBoolean()) {
		ibValue selValue;
		if (field->GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void ibFieldViewInput::OnClearButtonPressed()
{
	ibValueField* const field = GetField();
	if (field->IsReadOnly())
		return;

	ibValue standardProcessing = true;
	field->CallAsEvent(m_eventClearing, field->GetValue(), standardProcessing);
	if (standardProcessing.GetBoolean())
		field->SetControlValue();
}

//*******************************************************************
//*                            Property                             *
//*******************************************************************

void ibFieldViewInput::OnPropertyRefresh()
{
	const ibValueField* const field = GetField();
	const ibMetaData* const metaData = GetMetaData();

	// A choice form can only be offered when the field points at ONE reference type.
	bool hasChoice = false;
	if (metaData != nullptr && field->GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(field->GetFirstClsid());
		hasChoice = so != nullptr
			&& so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference;
	}
	HideProperty(m_propertyChoiceForm, !hasChoice);

	// How a reference is picked — only where the field holds a reference.
	bool hasReference = false;
	for (const ibClassID& clsid : field->GetTypeDesc().GetClsidList())
		hasReference = hasReference || ::IsReference(clsid);
	HideProperty(m_propertyQuickChoice, !hasReference);
	HideProperty(m_propertyChoiceFoldersAndItems, !hasReference);
	// …and what narrows its list (#616). The link by type stays, as an attribute's does.
	HideProperty(m_propertyChoiceParameters, !hasReference);
}

// THE FIELD'S SOURCE CHANGED — the choice form is one of its type's forms: the list is read afresh for the type, and the
// property set again to its own answer — the form where it is still among them, none where it is not
// (ibPropertyList::GetValueAsInteger). As the switch keeps its items.
void ibFieldViewInput::OnViewChanged()
{
	m_propertyChoiceForm->FillList();
	m_propertyChoiceForm->SetValue(m_propertyChoiceForm->GetValueAsInteger());

	// …and the select button is the type's, as the form's builder gave it: none where one Boolean, Number or String is
	// typed in — nothing to pick it from — and there where the value is picked (a reference, a date, several types).
	const ibTypeDescription& typeDesc = GetField()->GetTypeDesc();
	const ibClassID only = typeDesc.GetClsidCount() == 1 ? typeDesc.GetFirstClsid() : 0;
	m_propertySelectButton->SetValue(only != g_valueBooleanCLSID && only != g_valueNumberCLSID && only != g_valueStringCLSID);
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibFieldViewInput::ReadData(const ibDataNode& node)
{
	m_propertyPasswordMode->SetNodeValue(node.GetProperty(m_propertyPasswordMode->GetName()));
	m_propertyMultilineMode->SetNodeValue(node.GetProperty(m_propertyMultilineMode->GetName()));
	m_propertyTexteditMode->SetNodeValue(node.GetProperty(m_propertyTexteditMode->GetName()));
	m_propertyFormat->SetNodeValue(node.GetProperty(m_propertyFormat->GetName()));
	m_propertyMask->SetNodeValue(node.GetProperty(m_propertyMask->GetName()));
	m_propertyChoiceForm->SetNodeValue(node.GetProperty(m_propertyChoiceForm->GetName()));
	m_propertyQuickChoice->SetNodeValue(node.GetProperty(m_propertyQuickChoice->GetName()));
	m_propertyChoiceFoldersAndItems->SetNodeValue(node.GetProperty(m_propertyChoiceFoldersAndItems->GetName()));
	m_propertyTypeLink->SetNodeValue(node.GetProperty(m_propertyTypeLink->GetName()));
	m_propertyChoiceParameters->SetNodeValue(node.GetProperty(m_propertyChoiceParameters->GetName()));
	m_propertySelectButton->SetNodeValue(node.GetProperty(m_propertySelectButton->GetName()));
	m_propertyClearButton->SetNodeValue(node.GetProperty(m_propertyClearButton->GetName()));
	m_propertyOpenButton->SetNodeValue(node.GetProperty(m_propertyOpenButton->GetName()));

	//events
	m_eventOnChange->SetNodeValue(node.GetProperty(m_eventOnChange->GetName()));
	m_eventStartChoice->SetNodeValue(node.GetProperty(m_eventStartChoice->GetName()));
	m_eventStartListChoice->SetNodeValue(node.GetProperty(m_eventStartListChoice->GetName()));
	m_eventClearing->SetNodeValue(node.GetProperty(m_eventClearing->GetName()));
	m_eventOpening->SetNodeValue(node.GetProperty(m_eventOpening->GetName()));
	m_eventChoiceProcessing->SetNodeValue(node.GetProperty(m_eventChoiceProcessing->GetName()));
	return true;
}

bool ibFieldViewInput::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyPasswordMode->GetName(), m_propertyPasswordMode->GetNodeValue());
	node.SetProperty(m_propertyMultilineMode->GetName(), m_propertyMultilineMode->GetNodeValue());
	node.SetProperty(m_propertyTexteditMode->GetName(), m_propertyTexteditMode->GetNodeValue());
	node.SetProperty(m_propertyFormat->GetName(), m_propertyFormat->GetNodeValue());
	node.SetProperty(m_propertyMask->GetName(), m_propertyMask->GetNodeValue());
	node.SetProperty(m_propertyChoiceForm->GetName(), m_propertyChoiceForm->GetNodeValue());
	node.SetProperty(m_propertyQuickChoice->GetName(), m_propertyQuickChoice->GetNodeValue());
	node.SetProperty(m_propertyChoiceFoldersAndItems->GetName(), m_propertyChoiceFoldersAndItems->GetNodeValue());
	node.SetProperty(m_propertyTypeLink->GetName(), m_propertyTypeLink->GetNodeValue());
	node.SetProperty(m_propertyChoiceParameters->GetName(), m_propertyChoiceParameters->GetNodeValue());
	node.SetProperty(m_propertySelectButton->GetName(), m_propertySelectButton->GetNodeValue());
	node.SetProperty(m_propertyClearButton->GetName(), m_propertyClearButton->GetNodeValue());
	node.SetProperty(m_propertyOpenButton->GetName(), m_propertyOpenButton->GetNodeValue());

	//events
	node.SetProperty(m_eventOnChange->GetName(), m_eventOnChange->GetNodeValue());
	node.SetProperty(m_eventStartChoice->GetName(), m_eventStartChoice->GetNodeValue());
	node.SetProperty(m_eventStartListChoice->GetName(), m_eventStartListChoice->GetNodeValue());
	node.SetProperty(m_eventClearing->GetName(), m_eventClearing->GetNodeValue());
	node.SetProperty(m_eventOpening->GetName(), m_eventOpening->GetNodeValue());
	node.SetProperty(m_eventChoiceProcessing->GetName(), m_eventChoiceProcessing->GetNodeValue());
	return true;
}
