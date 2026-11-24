#include "tableBox.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"     // ibDataNode — a cell written, an event's arguments
#include "core/inputMask.h"                 // ibInputMask — what can be typed into a cell
#include "backend/backend_exception.h"      // ibCoreException — a value of a type the application lacks
#include "backend/choiceLinkResolver.h"     // ibChoiceLinkResolver — an Input's own choice parameters read
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/metadataSerialization.h"  // ibDeserializeValue — a value the client sent with its type
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/system/systemManager.h"   // ibValueSystemFunction::Message — a value typed in part is said

//****************************************************************************
//*                                 Input                                    *
//****************************************************************************

bool ibColumnViewInput::GetChoiceForm(ibPropertyList* property)
{
	const ibValueModelTableBoxColumn* const column = GetColumn();
	const ibMetaData* const metaData = GetMetaData();
	if (metaData == nullptr)
		return true;

	const ibValueMetaObjectRecordDataRef* metaObject = nullptr;
	if (!column->GetSourcePath().empty()) {
		// The bound field resolved through the source explorer (WalkSource), not the form's own metaobject: a dotted
		// path's leaf — or a value-table / dynamic-list column with no backing metaobject — is missed by a source-scoped
		// FindAnyObjectByFilter.
		const ibBackendSourceColumn* attribute = column->WalkSource(column->GetSourceDesc());
		if (attribute != nullptr) {
			const ibCtorMetaValueType* so = metaData->GetTypeCtor(attribute->GetTypeDesc().GetFirstClsid());
			if (so != nullptr)
				metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
		}
	}
	else {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(column->GetFirstClsid());
		if (so != nullptr)
			metaObject = dynamic_cast<const ibValueMetaObjectRecordDataRef*>(so->GetMetaObject());
	}

	if (metaObject != nullptr) {
		for (auto form : metaObject->GetFormArrayObject())
			property->AppendItem(form->GetSynonym(), form->GetMetaID(), form->GetClassType(), form);
	}

	return true;
}

ibInputMask ibColumnViewInput::GetMask() const
{
	if (m_propertyMultilineMode->GetValueAsBoolean())
		return ibInputMask();
	const ibString text = m_propertyMask->GetValueAsString();
	return !text.IsEmpty() ? ibInputMask::Parse(text) : ibColumnViewProvider::GetMask();
}

bool ibColumnViewInput::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	switch (event) {
	case ibProtocol::ibEvent::ibKind::Change:
		// The value with its type when the client has one (Value), else the text it typed.
		if (const auto value = args.FindChild(ibProtocol::ibEvent::Value))
			ValueProcessing(*value);
		else
			TextProcessing(args.GetValue<ibString>(ibProtocol::ibEvent::Text));
		return true;
	case ibProtocol::ibEvent::ibKind::Toggle: {
		// The check box a boolean's cell is edited with (Max, 2026-11-24), committed as a Flag's box is toggled: the state
		// it was ticked to, written as a Boolean.
		ibValueModelTableBoxColumn* const column = GetColumn();
		if (!column->IsReadOnly()) {
			column->SetControlValue(ibValue(args.GetValue<bool>(ibProtocol::ibEvent::Checked)));
			column->CallAsEvent(m_eventOnChange, column->GetValue());
		}
		return true;
	}
	case ibProtocol::ibEvent::ibKind::Select: OnSelectButtonPressed(); return true;
	case ibProtocol::ibEvent::ibKind::Open:   OnOpenButtonPressed();   return true;
	case ibProtocol::ibEvent::ibKind::Clear:  OnClearButtonPressed();  return true;
	default:                                  return false;
	}
}

//*******************************************************************
//*							 Choice     	                        *
//*******************************************************************

