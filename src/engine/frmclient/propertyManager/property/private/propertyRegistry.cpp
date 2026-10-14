#include "propertyRegistry.h"

#include <wx/propgrid/props.h>   // wxStringProperty — what a property of no registered type is shown by

namespace {

// A PROPERTY OF NO REGISTERED TYPE — a string, read-only, as it reads. A property is never dropped, and never guessed
// from its shape: a type nobody taught the client says what it holds, and shows that it is not done.
wxPGProperty* CreateAsString(ibBackendProperty* property)
{
	wxPGProperty* const shown = new wxStringProperty(property->GetLabel(), property->GetName(),
		property->GetValue().GetString());
	shown->ChangeFlag(wxPGFlags::ReadOnly, true);
	return shown;
}

// …and its value back, by the kind the grid holds it in.
ibDataValue GetNodeValueByKind(const wxVariant& value)
{
	const wxString type = value.GetType();
	if (type == wxT("bool"))
		return ibDataValue::Bool(value.GetBool());
	if (type == wxT("long"))
		return ibDataValue::Int(value.GetLong());
	if (type == wxT("ulonglong"))
		return ibDataValue::UInt(value.GetULongLong().GetValue());
	if (type == wxT("longlong"))
		return ibDataValue::Int(value.GetLongLong().GetValue());
	return ibDataValue::String(value.GetString());
}

} // namespace

std::map<wxString, ibPropertyRegistry::ibPropertyEntry>& ibPropertyRegistry::GetEntries()
{
	static std::map<wxString, ibPropertyEntry> s_entries;
	return s_entries;
}

void ibPropertyRegistry::Register(const wxString& type, ibPropertyMaker maker, ibPropertyWriter writer)
{
	wxASSERT_MSG(GetEntries().find(type) == GetEntries().end(),
		wxString::Format(wxT("ibPropertyRegistry: '%s' is registered twice"), type));
	GetEntries()[type] = { std::move(maker), std::move(writer) };
}

wxPGProperty* ibPropertyRegistry::Create(ibBackendProperty* property)
{
	wxPGProperty* result = nullptr;
	const auto found = GetEntries().find(property->GetType());
	if (found != GetEntries().end())
		result = found->second.m_maker(property);
	if (result == nullptr)
		result = CreateAsString(property);

	result->SetHelpString(property->GetHelp());
	if (!property->IsEditable())
		result->ChangeFlag(wxPGFlags::ReadOnly, true);
	return result;
}

ibDataValue ibPropertyRegistry::GetNodeValue(const ibBackendProperty* property, const wxVariant& value)
{
	const auto found = GetEntries().find(property->GetType());
	if (found != GetEntries().end() && found->second.m_writer)
		return found->second.m_writer(value);
	return GetNodeValueByKind(value);
}

wxPGChoices ibPropertyRegistry::GetChoices(ibBackendProperty* property)
{
	ibPropertyChoiceList list;
	property->GetValueList(list);
	wxPGChoices choices;
	for (unsigned int index = 0; index < list.GetCount(); ++index)
		choices.Add(list.GetLabel(index), static_cast<int>(list.GetId(index)));
	return choices;
}
