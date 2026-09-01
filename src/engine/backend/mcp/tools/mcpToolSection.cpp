////////////////////////////////////////////////////////////////////////////
//	Description : what is checked into a section — the command interface
////////////////////////////////////////////////////////////////////////////
//
// ⭐ THE TICK-BOX, WHICH IS ALL A SECTION IS. A section (`Section` under Common) declares nothing
// about its contents; the CONTENTS declare their sections. Every metaobject carries a set of
// section ids (ibInterfaceObject::m_interfaces, backend/interfaceHelper.h) and the section's editor
// simply paints one column of checkboxes over the whole tree — which is why "what is in this
// section" is a walk, and "put this in that section" is one call on the OBJECT.
//
// ⭐ THE MECHANISM WAS ALREADY THERE, AND HAD NO DOOR: SetInterface / IsSetInterface have existed
// as long as sections have. Nothing here computes membership — it asks and it sets.
//
// ⚠ AND IT IS THE LAST STEP OF BUILDING ANYTHING. A catalog nobody checked into a section exists,
// compiles, restructures and is reachable from script — and is invisible in the application,
// because the command interface is what a person navigates by. That is why this refuses quietly
// nowhere: an object outside every section is a finished object nobody can open.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaSectionObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/stringUtils.h"

#include <functional>
#include <set>

namespace {

// ⭐⭐ THE TREE FINDS ITS OWN. This was two hand-written functions — one recursing through
// GetInterfaceArrayObject, one walking every top-level section and casting each result — and both
// of them are ibMetaData::FindAnyObjectByFilter, which takes a name, a clsid filter and a flag
// saying "descend". It even settles the cast the same way: when the clsid filter matched it
// static_casts, because an exact class id IS an exact type.
//
// SECTIONS NEST, which is why the third argument is true: a caller naming a sub-section means that
// sub-section, at whatever depth. That is the whole reason the hand-rolled recursion existed.
ibValueMetaObjectSection* FindSection(ibMetaData* metaData, const wxString& name)
{
	return metaData->FindAnyObjectByFilter<ibValueMetaObjectSection>(
		name, g_metaSectionCLSID, /*use_child_filter*/ true);
}

// Every section in the tree, deepest last — for saying which ones exist when a name misses. A
// refusal that lists the alternatives ends the guessing in one round trip.
void CollectSectionNames(ibValueMetaObjectSection* where, const wxString& path,
	std::vector<wxString>& into)
{
	if (where == nullptr || where->IsDeleted())
		return;

	const wxString here = path.IsEmpty()
		? where->GetName() : path + wxT(".") + where->GetName();

	into.push_back(here);

	for (ibValueMetaObjectSection* child : where->GetInterfaceArrayObject())
		CollectSectionNames(child, here, into);
}

std::vector<wxString> AllSectionNames(ibMetaData* metaData)
{
	std::vector<wxString> names;

	for (ibValueMetaObjectSection* object : metaData->GetAnyArrayObject<ibValueMetaObjectSection>(g_metaSectionCLSID))
		CollectSectionNames(object, wxEmptyString, names);

	return names;
}

// The ids of every section, at every depth — so "does this object belong anywhere" is one set
// lookup instead of a walk per object.
void CollectSectionIds(ibValueMetaObjectSection* where, std::set<ibMetaID>& into)
{
	if (where == nullptr || where->IsDeleted())
		return;

	into.insert(where->GetMetaID());

	for (ibValueMetaObjectSection* child : where->GetInterfaceArrayObject())
		CollectSectionIds(child, into);
}

wxString ListNames(const std::vector<wxString>& names)
{
	wxString out;

	for (const wxString& name : names) {
		if (!out.IsEmpty())
			out += wxT(", ");
		out += name;
	}

	return out;
}

// An id argument, read only when it really is a number — AsInt raises on anything else, and a
// caller passing a name where an id belongs deserves a sentence rather than an exception.
bool ReadId(const ibDataValue& value, ibMetaID& id)
{
	if (value.Kind() != ibDataKind::Number)
		return false;

	id = (ibMetaID)value.AsInt();
	return true;
}

using ibArg = ibMcpTool::ibMcpArgument;

// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgSection()
{
	static const ibArg s_a(wxT("section"), ibArg::Kind::Text,
		_("The section's name. Sections nest - name the sub-section itself, not the path to "
			  "it."), /*required*/ true);
	return s_a;
}

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		_("The metaobject to check in, by id from metadata_list or metadata_create."));
	return s_a;
}

