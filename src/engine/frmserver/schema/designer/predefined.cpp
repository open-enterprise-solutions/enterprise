#include "predefined.h"

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/partial/commonObject.h"   // ibValueMetaObjectRecordDataHierarchyMutableRef — what declares them
#include "backend/session/session.h"
#include "core/description/predefinedDescription.h"        // ibPredefinedDescription — the items and what one is entered with
#include "core/serialize/dataBuilder.h"

#include "frmserver/schema/designer/metaData.h"   // ibSchemaMetaData — whose tree, which object
#include "frmserver/win/dlgs/settings/settingsRequest.h"   // ibWriteSettingsClasses — the types a type is picked from, named
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibPredefined

namespace {

typedef ibValueMetaObjectRecordDataHierarchyMutableRef ibPredefinedOwner;

// The object Id names, in Tab's tree — refused, null, when there is no such, or it is one that cannot declare any.
ibPredefinedOwner* FindOwner(const ibSession& session, const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error)
{
	ibMetaData* const metaData = ibSchemaMetaData::FindMetaData(session, args, refusal, error);
	ibValueMetaObject* const object = metaData != nullptr
		? ibSchemaMetaData::FindObject(metaData, args, ibProtocol::ibSchema::ibPredefined::Id, refusal, error) : nullptr;
	if (object == nullptr)
		return nullptr;

	ibPredefinedOwner* const owner = dynamic_cast<ibPredefinedOwner*>(object);
	if (owner == nullptr) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibString::Format(ibT("'%s' cannot have predefined items"), object->GetName());
	}
	return owner;
}

// The item `name` names by its Guid — refused, null, when the owner has none of it.
const ibPredefinedItemDescription* FindItem(const ibPredefinedOwner* owner, const ibDataNode& args, const ibString& name,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	const ibString guid = args.GetValue<ibString>(name);
	const ibPredefinedItemDescription* const item = !guid.IsEmpty() ? owner->FindPredefinedValue(ibGuid(guid)) : nullptr;
	if (item == nullptr) {
		refusal = ibProtocol::ibRefusal::NotFound;
		error = ibString::Format(ibT("'%s' has no predefined item '%s'"), owner->GetName(), guid);
	}
	return item;
}

// WHAT A COMMAND IS GIVEN — an item in its node form (ibPredefinedItemDescriptionMemory: the
// window sends it as it was shown), checked as the dialog checked it: a name, one no other item of the owner has.
// Refused, false, otherwise. Whether the kind's hierarchy holds it there and what it declares is the engine's door to
// say (AppendPredefinedValue / SetPredefinedValue).
bool ReadGiven(const ibPredefinedOwner* owner, const ibDataNode& args, ibPredefinedItemDescription& item,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	if (!ibPredefinedItemDescriptionMemory::ReadNode(ibDataValue::Child(std::make_shared<ibDataNode>(args)), item)) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("the predefined item given cannot be read");
		return false;
	}

	if (item.m_name.IsEmpty()) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibT("a predefined item needs a name");
		return false;
	}

	const ibPredefinedItemDescription* const named = owner->FindPredefinedValue(item.m_name);
	if (named != nullptr && named->m_guid != item.m_guid) {
		refusal = ibProtocol::ibRefusal::BadParameter;
		error = ibString::Format(ibT("'%s' already has a predefined item named '%s'"), owner->GetName(), item.m_name);
		return false;
	}
	return true;
}

} // namespace

bool ibSchemaPredefined::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_Administration();
}

void ibSchemaPredefined::Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const
{
	ibProtocol::ibRefusal refusal = ibProtocol::ibRefusal::None;
	ibString error;
	ibPredefinedOwner* const owner = FindOwner(session, args, refusal, error);
	if (owner == nullptr)
		return;

	result.SetValue(ibProtocol::ibSchema::ibPredefined::Type, ibString::Format(ibT("%llu"), static_cast<unsigned long long>(owner->GetClassType())));

	// ⭐ THE ONE SCHEMA — the items as the configuration keeps them and what an item of this kind is entered with
	// (GetPredefinedDesc); the window is built of it and knows no kind.
	const ibPredefinedDescription& described = owner->GetPredefinedDesc();
	ibDataValue declares;
	ibPredefinedDescriptionMemory::WriteNode(declares, described);
	result.AddField(ibProtocol::ibSchema::ibPredefined::Declares, declares);

	// …and the types a type is picked from, named: the client's picker names them as the server does.
	std::vector<ibTypeDescription> offered;
	for (const ibPredefinedAttributeDescription& attribute : described.m_attributes)
		if (attribute.m_holdsType)
			offered.emplace_back(attribute.m_offered);
	for (const ibPredefinedTableDescription& table : described.m_tables)
		for (const ibPredefinedAttributeDescription& column : table.m_columns)
			if (column.m_holdsType)
				offered.emplace_back(column.m_offered);
	ibWriteSettingsClasses(result.Child(ibProtocol::ibSchema::ibPredefined::Classes), offered, owner->GetMetaData());
}

std::function<void()> ibSchemaPredefined::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	ibPredefinedOwner* const owner = FindOwner(session, args, refusal, error);
	if (owner == nullptr)
		return nullptr;

	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Create: {
		ibPredefinedItemDescription item;
		if (!ReadGiven(owner, args, item, refusal, error))
			return nullptr;
		// Its fields and what it declares, through the one door: made whole, or refused and nothing made.
		return [owner, item]() { owner->AppendPredefinedValue(item); };
	}
	case ibProtocol::ibSchema::ibCommand::Set: {
		// A Guid the owner has not — one more item of it, as the engine's SetPredefinedValue makes it (the dialog's Copy).
		ibPredefinedItemDescription item;
		if (!ReadGiven(owner, args, item, refusal, error))
			return nullptr;
		if (!item.m_guid.isValid()) {
			refusal = ibProtocol::ibRefusal::BadParameter;
			error = ibT("which predefined item - its Guid");
			return nullptr;
		}
		// Its fields and what it declares, through the one door: changed whole, or refused and the item as it was.
		return [owner, item]() { owner->SetPredefinedValue(item); };
	}
	case ibProtocol::ibSchema::ibCommand::Remove: {
		const ibPredefinedItemDescription* const item = FindItem(owner, args, ibProtocol::ibSchema::ibPredefined::Guid, refusal, error);
		if (item == nullptr)
			return nullptr;
		const ibGuid guid = item->m_guid;
		return [owner, guid]() { owner->DeletePredefinedValue(guid); };
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibString::Format(ibT("the predefined values editor has no command %d"), command);
	return nullptr;
}
