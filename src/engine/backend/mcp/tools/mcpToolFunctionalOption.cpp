////////////////////////////////////////////////////////////////////////////
//	Description : what an object belongs to - a functional option's members
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE FOURTH TICK-BOX. A section's editor, a common attribute's and a role's paint a checkable tree over
// the configuration, and each mechanism keeps its own set (mcpToolComposition.cpp says why the stores stay
// apart). A functional option's editor paints the same tree — objects, and the fields and tables inside
// them — and the MEMBER carries the set, `ibFunctionalOptionObject::m_functionalOptions`
// (backend/functionalOptionHelper.h). Spelled like the others, `option_include`, so a caller learns one
// shape and uses four.
//
// Nothing here decides anything: it asks the object whether it may belong (IsFunctionalOptionAllowed —
// the question the designer's tree asks too) and sets.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"

namespace {

// The short spelling every tool file here uses for one declared argument.
using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The object, field or table that belongs, as NodeId - the MEMBER, not the option. "
			  "metadata_list and metadata_tree give it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgInto()
{
	static const ibArg s_a(wxT("into"), ibArg::Kind::Whole,
		ibMcpText("The functional option, as NodeId. Omit it to be TOLD instead: the answer is which "
			  "options this object already belongs to."));
	return s_a;
}

const ibArg& ArgRemove()
{
	static const ibArg s_a(wxT("remove"), ibArg::Kind::Flag,
		ibMcpText("Take it OUT of that option instead of putting it in."));
	return s_a;
}

class ibMcpToolOptionInclude : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("option_include"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("putting an object into a functional option");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("PUT AN OBJECT, A FIELD OR A TABLE INTO A FUNCTIONAL OPTION - what is not shown while "
			"the option is off: in forms, lists, sections, command bars and All functions. It stays in the "
			"metadata, in the data and in every query; only the interface does not offer it. THE SAME "
			"PICTURE the designer paints as a tick-box tree when the option is opened, and the same one "
			"section_include and metadata_include paint for sections and common attributes - a different "
			"store for each. WHO MAY BELONG is asked of the object: what the command interface offers, "
			"and the fields and tables a form binds to; an option itself never does (how options depend "
			"on each other is their `Requires`). Omit `into` to READ what this object already belongs to.");
	}

	wxString GetSearchText() const override
	{
		return ibMcpText("functional option member membership hide visibility part of the system not used "
			"tick box include");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgInto(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaDataConfigurationBase* metaData = activeMetaData;

		if (metaData == nullptr || !metaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open, so there is nothing to put anything into.");
			return false;
		}

		const ibMetaID memberId = (ibMetaID)ArgId().Whole(params);
		ibValueMetaObject* const member = ibFindMetaObjectById(metaData, memberId);

		if (member == nullptr) {
			refusal = wxString::Format(
				ibMcpText("Nothing in this configuration has the id %d."), (int)memberId);
			return false;
		}

		// ⭐ ASKED OF THE OBJECT, NOT OF A LIST — the same question the designer's tree asks.
		if (!member->IsFunctionalOptionAllowed()) {
			refusal = wxString::Format(
				ibMcpText("'%s' cannot belong to a functional option - it says so itself. What may is what "
					  "the interface shows: an object the command interface offers, a field, a table."),
				member->GetName());
			return false;
		}

		result.SetValue(wxT("object"), member->GetName());

		// READING - no `into` given. Which options this object belongs to, with the names beside the ids.
		if (params.FindField(ArgInto().Name()) == nullptr) {
			std::vector<ibDataValue> in;
			for (const ibMetaID& id : member->GetFunctionalOptions()) {
				ibValueMetaObject* const option = ibFindMetaObjectById(metaData, id);
				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				entry->AddField(wxT("id"), ibDataValue::Int((s64)id));
				entry->SetValue(wxT("name"), option != nullptr ? option->GetName() : wxString(wxT("?")));
				in.push_back(ibDataValue::Child(entry));
			}
			result.AddField(wxT("in"), ibDataValue::Array(in));
			return true;
		}

		const ibMetaID optionId = (ibMetaID)ArgInto().Whole(params);
		ibValueMetaObject* const option = ibFindMetaObjectById(metaData, optionId);

		if (option == nullptr || option->GetClassType() != g_metaFunctionalOptionCLSID) {
			refusal = wxString::Format(
				ibMcpText("%d is not a functional option of this configuration."), (int)optionId);
			return false;
		}

		const bool remove = ArgRemove().Flag(params);
		member->SetFunctionalOption(optionId, !remove);

		result.SetValue(wxT("option"), option->GetName());
		result.SetValue(wxT("included"), !remove);
		return true;
	}
};

} // namespace

MCP_TOOL_REGISTER(ibMcpToolOptionInclude);