// THE COLUMN'S OWN WORD over what its type says (issue #259): Use and Don't use say it outright, Auto asks the type.
bool ibColumnViewInput::HasQuickChoice() const
{
	switch (m_propertyQuickChoice->GetValueAsEnum()) {
	case ibQuickChoiceMode::ibQuickChoiceMode_Use: return true;
	case ibQuickChoiceMode::ibQuickChoiceMode_DontUse: return false;
	case ibQuickChoiceMode::ibQuickChoiceMode_Auto: break;
	}
	return ibColumnViewProvider::HasQuickChoice();
}

// …and over what its attribute's Select says.
ibSelectMode ibColumnViewInput::GetSelectMode() const
{
	switch (m_propertyChoiceFoldersAndItems->GetValueAsEnum()) {
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Items: return ibSelectMode::ibSelectMode_Items;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Folders: return ibSelectMode::ibSelectMode_Folders;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_FoldersAndItems: return ibSelectMode::ibSelectMode_FoldersAndItems;
	case ibChoiceFoldersAndItems::ibChoiceFoldersAndItems_Auto: break;
	}
	return ibColumnViewProvider::GetSelectMode();
}

// …and its own rows over its field's, read in the form (#616).
ibChoiceCondition ibColumnViewInput::GetChoiceCondition() const
{
	const ibValueModelTableBoxColumn* const column = GetColumn();
	return ibChoiceLinkResolver::Resolve(column->GetChoiceHolder(), column->GetSourceDesc().GetLeaf(), GetChoiceParameters(),
		ibChoiceHolder(column->GetOwnerForm()));
}

// ⭐ THE SAME DOOR A TYPED VALUE GOES THROUGH (SetControlValue), as on a form's field.
void ibColumnViewInput::ChoiceProcessing(ibValue& selected)
{
	ibValueModelTableBoxColumn* const column = GetColumn();
	ibValue standardProcessing = true;
	column->CallAsEvent(m_eventChoiceProcessing, column->GetValue(), selected, standardProcessing);
	if (standardProcessing.GetBoolean()) {
		column->SetControlValue(selected);
		column->CallAsEvent(m_eventOnChange, column->GetValue());
	}
}

//*******************************************************************
//*                             Events                              *
//*******************************************************************

// The typed text becomes a value of the cell's type, or is refused. A refused text leaves the cell as it was, and the next
// fetch of the row shows that value again in place of what was typed. A cell whose text edit mode is off is picked or
// cleared, never typed into — as a field's (Max, 2026-11-24).
bool ibColumnViewInput::TextProcessing(const ibString& typed)
{
	ibValueModelTableBoxColumn* const column = GetColumn();
	if (column->IsReadOnly() || !m_propertyTexteditMode->GetValueAsBoolean())
		return false;

	// Through the mask first, as a field reads it.
	ibString strData, refusal;
	if (!column->GetMask().Strip(typed, strData, &refusal)) {
		ibValueSystemFunction::Message(refusal, ibStatusMessage::ibStatusMessage_Warning);
		return false;
	}

	const ibMetaData* const metaData = GetMetaData();
	ibASSERT(metaData);
	if (metaData == nullptr)
		return false;
	ibValue selValue; column->GetControlValue(selValue);
	const ibValue& newValue = metaData->CreateObject(selValue.GetClassType());
	if (newValue.GetType() == ibValueTypes::TYPE_EMPTY)
		return false;
	if (strData.Length() > 0) {
		std::vector<ibValue> listValue;
		if (newValue.FindValue(strData, GetChoiceCondition(), listValue)) {
			column->SetControlValue(listValue.at(0));
		}
		else {
			// …and a number or a date its type cannot read is said, as a field says it.
			if (newValue.GetType() == ibValueTypes::TYPE_NUMBER || newValue.GetType() == ibValueTypes::TYPE_DATE)
				ibValueSystemFunction::Message(ibString::Format(_("The value typed cannot be read: %s"), strData),
					ibStatusMessage::ibStatusMessage_Warning);
			return false;
		}
	}
	else {
		column->SetControlValue(newValue);
	}

	column->CallAsEvent(m_eventOnChange, column->GetValue());
	return true;
}

