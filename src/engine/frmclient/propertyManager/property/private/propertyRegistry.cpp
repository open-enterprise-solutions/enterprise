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

wxPGProperty* ibPropertyRegistry::Create(ibBackendProperty* property)
{
	wxPGProperty* result = nullptr;
	const auto found = GetEntries().find(property->GetType());
	if (found != GetEntries().end())
		result = found->second(property);
	if (result == nullptr)
		result = CreateAsString(property);

	result->SetHelpString(property->GetHelp());
	if (!property->IsEditable())
		result->ChangeFlag(wxPGFlags::ReadOnly, true);
	return result;
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
