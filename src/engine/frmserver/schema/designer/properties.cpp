#include "properties.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"   // the metaobject an edit names by its id
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include "frmserver/client/clientFrameDesigner.h"   // the inspector the frame holds
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

void ibSchemaProperties::Build(const ibSession& WXUNUSED(session), const ibDataNode& WXUNUSED(args),
	ibDataNode& WXUNUSED(result)) const
{
}

std::function<void()> ibSchemaProperties::Command(const ibSession& session, s32 command,
	const ibDataNode& args, ibProtocolRefusal& refusal, wxString& error) const
{
	if (static_cast<ibProtocolSchemaCommand>(command) != ibProtocolSchemaCommand::Save) {
		refusal = ibProtocolRefusal::BadParameter;
		error = wxT("the inspector takes Save alone");
		return nullptr;
	}

	ibClientFrameDesigner* const frame = dynamic_cast<ibClientFrameDesigner*>(session.GetFrame());
	if (frame == nullptr) {
		refusal = ibProtocolRefusal::NotFound;
		error = wxT("the session has no designer's frame");
		return nullptr;
	}

	// The object the edit was made on — the one shown, through the inspector; …
	ibPropertyInspector& inspector = frame->GetPropertyInspector();
	ibPropertyObject* const shown = inspector.GetSelectedObject();
	const bool isShown = shown != nullptr && inspector.GetSelectedId() == static_cast<ibMetaID>(args.GetValue<s32>(wxT("Id")));

	// …else the metaobject of that id in the tab's tree — the header above an external file's (its rows the inspector's
	// too); a form's control is never anywhere but shown.
	ibPropertyObject* object = shown;
	if (!isShown) {
		ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
		object = metaData != nullptr ? ibSchemaMetaData::FindObject(metaData, args, wxT("Id"), refusal, error) : nullptr;
		if (object == nullptr)
			return nullptr;
	}

	const wxString name = args.GetValue<wxString>(wxT("Name"));
	ibDataValue value;
	if ((object->GetProperty(name) == nullptr && object->GetEvent(name) == nullptr) || !ReadValue(args, value)) {
		refusal = ibProtocolRefusal::BadParameter;
		error = wxString::Format(wxT("'%s' takes no such value of '%s'"), object->GetClassName(), name);
		return nullptr;
	}

	if (isShown)
		return [&inspector, name, value]() { inspector.SetValue(name, value); };
	return [object, name, value]() {
		if (ibProperty* const property = object->GetProperty(name))
			ibPropertyGate::SetValue(object, property, value);
		else if (ibEvent* const event = object->GetEvent(name))
			ibPropertyGate::SetEvent(object, event, value);
	};
}
