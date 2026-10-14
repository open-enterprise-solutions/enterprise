#include "propertyRegistry.h"

#include <wx/propgrid/props.h>   // wxEnumProperty, wxBoolProperty — what a row of no registered class is shown by

#include "core/typeName.h"   // ibTypeInfoName — a local property's class, named as the server names one
#include "frmclient/backend/propertyManager/propertyObject.h"

namespace {

// A ROW OF NO REGISTERED CLASS, shown by its shape — its choices a list, its value by its kind; failing that, read-only,
// as it reads (Presentation). A row is never dropped: a class nobody taught the client still says what it holds.
wxPGProperty* CreateByShape(const ibDataNode& row)
{
	const wxString label = ibPropertyRegistry::GetLabel(row), name = ibPropertyRegistry::GetName(row);
	const ibDataValue value = ibPropertyRegistry::GetValue(row);

	wxPGChoices choices = ibPropertyRegistry::GetChoices(row);
	if (choices.GetCount() > 0 && value.Kind() == ibDataKind::Number)
		return new wxEnumProperty(label, name, choices, static_cast<int>(value.AsInt()));

	switch (value.Kind()) {
	case ibDataKind::Bool:
		return new wxBoolProperty(label, name, value.AsBool());
	case ibDataKind::Number:
		return new wxIntProperty(label, name, static_cast<long>(value.AsInt()));
	case ibDataKind::String:
		return new wxStringProperty(label, name, value.AsString());
	default:
		break;
	}

	wxPGProperty* const shown = new wxStringProperty(label, name, row.GetValue<wxString>(wxT("Presentation")));
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

void ibPropertyRegistry::Register(const wxString& className, ibPropertyMaker maker, ibPropertyWriter writer)
{
	wxASSERT_MSG(GetEntries().find(className) == GetEntries().end(),
		wxString::Format(wxT("ibPropertyRegistry: '%s' is registered twice"), className));
	GetEntries()[className] = { std::move(maker), std::move(writer) };
}

wxPGProperty* ibPropertyRegistry::Create(const ibDataNode& row)
{
	const wxString className = GetClass(row);
	if (className.IsEmpty())
		return nullptr;   // a category

	wxPGProperty* result = nullptr;
	const auto found = GetEntries().find(className);
	if (found != GetEntries().end())
		result = found->second.m_maker(row);
	if (result == nullptr)
		result = CreateByShape(row);

	result->SetHelpString(row.GetValue<wxString>(wxT("Help")));
	if (row.GetValue<bool>(wxT("ReadOnly")))
		result->ChangeFlag(wxPGFlags::ReadOnly, true);
	return result;
}

ibDataValue ibPropertyRegistry::GetNodeValue(const ibDataNode& row, const wxVariant& value)
{
	const auto found = GetEntries().find(GetClass(row));
	if (found != GetEntries().end() && found->second.m_writer)
		return found->second.m_writer(value);
	return GetNodeValueByKind(value);
}

ibDataNode& ibPropertyRegistry::AppendProperty(ibDataNode& parent, ibBackendProperty* property)
{
	ibDataNode& row = parent.AddChild(0, static_cast<ibMetaID>(parent.Children().size() + 1));
	row.SetValue(wxT("Name"), property->GetName());
	row.SetValue(wxT("Title"), property->GetLabel());
	if (!property->GetHelp().IsEmpty())
		row.SetValue(wxT("Help"), property->GetHelp());
	row.SetValue(wxT("Class"), ibTypeInfoName(typeid(*property)));

	// …as it is stored: a scalar a field, a composite a node of its own.
	const ibDataValue value = property->GetNodeValue();
	if (value.Kind() == ibDataKind::Child)
		row.SetProperty(wxT("Value"), value);
	else
		row.AddField(wxT("Value"), value);
	row.SetValue(wxT("Presentation"), property->GetValue().MakeString());

	ibPropertyChoiceList choices;
	if (property->GetValueList(choices) == ibPropertyChoiceMode::Mult)
		row.SetValue(wxT("Multiple"), true);
	for (unsigned int index = 0; index < choices.GetCount(); ++index) {
		ibDataNode& choice = row.AddChild(0, static_cast<ibMetaID>(index + 1));
		choice.SetValue(wxT("Id"), static_cast<s32>(choices.GetId(index)));
		choice.SetValue(wxT("Title"), choices.GetLabel(index));
	}

	if (!property->IsEditable())
		row.SetValue(wxT("ReadOnly"), true);
	return row;
}

wxString ibPropertyRegistry::GetName(const ibDataNode& row)
{
	return row.GetValue<wxString>(wxT("Name"));
}

wxString ibPropertyRegistry::GetLabel(const ibDataNode& row)
{
	const wxString title = row.GetValue<wxString>(wxT("Title"));
	return title.IsEmpty() ? GetName(row) : title;
}

wxString ibPropertyRegistry::GetClass(const ibDataNode& row)
{
	return row.GetValue<wxString>(wxT("Class"));
}

ibDataValue ibPropertyRegistry::GetValue(const ibDataNode& row)
{
	// …as the server wrote it (ibPropertyInspector): a scalar a field, a composite a node.
	if (const ibDataValue* const scalar = row.FindField(wxT("Value")))
		return *scalar;
	return row.GetProperty(wxT("Value"));
}

wxPGChoices ibPropertyRegistry::GetChoices(const ibDataNode& row)
{
	wxPGChoices choices;
	for (const ibDataNode& choice : row.Children()) {
		if (choice.FindField(wxT("Id")) == nullptr)
			continue;   // a type the row may hold (Type), not a choice
		choices.Add(choice.GetValue<wxString>(wxT("Title")), static_cast<int>(choice.GetValue<s32>(wxT("Id"))));
	}
	return choices;
}
