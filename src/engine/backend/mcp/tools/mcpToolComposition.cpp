////////////////////////////////////////////////////////////////////////////
//	Description : what an object is checked INTO - a common attribute's composition
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE OTHER TICK-BOX. A section's editor paints one column of checkboxes over the tree and the
// OBJECT carries the set of section ids (`section_include`, mcpToolSection.cpp). A common
// attribute's editor paints exactly the same picture - and the object carries a DIFFERENT set,
// `ibCompositionObject::m_compositions` (backend/compositionHelper.h), because "which sections am I
// in" and "whose column do I carry" have to stay tellable apart.
//
// ⭐ THE MECHANISM WAS THERE AND HAD NO DOOR - `SetComposition` / `IsInComposition` /
// `GetCompositions` / `IsCompositionAllowed` have existed since common attributes landed
// (2026-08-06). Nothing here decides anything: it asks and it sets.
//
// 🛑 AND WITHOUT IT THE ASSISTANT COULD DECLARE A COMMON ATTRIBUTE AND NOT MAKE IT DO ANYTHING.
// Measured 2026-09-09 while assembling data separation over this wire: the declaration was created
// and typed in two calls, and then there was no way to check a single object into it - the one step
// that turns a declaration into a column. A separator nothing carries is not a half-built feature,
// it is an INVISIBLE one: everything compiles, restructures and runs, and the separation quietly
// does not happen. Max, the same day: the assistant has to be able to set the membership for roles,
// for common attributes and for sections alike.
//
// ⚠ THREE EDITORS, THREE MECHANISMS, ONE VOCABULARY - which is why this is a verb of its own rather
// than a flag on another. Sections store membership in `ibInterfaceObject`, a common attribute's
// composition in `ibCompositionObject`, a role's rights in the role table on `ibAccessObject`.
// compositionHelper.h states the reason for keeping the first two apart in so many words: one
// container holding two meanings could no longer answer "checked in AS WHAT". Fusing the three
// behind one verb would move that ambiguity from the store to the door. They are spelled alike on
// purpose - `section_include`, `metadata_include`, `role_grant` - so a caller learns one shape and
// uses three.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/compositionHelper.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"

namespace {

// The short spelling every tool file here uses for one declared argument.
using ibArg = ibMcpTool::ibMcpArgument;

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The object to check in, as NodeId - the CARRIER, not the declaration. "
			  "metadata_list and metadata_tree give it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgInto()
{
	static const ibArg s_a(wxT("into"), ibArg::Kind::Whole,
		ibMcpText("The common attribute's declaration, as NodeId. Omit it to be TOLD instead: the "
			  "answer is what this object is already checked into."));
	return s_a;
}

const ibArg& ArgRemove()
{
	static const ibArg s_a(wxT("remove"), ibArg::Kind::Flag,
		ibMcpText("Take it OUT of that composition instead of putting it in. The column and the data "
			  "in it go at the next apply, which is a restructuring - so this is the direction worth "
			  "being sure about."));
	return s_a;
}

class ibMcpToolMetadataInclude : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_include"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return ibMcpText("checking an object into a composition");
	}

	wxString GetDescription() const override
	{
		return ibMcpText("CHECK AN OBJECT INTO A COMMON ATTRIBUTE'S COMPOSITION - the step that turns a "
			"declaration into a real column on that object. A common attribute declares a name and a "
			"type; until something is checked in it carries nothing and changes nothing, and every "
			"answer about the configuration looks exactly as it would if the feature worked. "
			"THE SAME PICTURE the designer paints as a tick-box tree, and the same one section_include "
			"paints for sections - a different store, because 'which sections am I in' and 'whose "
			"column do I carry' have to stay tellable apart. Role rights are the third of the family "
			"and are role_grant. WHO MAY BE CHECKED IN is asked of the object and never listed here, "
			"so a metatype that arrives later answers for itself: today the catalog / document line "
			"says yes and registers say no. Omit `into` to READ what this object already carries.");
	}

	wxString GetSearchText() const override
	{
		return ibMcpText("composition common attribute check in membership separator data separation "
			"tick box carrier declaration");
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
			refusal = ibMcpText("No configuration is open, so there is nothing to check anything into.");
			return false;
		}

		const ibMetaID carrierId = (ibMetaID)ArgId().Whole(params);
		ibValueMetaObject* const carrier = ibFindMetaObjectById(metaData, carrierId);

		if (carrier == nullptr) {
			refusal = wxString::Format(
				ibMcpText("Nothing in this configuration has the id %d."), (int)carrierId);
			return false;
		}

		// ⭐ ASKED OF THE OBJECT, NOT OF A LIST. `IsCompositionAllowed` is the mechanism's own
		// question; answering it here from a table of metatypes would be the hand-kept list the
		// mechanism exists to avoid - and the one that goes stale the day a metatype is added.
		ibCompositionObject* const composition = dynamic_cast<ibCompositionObject*>(carrier);

		if (composition == nullptr || !composition->IsCompositionAllowed()) {
			refusal = wxString::Format(
				ibMcpText("'%s' cannot be part of a composition - it says so itself. A common attribute "
					  "needs somewhere to put the column it produces, so the catalog / document "
					  "line accepts one and a register does not."), carrier->GetName());
			return false;
		}

		result.SetValue(wxT("object"), carrier->GetName());

		// READING - no `into` given. What this object already carries, by declaration, with the
		// names beside the ids: an id alone would send the caller back for a second call.
		if (params.FindField(ArgInto().Name()) == nullptr) {
			std::vector<ibDataValue> in;
			for (const ibMetaID& id : composition->GetCompositions()) {
				ibValueMetaObject* const declaration = ibFindMetaObjectById(metaData, id);
				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				entry->AddField(wxT("id"), ibDataValue::Int((s64)id));
				entry->SetValue(wxT("name"),
					declaration != nullptr ? declaration->GetName() : wxString(wxT("?")));
				in.push_back(ibDataValue::Child(entry));
			}
			result.AddField(wxT("in"), ibDataValue::Array(in));
			return true;
		}

		const ibMetaID declarationId = (ibMetaID)ArgInto().Whole(params);
		ibValueMetaObject* const declaration = ibFindMetaObjectById(metaData, declarationId);

		if (declaration == nullptr) {
			refusal = wxString::Format(
				ibMcpText("Nothing in this configuration has the id %d."), (int)declarationId);
			return false;
		}

		const bool remove = ArgRemove().Flag(params);
		composition->SetComposition(declarationId, !remove);

		result.SetValue(wxT("declaration"), declaration->GetName());
		result.SetValue(wxT("included"), !remove);
		// ⚠ SAID OUT LOUD, because the two states read alike from here: the column does not exist
		// until the configuration is applied. A caller that stops at this answer has changed the
		// configuration and not the database.
		result.SetValue(wxT("next"), ibMcpText("The column appears when the configuration is applied - "
			"config_apply. Until then this is a declaration the database has not been told about."));
		return true;
	}
};

} // namespace

MCP_TOOL_REGISTER(ibMcpToolMetadataInclude);
