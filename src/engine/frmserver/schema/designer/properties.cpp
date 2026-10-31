#include "properties.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"   // the metaobject an edit names by its id
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"

#include "core/exception.h"   // ibCoreException — no such object to read
#include "frmserver/client/clientFrame.h"         // the rows after an edit, shown with its answer
#include "frmserver/client/propertyInspector.h"   // the rows, as the inspector writes them
#include "frmserver/schema/designer/metaData.h"     // the tab's tree, and its object by id — as its doors find them

namespace {

// THE VALUE FROM THE WIRE — Value, as the property's node holds it (the frame's Properties sent it so): a scalar a field,
// a composite (a type description) a node of its own. False: none.
bool ReadValue(const ibDataNode& args, ibDataValue& value)
{
	if (const auto scalar = args.FindField(ibProtocol::ibSchema::ibProperties::Value)) {
		value = *scalar;
		return true;
	}
	if (const auto composite = args.FindChild(ibProtocol::ibSchema::ibProperties::Value)) {
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
	ibDataNode& properties = result.Child(ibProtocol::ibSchema::ibProperties::Properties);

	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	if (metaData == nullptr) {
		if (refusal == ibProtocol::ibRefusal::NoSession)
			return;
		ibCoreException::Error(ibT("%s"), error);
	}
	ibValueMetaObject* const object = ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibProperties::Id, refusal, error);
	if (object == nullptr)
		ibCoreException::Error(ibT("%s"), error);

	// …as the desktop's grid showed it: registered on it, asked what its state hides, its rows written — for this answer.
	ibPropertyInspector inspector;
	inspector.SelectObject(object, args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Tab));
	inspector.OnDraw(properties);
}

std::function<void()> ibSchemaProperties::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	if (static_cast<ibProtocol::ibSchema::ibCommand>(command) != ibProtocol::ibSchema::ibCommand::Save) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("the inspector takes Save alone");
		return nullptr;
	}

	// The metaobject of that id in the tab's tree — or the header above an external file's (its rows the inspector's too).
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibProperties::Id, refusal, error) : nullptr;
	if (object == nullptr)
		return nullptr;

	const ibString name = args.GetValue<ibString>(ibProtocol::ibSchema::ibProperties::Name);
	ibDataValue value;
	if ((object->GetProperty(name) == nullptr && object->GetEvent(name) == nullptr) || !ReadValue(args, value)) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibString::Format(ibT("'%s' takes no such value of '%s'"), object->GetClassName(), name);
		return nullptr;
	}

	const std::int32_t tabId = args.GetValue<std::int32_t>(ibProtocol::ibSchema::ibProperties::Tab);
	return [object, name, value, tabId]() {
		if (ibProperty* const property = object->GetProperty(name))
			ibPropertyGate::SetValue(object, property, value);
		else if (ibEvent* const event = object->GetEvent(name))
			ibPropertyGate::SetEvent(object, event, value);
		if (ibClientFrame* const frame = ibClientFrame::GetFrame())
			frame->ShowProperties(object, tabId);
	};
}