// The value the client sent with its type becomes the cell's — made by the application (a reference is its), and only of a
// type the column admits; anything else is refused as a text that is no value is.
bool ibColumnViewInput::ValueProcessing(const ibDataNode& node)
{
	ibValueModelTableBoxColumn* const column = GetColumn();
	if (column->IsReadOnly())
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
	if (!column->GetTypeValueDesc().ContainType(newValue.GetClassType()))
		return false;

	column->SetControlValue(newValue);
	column->CallAsEvent(m_eventOnChange, column->GetValue());
	return true;
}

void ibColumnViewInput::OnSelectButtonPressed()
{
	ibValueModelTableBoxColumn* const column = GetColumn();
	if (column->IsReadOnly())
		return;

	ibValue standardProcessing = true;
	column->CallAsEvent(m_eventStartChoice, column->GetValue(), standardProcessing);
	if (!standardProcessing.GetBoolean())
		return;

	// THE ONE ROUTE (ibTypeControlFactory::ChooseValue), with the form the author picked (null = the metaobject's own).
	const ibMetaID& formId = m_propertyChoiceForm->GetValueAsInteger();
	const ibMetaData* const metaData = GetMetaData();
	const ibValueMetaObject* choiceForm = (formId != ibNOT_FOUND && metaData != nullptr)
		? metaData->FindAnyObjectByFilter(formId) : nullptr;
	ibTypeControlFactory::ChooseValue(column, choiceForm);
}

void ibColumnViewInput::OnOpenButtonPressed()
{
	ibValueModelTableBoxColumn* const column = GetColumn();
	ibValue standardProcessing = true;
	column->CallAsEvent(m_eventOpening, column->GetValue(), standardProcessing);
	if (standardProcessing.GetBoolean()) {
		ibValue selValue;
		if (column->GetControlValue(selValue) && !selValue.IsEmpty())
			selValue.ShowValue();
	}
}

void ibColumnViewInput::OnClearButtonPressed()
{
	ibValueModelTableBoxColumn* const column = GetColumn();
	if (column->IsReadOnly())
		return;

	ibValue standardProcessing = true;
	column->CallAsEvent(m_eventClearing, column->GetValue(), standardProcessing);
	if (standardProcessing.GetBoolean())
		column->SetControlValue();
}

//*******************************************************************
//*                            Property                             *
//*******************************************************************

void ibColumnViewInput::OnPropertyRefresh()
{
	const ibValueModelTableBoxColumn* const column = GetColumn();
	const ibMetaData* const metaData = GetMetaData();

	// A choice form can only be offered when the column points at ONE reference type.
	bool hasChoice = false;
	if (metaData != nullptr && column->GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(column->GetFirstClsid());
		hasChoice = so != nullptr
			&& so->GetMetaTypeCtor() == ibCtorObjectMetaType::ibCtorObjectMetaType_Reference;
	}
	HideProperty(m_propertyChoiceForm, !hasChoice);

	// How a reference is picked — only where the column holds a reference.
	bool hasReference = false;
	for (const ibClassID& clsid : column->GetTypeDesc().GetClsidList())
		hasReference = hasReference || ::IsReference(clsid);
	HideProperty(m_propertyQuickChoice, !hasReference);
	HideProperty(m_propertyChoiceFoldersAndItems, !hasReference);
	// …and what narrows its list (#616). The link by type stays, as an attribute's does.
	HideProperty(m_propertyChoiceParameters, !hasReference);
}

// THE COLUMN'S SOURCE CHANGED — the choice form is one of its type's forms: the list is read afresh for the type, and the
// property set again to its own answer — the form where it is still among them, none where it is not.
void ibColumnViewInput::OnViewChanged()
{
	m_propertyChoiceForm->FillList();
	m_propertyChoiceForm->SetValue(m_propertyChoiceForm->GetValueAsInteger());
}

//*******************************************************************
//*                              Data	                            *
//*******************************************************************

bool ibColumnViewInput::ReadData(const ibDataNode& node)
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

bool ibColumnViewInput::WriteData(ibDataNode& node) const
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
