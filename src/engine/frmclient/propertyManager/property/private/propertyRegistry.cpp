#include "propertyRegistry.h"

#include "deskgui/univ/propgrid/props.h"   // ibStringProperty — what a property of no registered type is shown by

namespace {

// A PROPERTY OF NO REGISTERED TYPE — a string, read-only, as it reads. A property is never dropped, and never guessed
// from its shape: a type nobody taught the client says what it holds, and shows that it is not done.
ibPGProperty* CreateAsString(ibBackendProperty* property)
{
	ibPGProperty* const shown = new ibStringProperty(property->GetLabel(), property->GetName(),
		property->GetValue().GetString());
	shown->ChangeFlag(ibPGFlags::ReadOnly, true);
	return shown;
}

} // namespace

std::map<wxString, ibPropertyRegistry::ibPropertyMaker>& ibPropertyRegistry::GetEntries()
{
	static std::map<wxString, ibPropertyMaker> s_entries;
	return s_entries;
}

void ibPropertyRegistry::Register(const wxString& type, ibPropertyMaker maker)
{
	wxASSERT_MSG(GetEntries().find(type) == GetEntries().end(),
		wxString::Format(wxT("ibPropertyRegistry: '%s' is registered twice"), type));
	GetEntries()[type] = std::move(maker);
}

ibPGProperty* ibPropertyRegistry::Create(ibBackendProperty* property)
{
	ibPGProperty* result = nullptr;
	const auto found = GetEntries().find(property->GetType());
	if (found != GetEntries().end())
		result = found->second(property);
	if (result == nullptr)
		result = CreateAsString(property);

	result->SetHelpString(property->GetHelp());
	if (!property->IsEditable())
		result->ChangeFlag(ibPGFlags::ReadOnly, true);
	return result;
}

ibPGChoices ibPropertyRegistry::GetChoices(ibBackendProperty* property)
{
	ibPropertyChoiceList list;
	property->GetValueList(list);
	ibPGChoices choices;
	for (unsigned int index = 0; index < list.GetCount(); ++index)
		choices.Add(list.GetLabel(index), static_cast<int>(list.GetId(index)));
	return choices;
}
