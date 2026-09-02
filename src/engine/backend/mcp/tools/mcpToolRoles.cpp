////////////////////////////////////////////////////////////////////////////
//	Description : what a role actually permits
////////////////////////////////////////////////////////////////////////////
//
// THE QUESTION THIS EXISTS FOR: "why can this person not see that?" Knowing WHO
// they are is half of it - user_list answers that, with the roles they hold -
// and the other half is what those roles let through.
//
// THE RIGHT IS ASKED OF THE OBJECT, NOT OF THE ROLE. That is the shape the role
// editor is built on and it is the right way round: an object declares the
// rights that are meaningful for it - a catalog has Read and Insert, a section
// has visibility - and a role is only a column in that table. So there is no
// list of "everything a role permits" to fetch; there is a walk over the objects
// that have rights, asking each one about this role.
//
// AN OBJECT WITH NO RIGHTS IS SKIPPED, exactly as the editor skips it: it has
// nothing to grant or deny, and listing it would pad the answer with objects the
// question can never be about.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaSectionObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/roleHelper.h"

#include <functional>

namespace {

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgDifferingOnly()
{
	static const ibArg s_a(wxT("differingOnly"), ibArg::Kind::Flag,
		ibMcpText("Report only rights the named roles DISAGREE about - one grants, another refuses. "
		  "With several roles this is the comparison itself, and it is usually short."));
	return s_a;
}

const ibArg& ArgRole()
{
	static const ibArg s_a(wxT("role"), ibArg::Kind::Text,
		ibMcpText("One role's name, as it is called in the configuration."));
	return s_a;
}

const ibArg& ArgRoles()
{
	static const ibArg s_a(wxT("roles"), ibArg::Kind::Many,
		ibMcpText("Several role names, to compare them side by side. Omit both this and `role` to "
			  "take every role there is - which is how you ask who can reach something."));
	return s_a;
}

const ibArg& ArgObject()
{
	static const ibArg s_a(wxT("object"), ibArg::Kind::Text,
		ibMcpText("One object by name - a catalog, a document, a section, or 'Configuration' for the "
			  "root, where the administration rights live. Omit for all of them."));
	return s_a;
}

const ibArg& ArgDeniedOnly()
{
	static const ibArg s_a(wxT("deniedOnly"), ibArg::Kind::Flag,
		ibMcpText("Report only rights that SOMEBODY is refused. Usually the shorter half, and the "
			  "half a complaint is about."));
	return s_a;
}

const ibArg& ArgValue()
{
	static const ibArg s_a(wxT("value"), ibArg::Kind::Flag,
		ibMcpText("true grants, false denies. REQUIRED - there is no safe default for a rights "
			  "change."), /*required*/ true);
	return s_a;
}

const ibArg& ArgRights()
{
	static const ibArg s_a(wxT("rights"), ibArg::Kind::Many,
		ibMcpText("Which rights, by the names metadata_rights reports (Read, Write, Delete, Use, "
			  "Administration...). Omit for all of them. A name an object does not declare is "
			  "simply not set on it - objects declare different rights."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// role_rights
//---------------------------------------------------------------------------
class ibMcpToolRoleRights : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("role_rights"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString role = ArgRole().Text(params);

		return role.IsEmpty()
			? ibMcpText("checking who has access to what")
			: wxString::Format(ibMcpText("checking what the role '%s' permits"), role);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("WHO HAS ACCESS, right by right. Every right an object declares is answered with "
			"the roles that grant it and the roles that refuse it, so comparing roles IS the "
			"answer rather than something to work out from two separate readings. Name one role "
			"to ask about that one, several to compare them, or none to ask about every role in "
			"the configuration. Ask it when somebody cannot see or change something: user_list "
			"says which roles they hold, and this says what those roles let through.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgRole(), ArgRoles(), ArgObject(), ArgDeniedOnly(), ArgDifferingOnly() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open.");
			return false;
		}

		// ⭐ THE ROLES BEING COMPARED, RESOLVED ONCE. One name, several names, or none at all —
		// and "none" means every role there is, because "who can reach this" is the same question
		// asked of everybody rather than a different question needing a different tool.
		//
		// Each is carried as the pair it is used as: the NAME, which is what an answer says, and
		// the ibRoleUserInfo, which is what the rights fold takes. Building the second from the
		// first inside the walk would rebuild it once per object per right.
		struct Asked {
			wxString              name;
			ibRoleCompositionMode mode;
			ibRoleUserInfo        as;
		};

		std::vector<Asked> asked;

		// 🛑 A ROLE CARRIES ITS OWN COMPOSITION, AND THE SHORT CTOR DOES NOT ASK IT.
		// ibRoleUserInfo(id) stamps Union, so a RESTRICTING role read through it would be folded
		// as a permitting one — and a restricting role grants nothing by construction (it only
		// subtracts, roleHelper.h). That reads as "this role allows everything", which is the
		// exact opposite of what it is for. The entry is built by hand so the mode comes off the
		// role itself.
		const auto describeRole = [](ibValueMetaObject* roleObject) {

			Asked one;
			one.name = roleObject->GetName();
			one.mode = roleObject->GetRoleCompositionMode();
			one.as.m_arrayRole.emplace_back(roleObject->GetMetaID(), one.mode);

			return one;
		};

		const auto addRole = [&](const wxString& name) -> bool {

			ibValueMetaObject* roleObject =
				ibFindMetaObject(activeMetaData, wxT("Role"), name);

			if (roleObject == nullptr) {
				refusal = wxString::Format(
					ibMcpText("This configuration has no role named '%s'."), name);
				return false;
			}

			asked.push_back(describeRole(roleObject));
			return true;
		};

		const wxString roleName = ArgRole().Text(params);

		if (!roleName.IsEmpty() && !addRole(roleName))
			return false;

		if (const ibDataValue* several = params.FindField(ArgRoles().Name()))
			if (several->Kind() == ibDataKind::Array)
				for (const ibDataValue& one : several->AsArray())
					if (one.Kind() == ibDataKind::String && !addRole(one.AsString()))
						return false;

		if (asked.empty())
			for (ibValueMetaObject* object :
				activeMetaData->GetAnyArrayObject(g_metaRoleCLSID))
				if (object != nullptr && !object->IsDeleted())
					asked.push_back(describeRole(object));

		if (asked.empty()) {
			refusal = ibMcpText("This configuration declares no roles, so nothing is governed by them - "
				"everything falls back to each right's own default. metadata_rights says what "
				"those defaults are.");
			return false;
		}

		const wxString only = ArgObject().Text(params);
		const bool deniedOnly = ArgDeniedOnly().Flag(params);
		const bool differingOnly = ArgDifferingOnly().Flag(params);

		std::vector<ibDataValue> objects;

		// A SECTION NESTS, and a sub-section bears rights of its own. Walking only
		// the top level would report a section as permitted while everything inside
		// it stayed shut — which is exactly the complaint this tool answers, given
		// the wrong answer. The editor recurses; so does this.
		std::function<void(ibValueMetaObject*, const wxString&)> consider =
			[&](ibValueMetaObject* object, const wxString& path) {

			if (object == nullptr || object->IsDeleted())
				return;

			const wxString here = path.IsEmpty()
				? object->GetName()
				: path + wxT(".") + object->GetName();

			// No rights declared, nothing any role could be asked about. A section
			// with none still has children that may have some.
			if (object->GetRoleCount() > 0
				&& (only.IsEmpty()
					|| stringUtils::CompareString(only, object->GetName())
					|| stringUtils::CompareString(only, here))) {

				std::vector<ibDataValue> rights;
				bool anyDenied = false;
				bool anyDiffering = false;

				for (unsigned int index = 0; index < object->GetRoleCount(); ++index) {

					const ibRole* right = object->GetRole(index);
					if (right == nullptr)
						continue;

					// ⭐ THE RIGHT IS THE ROW, THE ROLES ARE THE ANSWER. Asked this way round
					// because the question is "who can do this" — a shape that has to be worked
					// out from two separate readings is not an answer to it, and comparing roles
					// by eye across lists is exactly where a wrong conclusion comes from.
					std::vector<ibDataValue> grantedBy;
					std::vector<ibDataValue> deniedBy;

					for (const Asked& who : asked) {
						if (object->AccessRight(right, who.as))
							grantedBy.push_back(ibDataValue::String(who.name));
						else
							deniedBy.push_back(ibDataValue::String(who.name));
					}

					// THEY DISAGREE HERE — the whole point of naming more than one, said as a
					// fact rather than left to be inferred from two array lengths.
					const bool differs = !grantedBy.empty() && !deniedBy.empty();

					anyDenied = anyDenied || !deniedBy.empty();
					anyDiffering = anyDiffering || differs;

					if (deniedOnly && deniedBy.empty())
						continue;
					if (differingOnly && !differs)
						continue;

					std::shared_ptr<ibDataNode> row = std::make_shared<ibDataNode>();
					row->SetValue(wxT("right"), right->GetName());
					row->AddField(wxT("grantedBy"), ibDataValue::Array(grantedBy));
					row->AddField(wxT("deniedBy"), ibDataValue::Array(deniedBy));

					if (differs)
						row->AddField(wxT("differs"), ibDataValue::Bool(true));

					// ⚠ WHETHER ANYBODY ACTUALLY SET THIS is deliberately NOT answered here:
					// ibAccessObject keeps the map private and only folds it, so the question
					// cannot be asked without widening a header the whole tree includes.
					// metadata_rights carries the default instead, which is the same fact from
					// the side that owns it — a right nobody flipped answers with `whenUnset`.
					rights.push_back(ibDataValue::Child(row));
				}

				const bool worthReporting = !rights.empty()
					&& (!deniedOnly || anyDenied)
					&& (!differingOnly || anyDiffering);

				if (worthReporting) {

					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					entry->SetValue(wxT("object"), here);
					entry->SetValue(wxT("kind"), object->GetClassName());
					entry->AddField(wxT("id"), ibDataValue::Int((s64)object->GetMetaID()));

					if (anyDiffering)
						entry->AddField(wxT("differs"), ibDataValue::Bool(true));

					entry->AddField(wxT("rights"), ibDataValue::Array(rights));

					objects.push_back(ibDataValue::Child(entry));
				}
			}

			// ⭐ THE CLSID IS THE ANSWER. It was asked first and then asked again as a cast, which
			// cannot say anything the first test did not: an exact class id is an exact type. Two
			// spellings of one question, and the second one carries a null branch that is dead.
			if (object->GetClassType() != g_metaSectionCLSID)
				return;

			ibValueMetaObjectSection* section = static_cast<ibValueMetaObjectSection*>(object);

			for (ibValueMetaObjectSection* child : section->GetInterfaceArrayObject())
				consider(child, here);
		};

		// ⭐ THE ROOT COUNTS, AND IT IS NOT IN THE ARRAY. Administration, Update database
		// configuration, Active users and Exclusive mode are declared on the CONFIGURATION object,
		// so a walk over the metaobject array alone answers "this role denies nothing" about
		// precisely the rights an access question is usually about.
		consider(activeMetaData->GetCommonMetaObject(), wxEmptyString);

		for (ibValueMetaObject* object : activeMetaData->GetAnyArrayObject())
			consider(object, wxEmptyString);

		// ⭐ WHO WAS ASKED, AND WHAT KIND OF ROLE EACH IS — the classification, not just the name.
		// The two modes are not two flavours of the same thing: a PERMITTING role adds (any one
		// granting is enough) and a RESTRICTING role only subtracts, granting nothing on its own.
		// So the same word in `grantedBy` means "allows" for one and "does not object" for the
		// other, and a reader given only the names would draw the wrong conclusion from the right
		// data.
		std::vector<ibDataValue> who;
		bool anyRestricting = false;

		for (const Asked& one : asked) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->SetValue(wxT("role"), one.name);
			// ⚠ wxString, not the ternary's `const wchar_t*` — ibDataCodec has no specialisation
			// for a raw pointer, and the failure is a template error inside dataBuilder.h rather
			// than anything pointing here.
			entry->SetValue(wxT("mode"), wxString(one.mode == ibRoleCompositionMode_Intersection
				? wxT("restricting") : wxT("permitting")));

			who.push_back(ibDataValue::Child(entry));

			anyRestricting = anyRestricting
				|| one.mode == ibRoleCompositionMode_Intersection;
		}

		result.AddField(wxT("roles"), ibDataValue::Array(who));
		result.AddField(wxT("count"), ibDataValue::Int((s64)objects.size()));
		result.AddField(wxT("objects"), ibDataValue::Array(objects));

		if (anyRestricting)
			result.SetValue(wxT("composition"),
				ibMcpText("A restricting role grants nothing of its own - it SUBTRACTS from whatever the "
				  "permitting roles allowed, and no further role can widen past it. Read it in "
				  "`grantedBy` as 'does not object', never as 'allows'. The verdict a user "
				  "actually gets is (any permitting role grants) AND (every restricting role "
				  "agrees), so the order roles are assigned in never changes it."));

		if (objects.empty())
			result.SetValue(wxT("note"), only.IsEmpty()
				? ibMcpText("Nothing to report - either no object declares rights, or nothing matched the "
					"filters.")
				: ibMcpText("That object declares no rights, or does not exist under that name."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolRoleRights);

//---------------------------------------------------------------------------
// metadata_rights
//---------------------------------------------------------------------------
//
// ⭐ WHAT AN OBJECT DECLARES, ASKED OF THE OBJECT. role_rights answers "what does this ROLE let
// through", which presupposes knowing the names to look for; this is the other question, and the
// one that comes first: a catalog declares Read / Write / Delete, a section declares Use, the
// configuration root declares Administration and Update database configuration. Nothing shares a
// list — every metatype states its own where it is built (CreateRole in the metaobject's header),
// so the only honest answer is to ask each object.
//
// ⭐ AND IT REPORTS THE DEFAULT. A right nobody ever flipped is not denied: it falls back to the
// value declared with it (roleHelper.cpp). That is why a brand-new role appears to permit things
// nobody granted, and it cannot be understood from the checkbox alone — so the default is part of
// the answer rather than a footnote.
//
//---------------------------------------------------------------------------

class ibMcpToolMetadataRights : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_rights"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString object = ArgObject().Text(params);

		return object.IsEmpty()
			? ibMcpText("reading which rights the configuration declares")
			: wxString::Format(ibMcpText("reading which rights '%s' declares"), object);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Which rights a metaobject declares, and what each one answers when no role ever "
			"set it. Ask this BEFORE role_grant: every metatype declares its own rights (a catalog "
			"has Read, Write, Delete; a section has Use; the configuration root has Administration "
			"and Update database configuration) and there is no shared list. Without `object` it "
			"answers for the whole configuration, grouped by the set of rights declared.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open.");
			return false;
		}

		const wxString only = ArgObject().Text(params);

		std::vector<ibDataValue> objects;
		bool foundNamed = false;

		std::function<void(ibValueMetaObject*, const wxString&)> describe =
			[&](ibValueMetaObject* object, const wxString& path) {

			if (object == nullptr || object->IsDeleted())
				return;

			const wxString here = path.IsEmpty()
				? object->GetName()
				: path + wxT(".") + object->GetName();

			const bool thisOne = only.IsEmpty()
				|| stringUtils::CompareString(only, object->GetName())
				|| stringUtils::CompareString(only, here);

			if (thisOne) {

				foundNamed = true;

				if (object->GetRoleCount() > 0) {

					std::vector<ibDataValue> rights;

					for (unsigned int index = 0; index < object->GetRoleCount(); ++index) {

						const ibRole* right = object->GetRole(index);
						if (right == nullptr)
							continue;

						std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
						entry->SetValue(wxT("name"), right->GetName());
						entry->SetValue(wxT("label"), right->GetLabel());
						entry->AddField(wxT("whenUnset"), ibDataValue::Bool(right->GetDefValue()));

						rights.push_back(ibDataValue::Child(entry));
					}

					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					entry->SetValue(wxT("object"), here);
					entry->SetValue(wxT("kind"), object->GetClassName());
					entry->AddField(wxT("id"), ibDataValue::Int((s64)object->GetMetaID()));

					// ROW OR TABLE — the object's own answer, and it changes what a right MEANS:
					// a register controls its whole table, a catalog element controls itself.
					entry->AddField(wxT("perRecord"), ibDataValue::Bool(object->IsAccessPerRecord()));
					entry->AddField(wxT("rights"), ibDataValue::Array(rights));

					objects.push_back(ibDataValue::Child(entry));
				}
			}

			// ⭐ THE CLSID IS THE ANSWER. It was asked first and then asked again as a cast, which
			// cannot say anything the first test did not: an exact class id is an exact type. Two
			// spellings of one question, and the second one carries a null branch that is dead.
			if (object->GetClassType() != g_metaSectionCLSID)
				return;

			ibValueMetaObjectSection* section = static_cast<ibValueMetaObjectSection*>(object);

			for (ibValueMetaObjectSection* child : section->GetInterfaceArrayObject())
				describe(child, here);
		};

		// The root first, and it is NOT in the array — see the note on role_grant. The rights that
		// decide whether anybody may administer anything live only here.
		describe(activeMetaData->GetCommonMetaObject(), wxEmptyString);

		for (ibValueMetaObject* object : activeMetaData->GetAnyArrayObject())
			describe(object, wxEmptyString);

		if (!only.IsEmpty() && !foundNamed) {
			refusal = wxString::Format(
				ibMcpText("This configuration has no object named '%s'."), only);
			return false;
		}

		result.AddField(wxT("count"), ibDataValue::Int((s64)objects.size()));
		result.AddField(wxT("objects"), ibDataValue::Array(objects));

		if (objects.empty())
			result.SetValue(wxT("note"),
				ibMcpText("That object declares no rights - nothing about it is governed by roles."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataRights);

//---------------------------------------------------------------------------
// role_grant
//---------------------------------------------------------------------------
//
// ⭐ THE WRITING END OF THE SAME TABLE. role_rights reads the column; this sets it. The walk is
// deliberately identical - same recursion, same "an object with no rights is skipped" - because a
// caller that read an answer and acts on it must be addressing the same objects it was shown.
//
// ⭐ WHY "EVERYTHING" IS THE FIRST THING A ROLE NEEDS. Rights are not stored per role: an object
// keeps a map of what was SET, and a right nobody flipped falls back to the DEFAULT declared where
// it was created (roleHelper.cpp, ibAccessObject::AccessRight). So a freshly created role is not
// empty - it is SILENT, and silence reads as the default. That is fine until the configuration
// wants a real administrator: a role has to say so out loud, on every object, before a narrower
// role beside it means anything. Granting object by object is a thousand calls; this is one, and
// it is what the first role in a configuration is for.
//
// ⚠ IT WRITES, AND SAYS HOW MUCH. Changed objects are COUNTED, not listed - a configuration-wide
// grant would answer with the whole tree and drown the number that matters.
//
//---------------------------------------------------------------------------

class ibMcpToolRoleGrant : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("role_grant"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString role = ArgRole().Text(params);
		const wxString object = ArgObject().Text(params);

		if (object.IsEmpty())
			return ArgValue().Flag(params)
				? wxString::Format(ibMcpText("granting '%s' everything"), role)
				: wxString::Format(ibMcpText("denying '%s' everything"), role);

		return wxString::Format(ibMcpText("setting what '%s' may do with '%s'"), role, object);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Grant or deny rights to a role - the checkboxes in the role editor. Without "
			"`object` it applies to EVERY object that declares rights, which is how an "
			"administrator role is made: grant it everything once, then create narrower roles "
			"beside it. Without `rights` it applies to every right the object declares. A new role "
			"grants nothing explicitly and merely falls back to each right's default, so say what "
			"it may do before relying on it. metadata_rights lists the names.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgRole(), ArgValue(), ArgObject(), ArgRights() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		if (activeMetaData == nullptr || !activeMetaData->IsConfigOpen()) {
			refusal = ibMcpText("No configuration is open.");
			return false;
		}

		const wxString roleName = ArgRole().Text(params);
		ibValueMetaObject* roleObject = ibFindMetaObject(activeMetaData, wxT("Role"), roleName);

		if (roleObject == nullptr) {
			refusal = wxString::Format(ibMcpText("This configuration has no role named '%s'."), roleName);
			return false;
		}

		// ⚠ NO DEFAULT for the value: a rights change assumed in either direction is a security
		// decision nobody made.
		const ibDataValue* asked = params.FindField(ArgValue().Name());

		if (asked == nullptr || asked->Kind() != ibDataKind::Bool) {
			refusal = ibMcpText("Say value: true to grant or value: false to deny. Nothing was changed.");
			return false;
		}

		const bool value = asked->AsBool();
		const ibRoleID roleId = (ibRoleID)roleObject->GetMetaID();

		const wxString only = ArgObject().Text(params);

		// WHICH RIGHTS, held as words. Empty means "whatever this object declares", decided per
		// object rather than once, because objects declare different ones.
		std::vector<wxString> wantedRights;

		if (const ibDataValue* named = params.FindField(ArgRights().Name()))
			if (named->Kind() == ibDataKind::Array)
				for (const ibDataValue& one : named->AsArray())
					if (one.Kind() == ibDataKind::String)
						wantedRights.push_back(one.AsString());

		int touchedObjects = 0;
		int touchedRights = 0;
		bool foundNamed = false;

		std::function<void(ibValueMetaObject*, const wxString&)> apply =
			[&](ibValueMetaObject* object, const wxString& path) {

			if (object == nullptr || object->IsDeleted())
				return;

			const wxString here = path.IsEmpty()
				? object->GetName()
				: path + wxT(".") + object->GetName();

			const bool thisOne = only.IsEmpty()
				|| stringUtils::CompareString(only, object->GetName())
				|| stringUtils::CompareString(only, here);

			if (thisOne) {

				foundNamed = true;
				int settled = 0;

				for (unsigned int index = 0; index < object->GetRoleCount(); ++index) {

					const ibRole* right = object->GetRole(index);
					if (right == nullptr)
						continue;

					if (!wantedRights.empty()) {

						bool named = false;

						for (const wxString& wanted : wantedRights)
							if (stringUtils::CompareString(wanted, right->GetName())) {
								named = true;
								break;
							}

						if (!named)
							continue;
					}

					if (object->SetRight(right, roleId, value))
						settled++;
				}

				if (settled > 0) {
					touchedObjects++;
					touchedRights += settled;
				}
			}

			// ⭐ THE CLSID IS THE ANSWER. It was asked first and then asked again as a cast, which
			// cannot say anything the first test did not: an exact class id is an exact type. Two
			// spellings of one question, and the second one carries a null branch that is dead.
			if (object->GetClassType() != g_metaSectionCLSID)
				return;

			ibValueMetaObjectSection* section = static_cast<ibValueMetaObjectSection*>(object);

			for (ibValueMetaObjectSection* child : section->GetInterfaceArrayObject())
				apply(child, here);
		};

		// ⭐ THE ROOT COUNTS, AND IT IS NOT IN THE ARRAY. Administration, Update database
		// configuration, Active users and Exclusive mode are declared on the CONFIGURATION object
		// - precisely the rights an administrator role exists to hold. A walk over the metaobject
		// array alone would report success and grant none of them.
		apply(activeMetaData->GetCommonMetaObject(), wxEmptyString);

		for (ibValueMetaObject* object : activeMetaData->GetAnyArrayObject())
			apply(object, wxEmptyString);

		if (!only.IsEmpty() && !foundNamed) {
			refusal = wxString::Format(
				ibMcpText("This configuration has no object named '%s'."), only);
			return false;
		}

		if (touchedRights > 0)
			activeMetaData->Modify(true);

		result.SetValue(wxT("role"), roleObject->GetName());
		result.AddField(wxT("value"), ibDataValue::Bool(value));
		result.AddField(wxT("objects"), ibDataValue::Int((s64)touchedObjects));
		result.AddField(wxT("rights"), ibDataValue::Int((s64)touchedRights));

		if (touchedRights == 0)
			result.SetValue(wxT("note"), only.IsEmpty()
				? ibMcpText("Nothing declares rights under those names - check them against "
					"metadata_rights.")
				: ibMcpText("That object declares no rights under those names."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolRoleGrant);
