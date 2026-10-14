#include "properties.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"   // the metaobject an edit names by its id
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include "core/exception.h"   // ibCoreException — no such object to read
#include "frmserver/client/propertyInspector.h"   // the rows, as the inspector writes them
#include "frmserver/schema/designer/metaData.h"     // the tab's tree, and its object by id — as its doors find them

namespace {

// THE VALUE FROM THE WIRE — Value, as the property's node holds it (the frame's Properties sent it so): a scalar a field,
// a composite (a type description) a node of its own. False: none.
bool ReadValue(const ibDataNode& args, ibDataValue& value)
{
	if (const ibDataValue* const scalar = args.FindField(wxT("Value"))) {
		value = *scalar;
		return true;
	}
	if (const ibDataNode* const composite = args.FindChild(wxT("Value"))) {
		value = ibDataValue::Child(std::make_shared<ibDataNode>(*composite));
		return true;
	}
	return false;
}

} // namespace

bool ibSchemaProperties::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaProperties::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	ibDataNode& properties = result.Child(wxT("Properties"));

	ibProtocolRefusal refusal = ibProtocolRefusal::None;
	wxString error;
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	if (metaData == nullptr) {
		if (refusal == ibProtocolRefusal::NoSession)
			return;
		ibCoreException::Error(wxT("%s"), error);
	}
	ibValueMetaObject* const object = ibSchemaMetaData::FindObject(metaData, args, wxT("Id"), refusal, error);
	if (object == nullptr)
		ibCoreException::Error(wxT("%s"), error);

	// …as the desktop's grid showed it: registered on it, asked what its state hides, its rows written — for this answer.
	ibPropertyInspector inspector;
	inspector.SelectObject(object, args.GetValue<s32>(wxT("Tab")));
	inspector.OnDraw(properties);
}

std::function<void()> ibSchemaProperties::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	if (static_cast<ibProtocolSchemaCommand>(command) != ibProtocolSchemaCommand::Save) {
		refusal = ibProtocolRefusal::BadParameter;
		error = wxT("the inspector takes Save alone");
		return nullptr;
	}

	// The metaobject of that id in the tab's tree — or the header above an external file's (its rows the inspector's too).
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, wxT("Id"), refusal, error) : nullptr;
	if (object == nullptr)
		return nullptr;

	const wxString name = args.GetValue<wxString>(wxT("Name"));
	ibDataValue value;
	if ((object->GetProperty(name) == nullptr && object->GetEvent(name) == nullptr) || !ReadValue(args, value)) {
		refusal = ibProtocolRefusal::BadParameter;
		error = wxString::Format(wxT("'%s' takes no such value of '%s'"), object->GetClassName(), name);
		return nullptr;
	}

	return [object, name, value]() {
		if (ibProperty* const property = object->GetProperty(name))
			ibPropertyGate::SetValue(object, property, value);
		else if (ibEvent* const event = object->GetEvent(name))
			ibPropertyGate::SetEvent(object, event, value);
	};
}
