#include "webAddress.h"

#include <algorithm>

#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/served/servedAddressRegistry.h"
#include "backend/session/session.h"
#include "core/serialize/dataBuilder.h"
#include "protocol/protocolDesigner.h"   // ibProtocol::ibSchema::ibWebAddress

namespace {

// The object an entry answers for, by its name in the configuration; empty — none, or not there any more.
ibString ObjectTitle(const ibSession& session, const ibGuid& object)
{
	if (!object.isValid())
		return ibString();
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	if (metaData == nullptr)
		return ibString();
	const ibMetaID id = metaData->MetaIdByGuid(object);
	const ibValueMetaObject* const metaObject = id != wxNOT_FOUND ? metaData->FindAnyObjectByFilter(id) : nullptr;
	return metaObject != nullptr ? metaObject->GetFullName() : object.str();
}

void Describe(const ibSession& session, const ibServedAddress& address, ibDataNode& row)
{
	row.SetValue(ibProtocol::ibSchema::ibWebAddress::Kind, address.m_kind);
	row.SetValue(ibProtocol::ibSchema::ibWebAddress::Path, address.m_path);
	if (address.m_object.isValid()) {
		row.SetValue(ibProtocol::ibSchema::ibWebAddress::Object, address.m_object.str());
		row.SetValue(ibProtocol::ibSchema::ibWebAddress::Title, ObjectTitle(session, address.m_object));
	}
}

} // namespace

bool ibSchemaWebAddress::AccessRight(const ibSession& session) const
{
	const ibMetaDataConfigurationBase* const metaData = session.GetMetaData();
	return metaData != nullptr && metaData->AccessRight_DataAdministration();
}

void ibSchemaWebAddress::Build(const ibSession& session, const ibDataNode& /*args*/, ibDataNode& result) const
{
	ibServedAddressRegistry* const registry = ibApplicationInstance::GetServedAddressRegistry(session.GetApplicationInstance());
	const std::vector<ibServedAddress> addresses = registry != nullptr ? registry->GetServedAddressArray()
		: std::vector<ibServedAddress>();

	ibDataNode& rows = result.Child(ibProtocol::ibSchema::ibWebAddress::Addresses);
	for (std::size_t idx = 0; idx < addresses.size(); idx++) {
		const ibServedAddress& address = addresses[idx];
		ibDataNode& row = rows.AddChild(0, static_cast<ibMetaID>(idx));
		row.SetValue(ibProtocol::ibSchema::ibWebAddress::Id, address.m_id.str());
		Describe(session, address, row);
		row.SetValue(ibProtocol::ibSchema::ibWebAddress::Kept, address.IsKept());
	}

	// What is offered and not registered yet — an entry of that kind for that object, or at that path.
	ibDataNode& offers = result.Child(ibProtocol::ibSchema::ibWebAddress::Offers);
	ibDataNode& kinds = result.Child(ibProtocol::ibSchema::ibWebAddress::Kinds);
	ibMetaID offered = 0, named = 0;
	for (const ibString& name : ibServedKind::GetKindNames()) {
		kinds.AddChild(0, named++).SetValue(ibProtocol::ibSchema::ibWebAddress::Name, name);
		const ibServedKind* const kind = ibServedKind::Find(name);
		if (kind == nullptr)
			continue;
		for (const ibServedAddress& offer : kind->Offer()) {
			const bool registered = std::any_of(addresses.begin(), addresses.end(), [&offer](const ibServedAddress& address) {
				return address.m_path.IsSameAs(offer.m_path, false)
					|| (offer.m_object.isValid() && address.m_kind == offer.m_kind && address.m_object == offer.m_object);
			});
			if (!registered)
				Describe(session, offer, offers.AddChild(0, offered++));
		}
	}
}

std::function<void()> ibSchemaWebAddress::Command(const ibSession& session, std::int32_t command,
	const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const
{
	ibServedAddressRegistry* const registry = ibApplicationInstance::GetServedAddressRegistry(session.GetApplicationInstance());
	if (registry == nullptr) {
		refusal = ibProtocol::ibRefusal::NotNow;
		error = ibT("the base is not open");
		return nullptr;
	}

	// Done in the asking: what the registry refuses is said to the person as it says it.
	const auto done = []() {};
	switch (static_cast<ibProtocol::ibSchema::ibCommand>(command)) {
	case ibProtocol::ibSchema::ibCommand::Create: {
		ibServedAddress address;
		address.m_kind = args.GetValue<ibString>(ibProtocol::ibSchema::ibWebAddress::Kind);
		address.m_path = args.GetValue<ibString>(ibProtocol::ibSchema::ibWebAddress::Path);
		const ibString object = args.GetValue<ibString>(ibProtocol::ibSchema::ibWebAddress::Object);
		if (!object.IsEmpty())
			address.m_object = ibGuid(object);
		try {
			registry->Register(address);
		}
		catch (const ibCoreException& err) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = err.GetErrorDescription();
			return nullptr;
		}
		return done;
	}
	case ibProtocol::ibSchema::ibCommand::Remove: {
		const ibGuid id(args.GetValue<ibString>(ibProtocol::ibSchema::ibWebAddress::Id));
		try {
			registry->Unregister(id);
		}
		catch (const ibCoreException& err) {
			refusal = ibProtocol::ibRefusal::Failed;
			error = err.GetErrorDescription();
			return nullptr;
		}
		return done;
	}
	default:
		break;
	}

	refusal = ibProtocol::ibRefusal::NotFound;
	error = ibString::Format(ibT("web addresses have no command %d"), command);
	return nullptr;
}
