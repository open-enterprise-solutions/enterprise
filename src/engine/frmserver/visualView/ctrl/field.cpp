#include "widgets.h"
#include "form.h"
#include "core/serialize/dataBuilder.h"                         // ibDataNode — the State, an event's arguments
#include "backend/appData.h"                                     // DesignerMode — no value in the designer
#include "backend/metaData.h"
#include "backend/objCtor.h"
#include "backend/metaCollection/partial/commonObject.h"        // ibValueMetaObjectRecordDataEnumRef — its members
#include "backend/metaCollection/partial/reference/reference.h" // ibValueReferenceDataObject — a member as a value
#include "backend/system/value/valueType.h"                     // ibValueTypeDescription::AdjustValue — a line as a value

//****************************************************************************
//*                          The kinds of a field                            *
//****************************************************************************

namespace {

// INPUT — a text to type: the text box, as it is.
class ibFieldKindInput final : public ibFieldKindRole {
public:
	void OnUpdate(ibValueField& field, ibDataNode& state, ibVisualHost* host) const override
	{
		field.UpdateAsText(state, host);
	}

	bool OnClientEvent(ibValueField& field, ibProtocol::ibEvent::ibKind event, const ibDataNode& args) const override
	{
		return field.ClientEventAsText(event, args);
	}
};

// RADIO BUTTONS — a button per choice, the one the source holds selected; a click on another writes it in. Nothing is
// typed, so only the binding decides whether it may be written (TextEditMode is the typing's).
class ibFieldKindRadioButtons final : public ibFieldKindRole {
public:
	void OnUpdate(ibValueField& field, ibDataNode& state, ibVisualHost* host) const override
	{
		field.ibValueWindow::OnUpdate(state, host);

		state.SetValue(ibProtocol::ibForm::ibState::Caption, field.GetControlTitle());

		const std::vector<ibValueField::ibChoice> choices = field.GetChoices();
		ibDataNode& list = state.Child(ibProtocol::ibForm::ibField::Choices);
		for (const ibValueField::ibChoice& choice : choices)
			list.AddChild(0, 0).SetValue(ibProtocol::ibForm::ibField::Caption, choice.caption);

		// Which one stands in the source — not in the designer, where the field shows no value.
		if (!appData->DesignerMode()) {
			const int selected = field.GetSelectedChoice(choices);
			if (selected != ibNOT_FOUND)
				state.SetValue(ibProtocol::ibForm::ibField::Selected, static_cast<std::int32_t>(selected));
		}

		state.SetValue(ibProtocol::ibForm::ibState::ReadOnly, !field.IsBindingWritable());
	}

	bool OnClientEvent(ibValueField& field, ibProtocol::ibEvent::ibKind event, const ibDataNode& args) const override
	{
		if (event != ibProtocol::ibEvent::ibKind::Change)
			return field.ibValueWindow::OnClientEvent(event, args);
		field.Choose(args.GetValue<std::int32_t>(ibProtocol::ibEvent::Choice));
		return true;
	}
};

} // namespace

const ibFieldKindRole& ibFieldKindRole::Of(ibFieldKind kind)
{
	static const ibFieldKindInput s_input;
	static const ibFieldKindRadioButtons s_radioButtons;
	switch (kind) {
	case ibFieldKind::ibFieldKind_RadioButtons: return s_radioButtons;
	case ibFieldKind::ibFieldKind_Input: break;
	}
	return s_input;
}

//****************************************************************************
//*                                 Field                                    *
//****************************************************************************

ibValueField::ibValueField() : ibValueTextCtrl()
{
}

std::vector<ibValueField::ibChoice> ibValueField::GetChoices() const
{
	std::vector<ibChoice> choices;
	const ibTypeDescription& type = GetTypeDesc();
	const ibMetaData* const metaData = GetMetaData();

	// ITS OWN LIST — each line a value of the field's type, called as it is written.
	const ibString list = m_propertyChoiceList->GetValueAsString();
	if (!list.IsEmpty()) {
		size_t start = 0;
		while (start <= list.Length()) {
			size_t end = list.find(ibT('\n'), start);
			if (end == ibString::npos)
				end = list.Length();
			ibString line = list.substr(start, end - start);
			line.Trim(true).Trim(false);
			if (!line.IsEmpty())
				choices.push_back({ ibValueTypeDescription::AdjustValue(type, ibValue(line), metaData), line });
			start = end + 1;
		}
		return choices;
	}

	// …else its type's, when it holds one type that has choices.
	if (type.GetClsidCount() != 1)
		return choices;
	const ibClassID clsid = type.GetFirstClsid();

	if (clsid == g_valueBooleanCLSID) {
		choices.push_back({ ibValue(true), _("True") });
		choices.push_back({ ibValue(false), _("False") });
		return choices;
	}

	// An enumeration's members, each as the value a field of its type holds — its reference.
	const ibCtorMetaValueType* const ctor = metaData != nullptr ? metaData->GetTypeCtor(clsid) : nullptr;
	const ibValueMetaObjectRecordDataEnumRef* const enumeration = ctor != nullptr
		? dynamic_cast<const ibValueMetaObjectRecordDataEnumRef*>(ctor->GetMetaObject()) : nullptr;
	if (enumeration == nullptr)
		return choices;
	for (const ibValueMetaObjectEnum* member : enumeration->GetEnumObjectArray())
		choices.push_back({ ibValue(ibValueReferenceDataObject::Create(enumeration, member->GetGuid())), member->GetSynonym() });
	return choices;
}

int ibValueField::GetSelectedChoice(const std::vector<ibChoice>& choices) const
{
	ibValue value;
	if (!GetControlValue(value))
		return ibNOT_FOUND;
	for (size_t idx = 0; idx < choices.size(); ++idx) {
		if (choices[idx].value == value)
			return static_cast<int>(idx);
	}
	return ibNOT_FOUND;
}

bool ibValueField::Choose(int index)
{
	if (!IsBindingWritable())
		return false;
	const std::vector<ibChoice> choices = GetChoices();
	if (index < 0 || static_cast<size_t>(index) >= choices.size())
		return false;
	CommitValue(choices[index].value);
	return true;
}

void ibValueField::OnUpdate(ibDataNode& state, ibVisualHost* host)
{
	ibFieldKindRole::Of(GetKind()).OnUpdate(*this, state, host);
}

bool ibValueField::OnClientEvent(ibProtocol::ibEvent::ibKind event, const ibDataNode& args)
{
	return ibFieldKindRole::Of(GetKind()).OnClientEvent(*this, event, args);
}

//*******************************************************************
//*                            Data		                            *
//*******************************************************************

bool ibValueField::ReadData(const ibDataNode& node)
{
	m_propertyKind->SetNodeValue(node.GetProperty(m_propertyKind->GetName()));
	m_propertyChoiceList->SetNodeValue(node.GetProperty(m_propertyChoiceList->GetName()));
	return ibValueTextCtrl::ReadData(node);
}

bool ibValueField::WriteData(ibDataNode& node) const
{
	node.SetProperty(m_propertyKind->GetName(), m_propertyKind->GetNodeValue());
	node.SetProperty(m_propertyChoiceList->GetName(), m_propertyChoiceList->GetNodeValue());
	return ibValueTextCtrl::WriteData(node);
}

//***********************************************************************
//*                       Register in runtime                           *
//***********************************************************************

CONTROL_TYPE_REGISTER(ibValueField, "Field", "Widget", g_controlFieldCLSID);