const ibArg& ArgIds()
{
	static const ibArg s_a(wxT("ids"), ibArg::Kind::Many,
		_("Several at once, instead of `id`. Each is reported on separately, and one bad id "
			  "does not undo the rest."));
	return s_a;
}

const ibArg& ArgRemove()
{
	static const ibArg s_a(wxT("remove"), ibArg::Kind::Flag,
		_("Take them OUT of the section instead of putting them in. Off by default."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// section_include
//---------------------------------------------------------------------------

class ibMcpToolSectionInclude : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("section_include"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		// 🛑 IT READ `id` AND THE CALL HAD PASSED `ids`, so the line came out as "putting '' into
		// the section 'Warehouse'" — empty quotes where the substance should be. A person watching
		// the window saw something happen to nothing.
		//
		// ⭐ AND ONE NAME WOULD NOT HAVE BEEN ENOUGH EITHER. This tool takes a batch, and "putting
		// 'Goods' into..." while five others go in beside it is a line that is true and still
		// misleading. What a watcher needs is HOW MANY, and the names while they still fit.
		wxString what;

		if (const ibDataValue* many = params.FindField(ArgIds().Name())) {
			if (many->Kind() == ibDataKind::Array) {

				const size_t count = many->AsArray().size();

				what = count == 1
					? ibMcpNameOf(params, ArgIds().Name())
					: wxString::Format(_("%u objects"), (unsigned)count);
			}
		}
		else if (params.FindField(ArgId().Name()) != nullptr) {
			what = ibMcpNameOf(params);
		}

		if (what.IsEmpty())
			what = _("nothing");

		return ArgRemove().Flag(params)
			? wxString::Format(_("taking %s out of the section '%s'"),
				what, ArgSection().Text(params))
			: wxString::Format(_("putting %s into the section '%s'"),
				what, ArgSection().Text(params));
	}

	wxString GetDescription() const override
	{
		return _("Check a metaobject into a section, or take it out - the tick-boxes the section's "
			"editor shows. THIS IS THE LAST STEP OF BUILDING ANYTHING a person is meant to open: a "
			"catalog or a document outside every section works perfectly and is invisible in the "
			"running application, because sections ARE the command interface. Takes several ids at "
			"once, since a section is normally filled in one go.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgSection(), ArgId(), ArgIds(), ArgRemove() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaDataConfigurationBase* metaData = activeMetaData;

		if (metaData == nullptr || !metaData->IsConfigOpen()) {
			refusal = _("No configuration is open.");
			return false;
		}

		const wxString sectionName = ArgSection().Text(params);

		ibValueMetaObjectSection* section = FindSection(metaData, sectionName);

		if (section == nullptr) {
			const std::vector<wxString> known = AllSectionNames(metaData);

			// ⭐ REFUSED WITH THE ANSWER. A caller that misspelled a section can act on this reply;
			// one told only "not found" has to go and ask a second question.
			refusal = known.empty()
				? _("This configuration declares no sections at all. Create one under "
					"Common - Sections first (metadata_create kind=Section).")
				: wxString::Format(_("No section is called '%s'. There is: %s."),
					sectionName, ListNames(known));
			return false;
		}

		const bool remove = ArgRemove().Flag(params);

		// ONE ID OR MANY, read into one list so the loop below is the only place that knows how.
		std::vector<ibMetaID> wanted;

		if (const ibDataValue* many = params.FindField(ArgIds().Name())) {
			if (many->Kind() == ibDataKind::Array)
				for (const ibDataValue& one : many->AsArray()) {
					ibMetaID id = 0;
					if (ReadId(one, id))
						wanted.push_back(id);
				}
		}

		if (const ibDataValue* one = params.FindField(ArgId().Name())) {
			ibMetaID id = 0;
			if (ReadId(*one, id))
				wanted.push_back(id);
		}

		if (wanted.empty()) {
			refusal = _("Nothing to check in - pass `id`, or `ids` for several.");
			return false;
		}

		std::vector<ibDataValue> done;
		std::vector<ibDataValue> failed;
		bool changed = false;

		for (const ibMetaID& id : wanted) {

			ibValueMetaObject* object = ibFindMetaObjectById(metaData, id);

			if (object == nullptr || object->IsDeleted()) {
				failed.push_back(ibDataValue::String(
					wxString::Format(_("#%d - no such object"), (int)id)));
				continue;
			}

			// ⚠ A SECTION CANNOT HOLD ITSELF. It would read as a cycle to every walk over the
			// interface, and the editor does not offer the box either.
			if (object == section) {
				failed.push_back(ibDataValue::String(
					wxString::Format(_("%s - a section cannot contain itself"), object->GetName())));
				continue;
			}

			// ⭐ AND NOT EVERYTHING BELONGS IN ONE. The editor offers a checkbox only for what
			// answers IsInterfaceAllowed; setting the flag on anything else would store a
			// membership no interface ever reads — a write that succeeds and means nothing, which
			// is worse than a refusal because there is nothing to notice.
			if (!object->IsInterfaceAllowed()) {
				failed.push_back(ibDataValue::String(wxString::Format(
					_("%s (%s) - this kind does not appear in a command interface"),
					object->GetName(), object->GetClassName())));
				continue;
			}

			// ALREADY WHERE IT WAS ASKED TO BE — reported as done rather than as a change, so a
			// caller repeating itself does not see the configuration go modified for nothing.
			if (object->IsSetInterface(section->GetMetaID()) == !remove) {
				done.push_back(ibDataValue::String(object->GetName()));
				continue;
			}

			object->SetInterface(section->GetMetaID(), !remove);

			done.push_back(ibDataValue::String(object->GetName()));
			changed = true;
		}

		if (changed)
			metaData->Modify(true);

		result.SetValue(wxT("section"), section->GetName());
		result.AddField(wxT("id"), ibDataValue::Int((s64)section->GetMetaID()));
		result.AddField(remove ? wxT("removed") : wxT("included"), ibDataValue::Array(done));

		if (!failed.empty())
			result.AddField(wxT("refused"), ibDataValue::Array(failed));

		if (!changed && failed.empty())
			result.SetValue(wxT("note"),
				_("Everything named was already the way it was asked to be - nothing changed."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSectionInclude);

//---------------------------------------------------------------------------
// section_content
//---------------------------------------------------------------------------

class ibMcpToolSectionContent : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("section_content"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString section = ArgSection().Text(params);

		return section.IsEmpty()
			? _("reading the command interface")
			: wxString::Format(_("reading what is in the section '%s'"), section);
	}

	wxString GetDescription() const override
	{
		return _("What each section holds - the checked boxes, read back. Without arguments it "
			"answers every section, which is the whole command interface and the fastest way to "
			"see what a person can actually reach. Objects belonging to NO section are listed "
			"separately: they are the ones nobody can open.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {  };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaDataConfigurationBase* metaData = activeMetaData;

		if (metaData == nullptr || !metaData->IsConfigOpen()) {
			refusal = _("No configuration is open.");
			return false;
		}

		const wxString only = ArgSection().Text(params);

		if (!only.IsEmpty() && FindSection(metaData, only) == nullptr) {
			const std::vector<wxString> known = AllSectionNames(metaData);
			refusal = known.empty()
				? _("This configuration declares no sections at all.")
				: wxString::Format(_("No section is called '%s'. There is: %s."),
					only, ListNames(known));
			return false;
		}

		// EVERY OBJECT THAT COULD BE CHECKED IN, asked once — the membership lives on the object,
		// so this is a single pass no matter how many sections there are.
		const std::vector<ibValueMetaObject*> everything = metaData->GetAnyArrayObject();

		std::vector<ibDataValue> sections;
		std::vector<ibDataValue> homeless;

		// Which sections each object belongs to, accumulated in the same pass that fills them.
		std::function<void(ibValueMetaObjectSection*, const wxString&)> describe =
			[&](ibValueMetaObjectSection* section, const wxString& path) {

			if (section == nullptr || section->IsDeleted())
				return;

			const wxString here = path.IsEmpty()
				? section->GetName() : path + wxT(".") + section->GetName();

			if (only.IsEmpty() || stringUtils::CompareString(only, section->GetName())
				|| stringUtils::CompareString(only, here)) {

				std::vector<ibDataValue> items;

				for (ibValueMetaObject* object : everything)
					if (object != nullptr && !object->IsDeleted()
						&& object->IsSetInterface(section->GetMetaID())) {

						std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
						entry->SetValue(wxT("name"), object->GetName());
						entry->SetValue(wxT("kind"), object->GetClassName());
						entry->AddField(wxT("id"), ibDataValue::Int((s64)object->GetMetaID()));

						items.push_back(ibDataValue::Child(entry));
					}

				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				entry->SetValue(wxT("section"), here);
				entry->AddField(wxT("id"), ibDataValue::Int((s64)section->GetMetaID()));
				entry->AddField(wxT("count"), ibDataValue::Int((s64)items.size()));
				entry->AddField(wxT("items"), ibDataValue::Array(items));

				sections.push_back(ibDataValue::Child(entry));
			}

			for (ibValueMetaObjectSection* child : section->GetInterfaceArrayObject())
				describe(child, here);
		};

		for (ibValueMetaObjectSection* object : metaData->GetAnyArrayObject<ibValueMetaObjectSection>(g_metaSectionCLSID))
			describe(object, wxEmptyString);

		// ⭐ THE ANSWER THAT MATTERS MOST, and only this walk can give it: what nobody can reach.
		// Asked only of a whole reading — narrowed to one section, "belongs to no section" would
		// be a different question wearing the same word.
		if (only.IsEmpty()) {

			std::set<ibMetaID> everySection;

			for (ibValueMetaObjectSection* object : metaData->GetAnyArrayObject<ibValueMetaObjectSection>(g_metaSectionCLSID))
				CollectSectionIds(object, everySection);

			for (ibValueMetaObject* object : everything) {

				if (object == nullptr || object->IsDeleted())
					continue;

				// 🛑 ASKED OF THE OBJECT, which is what the section EDITOR already does. This used
				// to report every metaobject that was not in a section — and most of them never
				// could be: a common module, a language, a role are not things a person opens
				// from a command interface. The count came out alarming and wrong, and it was
				// repeated as if it meant something.
				//
				// `IsInterfaceAllowed` is the same question the editor's tree is built from
				// (interfaceEditor.cpp), so this list and those checkboxes cannot disagree — and
				// a metatype added later answers for itself instead of being missed by a list
				// somebody forgot to extend. The section test that used to be here was one entry
				// of exactly such a list.
				if (!object->IsInterfaceAllowed())
					continue;

				bool anywhere = false;

				for (const ibMetaID& id : everySection)
					if (object->IsSetInterface(id)) {
						anywhere = true;
						break;
					}

				if (!anywhere) {
					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					entry->SetValue(wxT("name"), object->GetName());
					entry->SetValue(wxT("kind"), object->GetClassName());
					entry->AddField(wxT("id"), ibDataValue::Int((s64)object->GetMetaID()));
					homeless.push_back(ibDataValue::Child(entry));
				}
			}

			result.AddField(wxT("inNoSection"), ibDataValue::Array(homeless));

			if (!homeless.empty())
				result.SetValue(wxT("note"),
					_("Those belong to no section, so nobody can open them in the running "
					  "application. section_include puts them somewhere."));
		}

		result.AddField(wxT("sections"), ibDataValue::Array(sections));

		if (sections.empty())
			result.SetValue(wxT("note"),
				_("This configuration declares no sections - there is no command interface yet."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolSectionContent);
