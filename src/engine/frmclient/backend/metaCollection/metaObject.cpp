#include "metaObject.h"

#include "core/serialize/dataBuilder.h"
#include "frmclient/backend/propertyManager/property/propertyValue.h"
#include "frmclient/backend/serialize/dataProtocol.h"   // ibReadProtocolNode, ibWriteProtocolNode — the rows, the edit
#include "frmclient/mainFrame/mainFrame.h"              // the door to the server

namespace {

// A category under `parent` of that name — none: none is.
ibPropertyCategory* FindCategory(const ibPropertyCategory* parent, const wxString& name)
{
	for (unsigned int index = 0; index < parent->GetCategoryCount(); ++index) {
		ibPropertyCategory* const category = parent->GetCategory(index);
		if (category != nullptr && category->GetName() == name)
			return category;
	}
	return nullptr;
}

} // namespace

wxString ibValueMetaObject::GetName() const
{
	const ibProperty* const property = GetProperty(wxT("Name"));
	return property != nullptr ? property->GetValue().GetString() : wxString();
}

wxString ibValueMetaObject::GetSynonym() const
{
	const ibProperty* const property = GetProperty(wxT("Synonym"));
	return property != nullptr ? property->GetValue().GetString() : wxString();
}

bool ibValueMetaObject::ReadProperty()
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Properties));
	ibProtocolNode args = params.Child(ibProtocolName::Args);
	args.SetValue(ibProtocolName::Id, static_cast<long long>(m_metaId));
	args.SetValue(ibProtocolName::Tab, m_metaData.GetTabId());
	if (!frame->Call(ibProtocolMethod::Schema, params, answer))
		return false;

	ibDataNode rows;
	ibReadProtocolNode(answer.FindChild(ibProtocolName::Properties), rows);
	return ReadProperty(rows);
}

bool ibValueMetaObject::ReadProperty(const ibDataNode& node)
{
	m_shown.clear();
	ReadCategory(m_category, node);
	return true;
}

void ibValueMetaObject::ReadCategory(ibPropertyCategory* category, const ibDataNode& node)
{
	for (const ibDataNode& row : node.Children()) {
		const wxString name = row.GetValue<wxString>(wxT("Name"));

		// A CATEGORY — a row with no class, its rows within.
		if (row.FindField(wxT("Class")) == nullptr) {
			ibPropertyCategory* nextCategory = FindCategory(category, name);
			if (nextCategory == nullptr)
				nextCategory = CreatePropertyCategory(category, name, row.GetValue<wxString>(wxT("Title")));
			ReadCategory(nextCategory, row);
			continue;
		}

		m_shown.insert(name);
		if (ibPropertyValue* const property = dynamic_cast<ibPropertyValue*>(GetProperty(name)))
			property->ReadProperty(row);
		else
			CreateProperty<ibPropertyValue>(category, row);
	}
}

void ibValueMetaObject::OnPropertyRefresh()
{
	for (unsigned int index = 0; index < GetPropertyCount(); ++index) {
		const ibProperty* const property = GetPropertyByIndex(index);
		if (property != nullptr)
			HideProperty(property, m_shown.find(property->GetName()) == m_shown.end());
	}
}

bool ibValueMetaObject::OnPropertyChanging(ibProperty* property, const wxVariant& WXUNUSED(newValue))
{
	ibFrontendMainFrame* const frame = ibFrontendMainFrame::GetFrame();
	if (frame == nullptr)
		return false;

	// …the value as the property's node holds it now: the door (ibPropertyGate) gave it before asking.
	ibDataNode args;
	args.SetValue(wxT("Id"), static_cast<s32>(m_metaId));
	args.SetValue(wxT("Name"), property->GetName());
	args.SetValue(wxT("Tab"), static_cast<s32>(m_metaData.GetTabId()));
	const ibDataValue value = property->GetNodeValue();
	if (value.Kind() == ibDataKind::Child)
		args.SetProperty(wxT("Value"), value);
	else
		args.AddField(wxT("Value"), value);

	ibProtocolNode params, answer;
	params.SetValue(ibProtocolName::Schema, static_cast<long long>(ibProtocolSchema::Properties));
	params.SetValue(ibProtocolName::Command, static_cast<long long>(ibProtocolSchemaCommand::Save));
	ibWriteProtocolNode(args, params.Child(ibProtocolName::Args));
	return frame->Call(ibProtocolMethod::Schema, params, answer);
}

void ibValueMetaObject::OnPropertyChanged(ibProperty* WXUNUSED(property), const wxVariant& WXUNUSED(oldValue),
	const wxVariant& WXUNUSED(newValue))
{
	ReadProperty();
}
