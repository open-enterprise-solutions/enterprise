////////////////////////////////////////////////////////////////////////////
//	Description : what the base answers on a web server - its web addresses
////////////////////////////////////////////////////////////////////////////
//
// A web server publishes a base under a name and carries its clients there;
// what ELSE the base answers under that name is the base's own list
// (backend/served/servedAddressRegistry.h). These tools are that list's door
// for an assistant - the same door the administration's form goes through:
// they decide nothing the registry does not.
//
////////////////////////////////////////////////////////////////////////////

#include "frmserver/mcp/mcpTool.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/served/servedAddressRegistry.h"

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgKind()
{
	static const ibArg s_a(ibT("kind"), ibArg::Kind::Text,
		ibMcpText("Which code answers there - one of the `kinds` address_list names."), /*required*/ true);
	return s_a;
}

const ibArg& ArgPath()
{
	static const ibArg s_a(ibT("path"), ibArg::Kind::Text,
		ibMcpText("Where, under every publication of this base: segments of letters, digits and . _ ~ -, "
			"no slash at either end, the first not `client` - es/desk is answered at /<publication>/es/desk/."),
		/*required*/ true);
	return s_a;
}

const ibArg& ArgObject()
{
	static const ibArg s_a(ibT("object"), ibArg::Kind::Text,
		ibMcpText("The configuration's object the entry answers for, by its guid - as address_list's `offered` "
			"gives it. Omit for a kind that answers for none."));
	return s_a;
}

const ibArg& ArgId()
{
	static const ibArg s_a(ibT("id"), ibArg::Kind::Text,
		ibMcpText("Which entry - its `id` from address_list."), /*required*/ true);
	return s_a;
}

ibServedAddressRegistry* Registry(ibString& refusal)
{
	ibServedAddressRegistry* const registry = ibApplicationInstance::GetServedAddressRegistry();
	if (registry == nullptr)
		refusal = ibMcpText("The base is not open.");
	return registry;
}

std::shared_ptr<ibDataNode> Describe(const ibServedAddress& address)
{
	auto entry = std::make_shared<ibDataNode>();
	if (address.m_id.isValid())
		entry->SetValue(ibT("id"), address.m_id.str());
	entry->SetValue(ibT("kind"), address.m_kind);
	entry->SetValue(ibT("path"), address.m_path);
	if (address.m_object.isValid())
		entry->SetValue(ibT("object"), address.m_object.str());
	if (address.m_id.isValid())
		entry->SetValue(ibT("kept"), address.IsKept());
	return entry;
}

} // namespace

//---------------------------------------------------------------------------
// address_list
//---------------------------------------------------------------------------
class ibMcpToolAddressList : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("address_list"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("looking at the base's web addresses");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("What this base answers on every web server that publishes it, beside its clients: "
			"each entry's kind, path and object, and whether it is kept in the base (registered by an "
			"administrator) or a session's own (gone with it). Also the kinds this platform knows, and "
			"what each offers to register - what the configuration declares for it - so address_register "
			"can be called with exactly what is offered.");
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibServedAddressRegistry* const registry = Registry(refusal);
		if (registry == nullptr)
			return false;

		std::vector<ibDataValue> addresses;
		for (const ibServedAddress& address : registry->GetServedAddressArray())
			addresses.push_back(ibDataValue::Child(Describe(address)));
		result.AddField(ibT("count"), ibDataValue::Int((std::int64_t)addresses.size()));
		result.AddField(ibT("addresses"), ibDataValue::Array(addresses));

		std::vector<ibDataValue> kinds, offered;
		for (const ibString& name : ibServedKind::GetKindNames()) {
			kinds.push_back(ibDataValue::String(name));
			if (const ibServedKind* const kind = ibServedKind::Find(name)) {
				for (const ibServedAddress& offer : kind->Offer())
					offered.push_back(ibDataValue::Child(Describe(offer)));
			}
		}
		result.AddField(ibT("kinds"), ibDataValue::Array(kinds));
		result.AddField(ibT("offered"), ibDataValue::Array(offered));

		if (addresses.empty())
			result.SetValue(ibT("note"),
				ibMcpText("Nothing is served beside the clients - a web server publishing this base answers only "
				  "its page and /<publication>/client."));
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolAddressList);

//---------------------------------------------------------------------------
// address_register
//---------------------------------------------------------------------------
class ibMcpToolAddressRegister : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("address_register"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("registering the web address '%s'"), ArgPath().Text(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Register a web address of this base, kept in it: every web server that publishes the "
			"base mounts it within seconds, application servers of the same base included. Refused, "
			"with why: a path that is not one or that another entry has, a kind nobody registered, an "
			"entry its kind does not take.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgKind(), ArgPath(), ArgObject() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibServedAddressRegistry* const registry = Registry(refusal);
		if (registry == nullptr)
			return false;

		ibServedAddress address;
		address.m_kind = ArgKind().Text(params);
		address.m_path = ArgPath().Text(params);
		const ibString object = ArgObject().Text(params);
		if (!object.IsEmpty()) {
			address.m_object = ibGuid(object);
			if (!address.m_object.isValid()) {
				refusal = ibString::Format(ibMcpText("'%s' is not a guid."), object);
				return false;
			}
		}
		try {
			address.m_id = registry->Register(address);
		}
		catch (const ibBackendInterruptException&) {
			throw;
		}
		catch (const ibCoreException& err) {
			refusal = err.GetErrorDescription();
			return false;
		}
		result.AddField(ibT("address"), ibDataValue::Child(Describe(address)));
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolAddressRegister);

//---------------------------------------------------------------------------
// address_unregister
//---------------------------------------------------------------------------
class ibMcpToolAddressUnregister : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("address_unregister"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("taking a web address off");
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Take a web address of this base off - its id from address_list. Every web server that "
			"publishes the base stops answering there within seconds.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibServedAddressRegistry* const registry = Registry(refusal);
		if (registry == nullptr)
			return false;

		const ibString text = ArgId().Text(params);
		const ibGuid id(text);
		if (!id.isValid()) {
			refusal = ibString::Format(ibMcpText("'%s' is not an entry's id - address_list gives them."), text);
			return false;
		}
		try {
			registry->Unregister(id);
		}
		catch (const ibBackendInterruptException&) {
			throw;
		}
		catch (const ibCoreException& err) {
			refusal = err.GetErrorDescription();
			return false;
		}
		result.SetValue(ibT("id"), text);
		result.SetValue(ibT("unregistered"), true);
		return true;
	}
};

MCP_TOOL_REGISTER_DESIGNER(ibMcpToolAddressUnregister);
