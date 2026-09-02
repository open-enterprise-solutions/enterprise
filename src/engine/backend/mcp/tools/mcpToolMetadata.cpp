////////////////////////////////////////////////////////////////////////////
//	Description : the metadata tools — what is there, and what it looks like
////////////////////////////////////////////////////////////////////////////
//
// The verbs belong to the metadata (metaCollection/metaIntrospect.h); this file
// is the door and the wording.
//
// THE CONFIGURATION IS THE ACTIVE ONE. A tool runs inside the session that
// started the server, in a host that has exactly one configuration open, and
// changing THAT one is the job — so the entry point resolves it here and hands
// it down. The mechanism below still takes it as an argument, which is what
// keeps it testable and keeps the assumption in one visible place.
//
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpTool.h"

#include "backend/metaCollection/attribute/metaAttributeObject.h"   // anything that DECLARES a type
#include "backend/metaCollection/genericData.h"        // the owner lays a new form out
#include "backend/metaCollection/metaComposerObject.h" // …and a query is the other place a name is said
#include "backend/metaCollection/metaFormObject.h"
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaModuleObject.h"   // the standard handlers a module may implement
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/objCtor.h"                          // ibCtorMetaValueType — a reference type by name
#include "backend/propertyManager/property/propertyList.h"   // a form is asked WHICH KIND — the owner fills the list
#include "backend/propertyManager/propertyObject.h"   // GetProperty(idx) — what can be set
#include "backend/propertyManager/property/variant/variantType.h"   // ibVariantDataAttribute — a value that hides a type description
#include "backend/restructureInfo.h"                  // the ledger an object complains into
#include "backend/typeDescription.h"                  // …and the description it hides

namespace {

// Every tool here needs the same first sentence, and the same refusal when
// there is nothing open to answer about.
ibMetaData* OpenConfiguration(wxString& refusal)
{
	ibMetaData* metaData = activeMetaData;

	if (metaData == nullptr || !metaData->IsConfigOpen()) {
		refusal = ibMcpText("No configuration is open.");
		return nullptr;
	}

	return metaData;
}

// WHAT THE OBJECT ITSELF COMPLAINS ABOUT.
//
// Every metaobject already knows when it is not fit to be stored — an
// accumulation register with nobody recording into it says "Doesn't have any
// recorder" — and it says so in the platform's own words, into the
// restructuring ledger. Nothing here invents a list of rules: the rules are
// wherever they were already written, and this only asks.
//
// ⚠ The ledger is process-wide, so it is emptied before the question and after
// it. In a designer session nothing else is writing to it at that moment; a
// batch apply would want its own, which is a reason not to call this during one.
std::vector<wxString> Complaints(ibValueMetaObject* object)
{
	std::vector<wxString> found;
	if (object == nullptr)
		return found;

	ibRestructureInfo& ledger = ibMetaDataConfigurationBase::GetRestructureInfo();
	ledger.Clear();

	try {
		object->OnSaveMetaObject(0);
	}
	catch (...) {
		// A refusal that throws is still a refusal; whatever reached the ledger
		// before it threw is what we came for.
	}

	for (const ibRestructureInfo::Entry& entry : ledger) {
		if (entry.type == ibRestructure::error || entry.type == ibRestructure::warning)
			found.push_back(entry.descr);
	}

	ledger.Clear();
	return found;
}


using ibArg = ibMcpTool::ibMcpArgument;

// The one argument this file both declares AND reads — which is what makes spelling it here
// legitimate. `id`, `value` and `language` are read by the shared doors and declared beside them.
const ibArg& ArgProperty()
{
	static const ibArg s_property(wxT("property"), ibArg::Kind::Text,
		ibMcpText("The property's name, spelled as metadata_get shows it - 'Type', 'Synonym', 'Comment'..."),
		/*required*/ true);
	return s_property;
}
const ibArg& ArgAcceptsId()
{
	static const ibArg s_id(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("An existing object, as NodeId - what IT holds and what it is still missing. Omit for "
		  "the configuration root, or pass `kind` instead to ask about a kind."));
	return s_id;
}

const ibArg& ArgAcceptsKind()
{
	static const ibArg s_kind(wxT("kind"), ibArg::Kind::Text,
		ibMcpText("A KIND, spelled the way a script writes it - Catalog, Document, Attribute. Answers what "
		  "one of those would hold BEFORE you make one: an empty one is built where it would live, "
		  "asked, and dropped, so nothing is created in the configuration. Metadata is built "
		  "tree-wise, so a kind that lives INSIDE something needs `parent_id` - an attribute under "
		  "a catalog holds different things from one under a register."));
	return s_kind;
}

const ibArg& ArgOnlyProperty()
{
	static const ibArg s_property(wxT("property"), ibArg::Kind::Text,
		ibMcpText("One property by name. Omit for all of them."));
	return s_property;
}

const ibArg& ArgEditableOnly()
{
	static const ibArg s_editableOnly(wxT("editableOnly"), ibArg::Kind::Flag,
		ibMcpText("Leave out the ones that cannot be changed - usually most of the answer."));
	return s_editableOnly;
}


// The arguments this file's tools take — declared once, and read through the same
// objects in Call, so the name a caller is told cannot drift from the name looked for.
const ibArg& ArgKind()
{
	static const ibArg s_a(wxT("kind"), ibArg::Kind::Text,
		ibMcpText("The kind, spelled the way a script writes it: Catalog, Document, "
			  "InformationRegister, AccumulationRegister, Report, DataProcessor, Enum..."), /*required*/ true);
	return s_a;
}

// ⭐⭐ THE SAME ARGUMENT, NOT REQUIRED — for the tools that have TWO ROADS in.
//
// 🛑 REQUIREDNESS IS THE TOOL'S, NOT THE ARGUMENT'S, and one shared object carried it for every
// tool that used it. `metadata_get` takes an id OR a kind and a name, and it says so in its own
// refusal — but `kind` arrived declared required, so the moment the gate began ENFORCING that
// (2026-09-01, the same day), the id road stopped working entirely: `metadata_get {id: 1283}` came
// back "needs 'kind', and it did not come".
//
// The declaration had been wrong all along and was harmless while nothing read it. Enforcing a rule
// is what tells you where it was never true.
const ibArg& ArgKindOptional()
{
	static const ibArg s_a(wxT("kind"), ibArg::Kind::Text,
		ibMcpText("The kind, spelled the way a script writes it: Catalog, Document, "
			  "InformationRegister, AccumulationRegister, Report, DataProcessor, Enum... "
			  "Not needed when you ask by id."));
	return s_a;
}

const ibArg& ArgId()
{
	static const ibArg s_a(wxT("id"), ibArg::Kind::Whole,
		ibMcpText("The object's identity, as NodeId in a previous answer. Survives a rename, "
			  "and finds an attribute or a tabular section as readily as its owner."));
	return s_a;
}

const ibArg& ArgParentId()
{
	static const ibArg s_a(wxT("parent_id"), ibArg::Kind::Whole,
		ibMcpText("Where it goes, as NodeId from a previous answer - a catalog's id to add an "
			  "attribute to it. Omit for a top-level object."));
	return s_a;
}

const ibArg& ArgName()
{
	static const ibArg s_a(wxT("name"), ibArg::Kind::Text,
		ibMcpText("What to call it. Omitted, it gets the same generated name a click would give it."));
	return s_a;
}

const ibArg& ArgNote()
{
	static const ibArg s_a(wxT("note"), ibArg::Kind::Text,
		ibMcpText("Why this object exists and what was decided - markdown, for whoever builds the "
			  "configuration next. Write it AS you create: the reasons are never cheaper to "
			  "record than now."));
	return s_a;
}

const ibArg& ArgHelp()
{
	static const ibArg s_a(wxT("help"), ibArg::Kind::Text,
		ibMcpText("What the person USING the application should read about this, on F1."));
	return s_a;
}

const ibArg& ArgProperties()
{
	static const ibArg s_a(wxT("properties"), ibArg::Kind::Node,
		ibMcpText("Properties to set on the new object, by name - {\"FormType\": \"Object form\"}. "
			  "Each is placed the way metadata_set places it, so a property with a closed set "
			  "takes one of its words. The answer lists every property the object has, with what "
			  "each accepts, so one call is enough to learn the rest. Passing any of these also "
			  "means the platform will not stop to ASK: a form, which a click answers in a "
			  "dialog, is created outright."));
	return s_a;
}

const ibArg& ArgType()
{
	static const ibArg s_a(wxT("type"), ibArg::Kind::Text,
		ibMcpText("The type's name: String, Number, Date, Boolean, or a reference type like "
			  "CatalogRef.Goods / DocumentRef.GoodsReceipt."));
	return s_a;
}

// ⭐⭐ A WHOLE TYPE DESCRIPTION, TAKEN BACK IN THE SHAPE IT WAS GIVEN.
//
// `type` above states ONE type in a word, which is the ordinary case and is what a person would
// say. It cannot state a COMPOSITE type — several types at once, each with its own qualifiers —
// and that is not a gap to be filled with more arguments: the description already writes itself
// and already reads itself back. So the second road is the round trip.
//
// The example in the schema is an EMPTY description, written by the same reader that will take the
// filled-in one — Max, 2026-09-01: *"get the type out of a value, write it, lay it out as JSON,
// work on it and throw it back."*
const ibArg& ArgTypeShape()
{
	static const ibArg s_a(wxT("description"), ibArg::Kind::Node,
		ibMcpText("The whole type description, in the shape metadata_get and metadata_set_type answer "
		  "with - use this instead of `type` for a COMPOSITE type, where a single name cannot say "
		  "what is held. Read one, change it, send it back."),
		/*required*/ false, std::vector<wxString>(),
		[](ibDataValue& shape) {
			return ibTypeDescriptionMemory::WriteNode(shape, ibTypeDescription(), activeMetaData);
		});
	return s_a;
}

const ibArg& ArgLength()
{
	static const ibArg s_a(wxT("length"), ibArg::Kind::Whole,
		ibMcpText("For String - how many characters. Default 10."));
	return s_a;
}

const ibArg& ArgPrecision()
{
	static const ibArg s_a(wxT("precision"), ibArg::Kind::Whole,
		ibMcpText("For Number - total digits. Default 10."));
	return s_a;
}

const ibArg& ArgScale()
{
	static const ibArg s_a(wxT("scale"), ibArg::Kind::Whole,
		ibMcpText("For Number - digits after the point. Default 0."));
	return s_a;
}

const ibArg& ArgTypeId()
{
	static const ibArg s_a(wxT("typeId"), ibArg::Kind::Whole,
		ibMcpText("The type's id instead of its name - steadier, because a reference type's NAME "
			  "contains the object's name and a rename breaks it. type_list gives both."));
	return s_a;
}

const ibArg& ArgDepth()
{
	static const ibArg s_a(wxT("depth"), ibArg::Kind::Whole,
		ibMcpText("How many levels down to walk. 1 is the children of the node asked about, 2 their "
		  "children too. Omit for 2, which is enough to see an object and what it holds; pass 0 "
		  "for the whole subtree. ASK WITH 1 FIRST when the question is what this configuration "
		  "holds at all - the default answers every attribute of every object, and eight of them "
		  "on a catalog are predefined ones that are the same everywhere."));
	return s_a;
}

// ⭐ THE TWO FLAGS A METAOBJECT CARRIES ABOUT ITSELF, and until now neither left the building.
//
// `deleted` is marked-for-deletion — still in the tree, gone at the next save.
//
// `disabled` is NOT somebody's switch: it is the platform's own answer that this node does not
// apply as its owner currently stands (metaDisableFlag). A catalog with no owner carries `Owner`
// and disables it; a turnovers register carries `RecordType` and disables it; a chart of accounts
// disables the dimension columns past the number it declares. The node is there, it is answered by
// every walk, and it is not a field of this object — which is exactly the kind of thing a caller
// reads as real, binds a control to, and writes into a query.
//
// So it is REPORTED and not settable: the owner computes it from its own properties, and a door
// that let it be written from outside would be a second authority on a fact that already has one.
const ibArg& ArgDeleted()
{
	static const ibArg s_a(wxT("deleted"), ibArg::Kind::Flag,
		ibMcpText("Include objects marked for deletion, each said to be so. Off by default: they go at the "
		  "next save, and offering their ids invites work on something about to vanish."));
	return s_a;
}

} // namespace

//---------------------------------------------------------------------------
// metadata_tree
//---------------------------------------------------------------------------
//
// ⭐⭐ EVERYTHING WITH ITS ADDRESS, IN ONE CALL. The point is orientation: a caller should be able
// to see what is there and reach for it, not go hunting (Max, 2026-09-01: *"so that navigating the
// elements and adding them is easy — so you do not spend time on obscure searches, everything at
// hand"*).
//
// 🛑 WHAT IT REPLACES IS A LIST OF DEAD ENDS, every one of them met in a single sweep:
//
//   • `metadata_list` answers NAMES, of ONE kind, and only for kinds that live at the top. A
//     Template, a Form, a Composer, a Command — everything NESTED — is invisible to it, and it was
//     the only way to see what exists. Told "a Template was created", there was no call that could
//     find it.
//   • an id had to be mined out of `metadata_get` on the OWNER: the composer's out of the report's
//     answer, the module's out of `metadata_properties`. Both work and neither is navigation.
//
// A node here is `{id, kind, name, children}` — the id being what every other metadata_* argument
// is written in, so seeing something and acting on it are one step apart instead of three.
class ibMcpToolMetadataTree : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_tree"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString named = ibMcpNameOf(params);
		return named.IsEmpty()
			? wxString(ibMcpText("reading the metadata tree"))
			: wxString::Format(ibMcpText("reading what is under '%s'"), named);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("THE MAP: what this configuration holds, as a tree, with the id of every node. "
			"Start here rather than guessing - one call shows the objects, their attributes, "
			"tabular sections, forms, templates, commands and composers, each with the id every "
			"other tool takes. Ask it of the whole configuration, or of one object to see inside "
			"it - and with depth 1 for the overview, since the default walks two levels and most "
			"of that is the predefined attributes every catalog and document has. A node that does "
			"not apply as its owner currently stands says so - a catalog with no owner disables "
			"Owner, a turnovers register disables RecordType - so it can be told from a field that "
			"is really there. "
			"metadata_list answers names of one kind and only at the top level; this answers "
			"everything, including the nested kinds that have no list of their own.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgDepth(), ArgDeleted() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* from = nullptr;

		if (const ibDataValue* id = params.FindField(ArgId().Name()); id != nullptr && id->Kind() == ibDataKind::Number) {

			from = ibFindMetaObjectById(metaData, (ibMetaID)id->AsInt());
			if (from == nullptr) {
				refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %s."),
					id->AsNumber().ToString());
				return false;
			}
		}
		else {
			from = metaData->GetCommonMetaObject();
		}

		if (from == nullptr) {
			refusal = ibMcpText("This configuration has no root to walk.");
			return false;
		}

		// Two levels by default: an object and what it holds. Deep enough to act on, shallow
		// enough that asking about the configuration does not answer with all of it.
		//
		// ⚠ ZERO MEANS EVERYTHING to the caller and "go no further" to the walk, which is the same
		// number carrying two opposite meanings — so it is turned into -1 here, once, rather than
		// tested for at every level (found the first time it ran: depth 1 answered no children at
		// all, where 1 is precisely "the children of this node").
		const bool given = params.FindField(ArgDepth().Name()) != nullptr;
		const int asked = given ? (int)ArgDepth().Whole(params) : 2;
		const int depth = asked <= 0 ? -1 : asked;

		const bool withDeleted = ArgDeleted().Flag(params);

		std::vector<ibDataValue> children;
		Walk(from, depth, withDeleted, children);

		result.AddField(wxT("id"), ibDataValue::Int((s64)from->GetMetaID()));
		result.SetValue(wxT("kind"), from->GetClassName());
		result.SetValue(wxT("name"), from->GetName());
		result.AddField(wxT("children"), ibDataValue::Array(children));
		return true;
	}

private:

	// ⚠ DELETED NODES ARE NOT THERE unless they were asked for. A metaobject marked deleted is
	// still in the tree until the configuration is saved, and answering with it by default would
	// offer an id that every other tool refuses — but a caller looking at a tree that does not add
	// up needs to be able to see them, which is what `deleted` is for.
	//
	// ⭐ AND THE STATE OF A NODE IS SAID ON THE NODE. Both flags are answered only when they are
	// TRUE: a `disabled: false` on every one of six hundred lines is noise that hides the four that
	// matter, and the absence of the word is already the ordinary case.
	// `depth` counts the levels still to be drawn; -1 is "as many as there are".
	static void Walk(const ibValueMetaObject* node, int depth, bool withDeleted,
		std::vector<ibDataValue>& into)
	{
		if (node == nullptr || depth == 0)
			return;

		for (unsigned int idx = 0; idx < node->GetChildCount(); idx++) {

			const ibValueMetaObject* child = node->GetChild(idx);
			if (child == nullptr)
				continue;

			const bool deleted = child->IsDeleted();
			if (deleted && !withDeleted)
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->AddField(wxT("id"), ibDataValue::Int((s64)child->GetMetaID()));
			entry->SetValue(wxT("kind"), child->GetClassName());
			entry->SetValue(wxT("name"), child->GetName());

			if (deleted)
				entry->AddField(wxT("deleted"), ibDataValue::Bool(true));

			if (!child->IsEnabled())
				entry->AddField(wxT("disabled"), ibDataValue::Bool(true));

			// ⭐ THE SHORT LINE THE OBJECT ALREADY CARRIES. `Comment` is one of the three texts every
			// metaobject has and the only one meant to be read AT A GLANCE — what this thing is for,
			// beside its name, in the property list a person opens it in. The map is exactly where a
			// glance happens, and a tree of names alone cannot tell a live catalog from a probe left
			// over from an experiment. Answered when there is one, so nothing is added to a
			// configuration that does not use them.
			const wxString comment = child->GetComment();
			if (!comment.IsEmpty())
				entry->SetValue(wxT("comment"), comment);

			std::vector<ibDataValue> grand;
			Walk(child, depth < 0 ? -1 : depth - 1, withDeleted, grand);

			if (!grand.empty())
				entry->AddField(wxT("children"), ibDataValue::Array(grand));

			into.push_back(ibDataValue::Child(entry));
		}
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataTree);

//---------------------------------------------------------------------------
// metadata_list
//---------------------------------------------------------------------------
class ibMcpToolMetadataList : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_list"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString kind = ArgKind().Text(params);
		return kind.IsEmpty()
			? wxString(ibMcpText("looking through the configuration"))
			: wxString::Format(ibMcpText("looking at the %s objects"), kind);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Names of every metadata object of a kind in the open configuration.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgKind() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		const wxString kind = ArgKind().Text(params);
		if (kind.IsEmpty()) {
			refusal = ibMcpText("No kind named.");
			return false;
		}

		// A KIND THAT DOES NOT RESOLVE IS A REFUSAL, not an empty list: "there
		// are no catalogs" and "Catallog is not a word" are different answers,
		// and a caller acting on the first would go on to create one.
		if (ibResolveMetaKind(metaData, kind) == 0) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a kind of metadata object in this configuration."), kind);
			return false;
		}

		std::vector<ibDataValue> names;
		for (const wxString& name : ibListMetaObjects(metaData, kind))
			names.push_back(ibDataValue::String(name));

		result.SetValue(wxT("kind"), kind);
		result.AddField(wxT("names"), ibDataValue::Array(names));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataList);

//---------------------------------------------------------------------------
// metadata_get
//---------------------------------------------------------------------------
class ibMcpToolMetadataGet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_get"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		wxString named = ibMcpNameOf(params);
		if (named.IsEmpty())
			named = ArgName().Text(params);

		return named.IsEmpty()
			? wxString(ibMcpText("reading an object"))
			: wxString::Format(ibMcpText("reading '%s'"), named);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("One metadata object in full: its properties, its attributes with their types, "
			"its tabular sections and its children. Ask by id when you have one - the answer "
			"carries the id it was found by, so a follow-up needs no name.");
	}

	// 🛑 `name` WAS MISSING HERE AND READ BELOW. Call() looks it up, and the refusal when nothing
	// matches says *"Ask by id, or by kind and name together"* — while the undeclared-argument gate
	// answered `'metadata_get' takes no argument called 'name'` and never let the call through. The
	// road the error message points at was the one road that could not be taken (found 2026-09-01,
	// sweeping every tool). An argument a tool READS has to be an argument it DECLARES.
	const std::vector<ibMcpArgument>& Arguments() const override
	{
		// ⚠ NEITHER IS REQUIRED, because either road is enough — see ArgKindOptional. Call() says
		// which two shapes it accepts when it gets neither.
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgKindOptional(), ArgName() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* found = nullptr;

		// BY ID FIRST — it is the identity. A name is what a person types and
		// what a rename changes.
		if (const ibDataValue* id = params.FindField(ArgId().Name())) {
			if (id->Kind() == ibDataKind::Number) {
				const ibMetaID metaId = (ibMetaID)id->AsInt();
				found = ibFindMetaObjectById(metaData, metaId);
				if (found == nullptr) {
					refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %s."),
						id->AsNumber().ToString());
					return false;
				}
			}
		}

		if (found == nullptr) {

			const wxString kind = ArgKind().Text(params);
			const wxString name = ArgName().Text(params);

			if (kind.IsEmpty() || name.IsEmpty()) {
				refusal = ibMcpText("Ask by id, or by kind and name together.");
				return false;
			}

			found = ibFindMetaObject(metaData, kind, name);
			if (found == nullptr) {
				refusal = wxString::Format(ibMcpText("No %s is named '%s'."), kind, name);
				return false;
			}
		}

		// ⭐⭐ THE OBJECT, THEN ITS PROPERTIES, THEN WHAT IS UNDER IT — and the first two come from
		// the object's own methods, not from the serializer.
		//
		// This answered ENTIRELY through ibBuildMetaObjectNode, which is BuildDataNode — how a
		// configuration is STORED. Its keys are storage keys, it carries every internal name a
		// metatype happens to have, and a property a metatype forgot to write in its `WriteData`
		// is simply absent from the answer, silently. Max, 2026-09-01: *"you don't work with the
		// metadata as such — you create it by type, fill in the header, and then you get the
		// PROPERTIES and work with those."*
		//
		// So the header is said by the object (ibMcpSayObject), the properties are WALKED
		// (ibMcpSayProperties — so one added tomorrow is here tomorrow, with nothing edited), and
		// the stored form is kept beside them under `stored`, because the children — attributes,
		// tabular sections, forms — are a tree only the serializer walks whole.
		ibMcpSayObject(found, result, /*withText*/ true);
		ibMcpSayProperties(found, result);

		if (!ibBuildMetaObjectNode(found, result.Child(wxT("stored")))) {
			refusal = ibMcpText("The object could not describe itself.");
			return false;
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataGet);

//---------------------------------------------------------------------------
// metadata_create
//---------------------------------------------------------------------------
//
// THE SAME DOOR THE BUTTON USES. ibMetaData::CreateMetaObject is what
// ibConfigurationTree::NewItem calls when a person picks *Add* — naming,
// lifecycle hooks, the modified flag, all of it. Nothing is re-implemented here;
// what this adds is only the two things a click gets from its surroundings: WHO
// the parent is (a selection, for a person) and telling the tree afterwards.
//
// ⭐ THE GATE IS THE PARENT'S OWN ANSWER. `ResolveChild` / `FilterChild` is what
// a metaobject says about what may live inside it — the same question the
// designer's menu is built from. No list here decides what goes where, so a
// metatype that becomes legal somewhere becomes creatable here on the same day.
//
class ibMcpToolMetadataCreate : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_create"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("creating %s '%s'"),
			ArgKind().Text(params),
			ArgName().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Add a metadata object, exactly as the designer's Add command does. Answers with "
			"the new object in full, so its id and its empty fields are known without asking "
			"again. Refuses when the kind may not live under that parent.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgKind(), ArgParentId(), ArgName(), ArgNote(), ArgHelp(), ArgProperties() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		const wxString kind = ArgKind().Text(params);
		const ibClassID clsid = ibResolveMetaKind(metaData, kind);
		if (clsid == 0) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a kind of metadata object in this configuration."), kind);
			return false;
		}

		// WHERE IT GOES. No parent named means the top level, which is the
		// configuration's own root object — the same thing a click gets by
		// walking up from a group row.
		ibValueMetaObject* parent = nullptr;
		if (const ibDataValue* parentId = params.FindField(ArgParentId().Name())) {
			if (parentId->Kind() == ibDataKind::Number) {
				parent = ibFindMetaObjectById(metaData, (ibMetaID)parentId->AsInt());
				if (parent == nullptr) {
					refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %s."),
						parentId->AsNumber().ToString());
					return false;
				}
			}
		}

		if (parent == nullptr)
			parent = metaData->GetCommonMetaObject();

		if (parent == nullptr) {
			refusal = ibMcpText("This configuration has no root to add to.");
			return false;
		}

		// THE GATE, asked of the parent itself.
		if (!parent->FilterChild(clsid)) {
			refusal = wxString::Format(
				ibMcpText("A %s cannot be added to '%s'."), kind, parent->GetName());
			return false;
		}

		// ⭐ SOME OBJECTS ASK THE PERSON A QUESTION WHEN THEY ARE CREATED, and a
		// form is the first of them: it opens a dialog to be told WHICH KIND of
		// form it is. That is right for a click — the developer is standing there —
		// and impossible for a tool, which has no person and no main thread: the
		// modal box tripped wx's "only the main thread may do this" assertion and
		// took the call down (2026-08-30).
		//
		// The platform's own answer is the flag. `runObject = false` takes the PASTE
		// path — the road an object travels when it arrives with its decisions
		// already made — and asks nothing. So the answers come as arguments instead,
		// and the object is created the quiet way when they do.
		//
		// Narrow on purpose: without an answer to give, the ordinary road stands.
		// ⭐ "THE CALLER HAS ANSWERS" is what suppresses the dialog, and it is now asked in general
		// rather than of one property: a create that carries properties is a create nobody needs to
		// be asked about. It used to be `formType was passed`, which is the same rule written for
		// the single case somebody happened to need first.
		//
		// ⭐⭐ AND A NAME IS AN ANSWER TOO. The real question is not "were properties passed" but
		// "is this call still going to finish the object" — and a caller that asked for a name is,
		// because CreateMetaObject makes it under a generated one and the rename happens below.
		//
		// 🛑 OTHERWISE ONE ACT IS ANNOUNCED AS TWO: a watcher saw `created Catalog 'Catalog1'` and
		// then `renamed Catalog 'SwCat'` for a single create, the first line naming an object that
		// never existed under that name for any purpose (2026-09-01, reading the assistant's own
		// event log). A stage is a statement about a finished thing.
		const bool answered = params.FindChild(ArgProperties().Name()) != nullptr
			|| !ArgName().Text(params).IsEmpty();

		ibValueMetaObject* created = metaData->CreateMetaObject(clsid, parent, !answered);
		if (created == nullptr) {
			refusal = wxString::Format(ibMcpText("The %s could not be created."), kind);
			return false;
		}

		// ⭐⭐ THE PROPERTIES ARE THE OBJECT'S OWN, ASKED OF THE OBJECT THAT WAS JUST MADE.
		//
		// This used to be sixty lines about ONE property of ONE metatype: `formType` had its own
		// argument, its own schema entry, its own choice matching and its own three refusals. Every
		// other property a caller might want set at creation had none of that, and the next one
		// would have needed a second copy — which is the hardcode that drifts: a property renamed
		// in the platform leaves a tool naming something that no longer exists, and it compiles.
		//
		// So the tool knows only the HEADER — kind, parent, name — and everything past it is
		// handed over as a map and placed through the same door metadata_set uses. Which properties
		// exist, which words each accepts, and whether a value is legal are all the created
		// object's answers; the answer below reports its whole property list, so a caller learns
		// what it may set from the object rather than from this file.
		if (const ibDataNode* wanted = params.FindChild(ArgProperties().Name())) {

			std::vector<ibDataValue> refused;

			for (const auto& field : wanted->Fields()) {

				ibProperty* property = created->GetProperty(field.first);

				// SAID, NOT SWALLOWED: the object stands either way, and a caller that asked for a
				// property it did not get would go on believing it was set.
				wxString why;

				if (property == nullptr) {
					why = wxString::Format(ibMcpText("'%s' has no property called '%s'."),
						created->GetName(), field.first);
				}
				else {
					// One entry, in the shape ibMcpSetProperty reads — the same one metadata_set
					// hands it, so a word from a closed set, a relationship by name and a plain
					// value all behave here exactly as they do there.
					ibDataNode one;
					one.AddField(wxT("value"), field.second);

					ibDataNode said;
					ibMcpSetProperty(property, one, said, why);
				}

				if (why.IsEmpty())
					continue;

				auto entry = std::make_shared<ibDataNode>();
				entry->SetValue(wxT("property"), field.first);
				entry->SetValue(wxT("reason"), why);
				refused.push_back(ibDataValue::Child(entry));
			}

			if (!refused.empty())
				result.AddField(wxT("refused"), ibDataValue::Array(refused));
		}

		// ⚠ AND THE STEP THAT STANDS BESIDE THE DIALOG. For a form, choosing the kind and BUILDING
		// the initial layout are two statements in the same branch of the click path: skipping the
		// ask skipped the build too, and the form came out standing in the tree with nothing inside
		// it — "the stored layout is missing", from a form created a second earlier.
		//
		// Asked of the OWNER, exactly as the click path does once the person has answered. The
		// answers were ours; the building is still the platform's.
		if (answered && parent != nullptr) {
			if (ibValueMetaObjectGenericData* owner =
					parent->ConvertToType<ibValueMetaObjectGenericData>()) {
				if (ibValueMetaObjectFormBase* form =
						created->ConvertToType<ibValueMetaObjectFormBase>())
					owner->OnCreateFormObject(form);
			}
		}

		// The object was created under a generated name — CreateMetaObject guarantees one that does
		// not collide — and the name the caller asked for replaces it here, through the door that
		// checks the siblings and tells the designer's tree.
		const wxString name = ArgName().Text(params);
		if (!name.IsEmpty() && !metaData->RenameMetaObject(created, name)) {
			// The object stands; only the name did not take. Said out loud,
			// because a caller that asked for a name and got another one
			// would go looking for the wrong thing next.
			result.SetValue(wxT("nameRefused"), name);
		}

		// The rest of the header. Neither is a property, so neither can travel in the map.
		if (const ibDataValue* note = params.FindField(ArgNote().Name()))
			created->SetNoteContent(note->AsString());
		if (const ibDataValue* help = params.FindField(ArgHelp().Name()))
			created->SetHelpContent(help->AsString());

		// ⭐⭐ AND NOW IT IS RUN, WHICH IS WHAT MAKES IT ALIVE — the two phases the quiet road skips.
		//
		// 🛑 IT WAS FILLED IN AND ANNOUNCED WITHOUT EVER BEING RUN, and that is a half-created
		// object standing in the configuration. `runObject == false` is the road an object travels
		// when somebody else is going to finish it — a paste runs it (metaData.cpp, PasteObject:
		// OnBeforeRunMetaObject / OnAfterRunMetaObject around the fill) — and this code took that
		// road and never did.
		//
		// What it costs is invisible until the configuration CLOSES: the run event is what registers
		// an object's modules and its queryable source, so a catalog made this way had no manager
		// module on the register, and closing the configuration asserted in RemoveCommonModule and
		// then threw *"Object with id '…' is not exist"* — which is how a rollback died on it (Max,
		// 2026-09-01, rolling back exactly this sweep's catalog).
		//
		// ⚠ AS A NEW OBJECT (newObjectFlag), not as a load: the load-only flag gates the queryable
		// registration off, and this object IS new — the same reasoning, in those words, is written
		// at the paste's own call.
		if (answered) {

			if (!created->OnBeforeRunMetaObject(newObjectFlag)
				|| !created->OnAfterRunMetaObject(newObjectFlag)) {

				// It could not be brought to life — so it does not stand. Removed while it is still
				// reachable, which is the only moment it can be.
				metaData->RemoveMetaObject(created, parent);
				refusal = wxString::Format(ibMcpText("The %s was built but could not be started."), kind);
				return false;
			}

			// …AND ANNOUNCED, because only now is there a result to announce. Everything above is
			// part of ONE create: the properties, the name, the note, the run.
			//
			// ⚠ WITHOUT THIS AN MCP CREATE THAT CARRIED PROPERTIES WAS INVISIBLE: made, filled in,
			// saved with the configuration, and never drawn in any tree watching.
			metaData->MetaObjectStage(ibMetaDataNotifier::ibMetaStage::Created, created);
		}

		// ⚠ AN OBJECT THAT EXISTS IS NEVER REPORTED AS A FAILURE.
		//
		// This used to answer with a refusal when the description could not be
		// built — and the object was standing in the tree the whole time. A
		// caller reading "error" does the one thing that makes it worse: it tries
		// again, and now there are two. That happened, and the second one could
		// not even take the name because the first had it.
		//
		// So the outcome is stated in parts: what was made, what it is called,
		// and whether it could be described. Only the last of those is allowed
		// to be false.
		result.AddField(wxT("created"), ibDataValue::Bool(true));
		ibMcpSayObject(created, result);

		// ⭐ AND WHAT IT WILL TAKE, asked of the object that now exists. This is the half that lets
		// the tool know nothing about any particular kind: a caller creates, reads back the whole
		// property list with the words each one accepts, and sets the rest — without this file, or
		// the caller, holding a list that a rename in the platform would quietly invalidate.
		ibMcpSayProperties(created, result, wxEmptyString, /*editableOnly*/ true);

		// ⭐ SAID AT THE MOMENT OF CREATION, not discovered at save time. A register
		// created now is not yet valid — some document has to record into it — and
		// the useful moment to learn that is while you are still building it.
		std::vector<ibDataValue> complaints;
		for (const wxString& complaint : Complaints(created))
			complaints.push_back(ibDataValue::String(complaint));

		if (!complaints.empty()) {
			result.AddField(wxT("incomplete"), ibDataValue::Array(complaints));
			result.SetValue(wxT("nextStep"),
				ibMcpText("The object exists but is not finished - the lines above are the platform's own "
				  "words about what it is still missing."));
		}

		ibDataNode described;
		if (ibBuildMetaObjectNode(created, described)) {
			result.AddField(wxT("described"), ibDataValue::Bool(true));
			result.Child(wxT("object")) = described;
		}
		else {
			result.AddField(wxT("described"), ibDataValue::Bool(false));
			result.SetValue(wxT("note"),
				ibMcpText("Created, but it cannot describe itself yet - usually because it is not "
				  "complete. Use the id above to fill it in."));
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataCreate);

//---------------------------------------------------------------------------
// metadata_accepts
//---------------------------------------------------------------------------
//
// WHERE A THING CAN GO, asked instead of discovered by trying. Without this a
// caller learns the shape of the tree by attempting things and reading
// refusals — which works, and costs a round trip and a wrong step every time.
//
// ⭐ THE ANSWER IS THE PARENT'S OWN. Every kind the registry knows is offered to
// the object and it says yes or no (ResolveChild / FilterChild) — the same
// question the designer's Add menu is built from. No list here, so a metatype
// that becomes legal somewhere shows up here the day it does.
//
class ibMcpToolMetadataAccepts : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_accepts"); }

	// ⚠ ASKED THE WAY THE CALL WAS MADE. This said `checking what can go inside ''` for every
	// by-KIND call, because ibMcpNameOf resolves an `id` and there was none — the person watching
	// saw an empty quote where the subject should be. A line that names nothing is worse than no
	// line: it reads as the tool having lost track of what it was doing.
	wxString GetActivity(const ibDataNode& params) const override
	{
		const wxString kind = ArgAcceptsKind().Text(params);

		if (!kind.IsEmpty())
			return wxString::Format(ibMcpText("asking what a %s holds"), kind);

		const wxString named = ibMcpNameOf(params);
		return named.IsEmpty()
			? wxString(ibMcpText("checking what can go inside the configuration"))
			: wxString::Format(ibMcpText("checking what can go inside '%s'"), named);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("What can be added inside - attributes, tabular sections, dimensions, resources - "
			"with the properties it holds, and, for an existing object, what it is still missing "
			"to be valid. Ask it of a KIND before creating anything (an empty one is built, asked "
			"and dropped, so nothing is added to the configuration), or of an OBJECT by id. Ask "
			"this before building, instead of trying kinds until one is accepted.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgAcceptsId(), ArgAcceptsKind(), ArgParentId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		// ⭐⭐ ASKED OF A KIND, BY MAKING ONE. To learn what a Catalog holds there used to be no way
		// but to create a real Catalog in the configuration and look at it — so a caller had to
		// commit before it could ask, and anything that wanted the answer in advance needed a table
		// of kinds written here, which is the hardcode this whole file has been shedding.
		//
		// A type can answer for itself: build ONE, unattached, ask it, drop it. Max, 2026-09-01:
		// *"you need a type — you create an empty one, write it into your stream, and you have the
		// list of what it accepts."*
		//
		// ⚠ UNATTACHED IS THE WHOLE TRICK. Parent is null, so it is created through the value
		// registry and never enters the configuration: no metaID is spent, no lifecycle event
		// fires, the tree is told nothing and there is nothing to undo. It is a question, not a
		// creation — which is why this does not go through ibMetaData::CreateMetaObject.
		const wxString askedKind = ArgAcceptsKind().Text(params);

		if (!askedKind.IsEmpty()) {

			const ibClassID clsid = ibResolveMetaKind(metaData, askedKind);
			if (clsid == 0) {
				refusal = wxString::Format(
					ibMcpText("'%s' is not a kind of metadata object in this configuration."), askedKind);
				return false;
			}

			// 🛑 A METAOBJECT CANNOT BE BORN WITHOUT A PARENT, whatever the code looks like.
			//
			// ibValueMetaObject::Init reads as though it could — `if (parent == nullptr) return
			// true;` is written there — and that line is UNREACHABLE: it is guarded by
			// `paParams[0]->ConvertToValue(parent)`, which returns true only when the pointer came
			// out non-null. The dead branch is what sent me to pass a null slot, which is
			// dereferenced a frame earlier and took the designer down (2026-09-01,
			// ibValue::IsReference). Reading a branch is not the same as reaching it.
			//
			// ⭐⭐ AND THAT IS THE SHAPE, NOT A LIMITATION. Max, 2026-09-01: *"you can only create
			// tree-wise — first the catalog, then you hang the attributes on it. The schema simply
			// does not let you drift."* A kind is asked WHERE IT WOULD LIVE, because that is the
			// only place the question has an answer: an attribute under a catalog holds different
			// things from one under a register.
			//
			// So the sample is born under the parent the caller names — the ROOT when they name
			// none, which is where the top-level kinds live — and taken straight back out. Nothing
			// else is told: this does not go through ibMetaData::CreateMetaObject, so no id is
			// minted, no lifecycle event fires and the tree hears nothing. RemoveChild destroys it
			// and the configuration is as it was.
			ibValueMetaObject* root = nullptr;

			if (ArgParentId().Given(params)) {
				root = ibFindMetaObjectById(metaData, (ibMetaID)ArgParentId().Whole(params));
				if (root == nullptr) {
					refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %i."),
						(int)ArgParentId().Whole(params));
					return false;
				}
			}
			else {
				root = metaData->GetCommonMetaObject();
			}

			if (root == nullptr) {
				refusal = ibMcpText("This configuration has no root to build against.");
				return false;
			}

			ibValue* under[] = { root };
			ibValueMetaObject* sample = nullptr;

			try {
				sample = ibValue::CreateAndConvertObjectRef<ibValueMetaObject>(clsid, under, 1);
			}
			catch (...) {
				sample = nullptr;
			}

			// ⭐ A REFUSAL THAT SAYS WHERE TO ASK INSTEAD. The root accepts the top-level kinds; an
			// ATTRIBUTE or a tabular section lives inside an object, and the root turns it down —
			// which is not a failure of this verb but the answer to a question asked in the wrong
			// place.
			if (sample == nullptr) {
				refusal = wxString::Format(
					ibMcpText("'%s' does not go inside '%s'. Metadata is built tree-wise - a catalog first, "
					  "then the attributes on it - so name the parent it would live under with "
					  "`parent_id`, and this will answer for it there."),
					askedKind, root->GetName());
				return false;
			}

			// ⚠ AND IT NEEDS TO KNOW WHICH CONFIGURATION IT BELONGS TO — not to enter it, but to be
			// ASKED anything: ibValueMetaObject::IsEditable dereferences m_metaData without a guard,
			// and the property walk asks every property whether it can be edited. Told here, so the
			// sample can answer; it is still attached to nothing and still dropped below.
			sample->SetMetaData(metaData);

			result.SetValue(wxT("kind"), askedKind);
			ibMcpSayProperties(sample, result, wxEmptyString, /*editableOnly*/ true);

			// The same two lines the by-id path uses below: the registry is the list, the object
			// is the judge — and here the judge is one built for the question.
			std::vector<ibDataValue> accepts;
			for (const ibCtorAbstractType* ctor :
				ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_metadata)) {

				if (ctor != nullptr && sample->FilterChild(ctor->GetClassType()))
					accepts.push_back(ibDataValue::String(ctor->GetClassName()));
			}
			result.AddField(wxT("accepts"), ibDataValue::Array(accepts));

			// ⚠ TAKEN OUT THE WAY IT WENT IN. Init attached it to the root's OWNING child vector,
			// so the root is what destroys it — a wxDELETE here would free a node the vector still
			// holds. This is the same pairing ibMetaData::CreateMetaObject uses on a failed create.
			root->RemoveChild(sample);
			return true;
		}

		ibValueMetaObject* object = nullptr;
		if (const ibDataValue* id = params.FindField(ArgId().Name())) {
			if (id->Kind() == ibDataKind::Number) {
				object = ibFindMetaObjectById(metaData, (ibMetaID)id->AsInt());
				if (object == nullptr) {
					refusal = wxString::Format(ibMcpText("Nothing in this configuration has id %s."),
						id->AsNumber().ToString());
					return false;
				}
			}
		}

		if (object == nullptr)
			object = metaData->GetCommonMetaObject();

		if (object == nullptr) {
			refusal = ibMcpText("This configuration has no root.");
			return false;
		}

		// Ask the object about every metatype there is. The registry is the list;
		// the object is the judge.
		std::vector<ibDataValue> accepts;
		for (const ibCtorAbstractType* ctor :
			ibValue::GetListCtorsByType(ibCtorObjectType::ibCtorObjectType_object_metadata)) {

			if (ctor == nullptr)
				continue;
			if (object->FilterChild(ctor->GetClassType()))
				accepts.push_back(ibDataValue::String(ctor->GetClassName()));
		}

		std::vector<ibDataValue> settable;
		for (unsigned int i = 0; i < object->GetPropertyCount(); i++) {
			if (const ibProperty* property = object->GetProperty(i))
				settable.push_back(ibDataValue::String(property->GetName()));
		}

		// ⭐ AND THE HANDLERS THE MODULE MAY IMPLEMENT, with the arguments they
		// are called with. These are DECLARED on the object (ibEvent carries its
		// own argument names), so this is the platform saying what it will call
		// and with what — not a list anyone maintains, and not something a
		// caller should have to learn by writing a procedure and seeing whether
		// it ever runs.
		// A MODULE'S STANDARD HANDLERS — the same list the designer shows in
		// "Procedures and functions" for an empty module, and it lives on the
		// MODULE, not on the object that owns it (which is why asking a document
		// answers nothing).
		//
		// With the arguments, which is the half that matters here: a handler
		// whose signature does not match is not called, and nothing complains —
		// it simply never runs.
		std::vector<ibDataValue> handlers;

		const ibValueMetaObjectModuleBase* module = nullptr;

		if (object->ConvertToValue(module)) {

			for (size_t i = 0; i < module->GetDefaultProcedureCount(); i++) {

				std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
				const wxString name = module->GetDefaultProcedureName(i);
				entry->SetValue(wxT("name"), name);

				wxString signature = name + wxT("(");
				const std::vector<wxString> args = module->GetDefaultProcedureArgs(i);
				for (size_t a = 0; a < args.size(); a++) {
					if (a > 0) signature << wxT(", ");
					signature << args[a];
				}
				signature << wxT(")");

				entry->SetValue(wxT("signature"), signature);
				handlers.push_back(ibDataValue::Child(entry));
			}
		}

		if (!handlers.empty())
			result.AddField(wxT("handlers"), ibDataValue::Array(handlers));

		std::vector<ibDataValue> complaints;
		for (const wxString& complaint : Complaints(object))
			complaints.push_back(ibDataValue::String(complaint));

		result.SetValue(wxT("name"), object->GetName());
		result.AddField(wxT("accepts"), ibDataValue::Array(accepts));
		result.AddField(wxT("properties"), ibDataValue::Array(settable));
		if (!complaints.empty())
			result.AddField(wxT("incomplete"), ibDataValue::Array(complaints));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataAccepts);

//---------------------------------------------------------------------------
// metadata_delete
//---------------------------------------------------------------------------
//
// THE OTHER HALF OF CREATE, and it was missing — which is not a small gap. A
// caller that can only add has no way back from its own mistake, and a
// configuration that cannot be saved because of a half-built object left behind
// is a configuration nobody can work in until somebody clicks Delete by hand.
//
// It takes the SAME road the toolbar's Delete takes: announce it so whatever is
// showing the object stops showing it, then remove. Not a second remover — one
// verb, reachable from two places.
//
class ibMcpToolMetadataDelete : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_delete"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("deleting '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Remove an object from the configuration, with everything under it - exactly "
			"as pressing Delete in the tree would. Use it to undo a mistake: a half-built "
			"object left behind will block the configuration from being saved at all.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		const s32 asked = (s32)ArgId().Whole(params);
		if (asked <= 0) {
			refusal = ibMcpText("Pass the object's NodeId.");
			return false;
		}

		ibValueMetaObject* object = ibFindMetaObjectById(metaData, (ibMetaID)asked);
		if (object == nullptr) {
			refusal = wxString::Format(
				ibMcpText("Nothing in this configuration has id %i."), (int)asked);
			return false;
		}

		// SAID BEFORE IT IS TRUE, because afterwards there is nothing left to name.
		const wxString name = object->GetName();
		const s32      id = (s32)object->GetMetaID();

		// A CONFIGURATION IS NOT AN OBJECT, and deleting the root would take the
		// whole tree with it. There is no legitimate caller for that here.
		if (object->GetParent() == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' is the root of the configuration and cannot be removed."), name);
			return false;
		}

		metaData->RemoveMetaObject(object);
		metaData->Modify(true);

		result.AddField(wxT("deleted"), ibDataValue::Bool(true));
		result.SetValue(wxT("name"), name);
		result.AddField(wxT("id"), ibDataValue::Int((s64)id));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataDelete);

//===========================================================================
// The WRITING half — folded in from mcpToolEdit.cpp on 2026-09-01.
//
// "Edit" named a moment in time, not a subject: the file held metadata verbs, a
// module verb and a document binding at once. The module verbs went to
// mcpToolModule.cpp, the binding was found to be a duplicate of metadata_bind, and
// what remains is metadata — which is this file.
//===========================================================================

//---------------------------------------------------------------------------
// metadata_set
//---------------------------------------------------------------------------
class ibMcpToolMetadataSet : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_set"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		// ⚠ A PROPERTY'S VALUE IS NOT ALWAYS A STRING — a boolean, a number and a metaobject all
		// arrive here, and reading one as a string RAISES (ibDataValue::Expect). This line is
		// decoration; the raise used to escape and take the whole answer with it, leaving a caller
		// with HTTP 500 over a change that had already landed.
		wxString shown;

		if (const ibDataValue* value = params.FindField(ibMcpValueArgument().Name())) {
			switch (value->Kind()) {
				case ibDataKind::String: shown = value->AsString(); break;
				case ibDataKind::Bool:   shown = value->AsBool() ? wxT("yes") : wxT("no"); break;
				case ibDataKind::Number: shown = value->AsNumber().ToString(); break;
				default: break;
			}
		}

		return wxString::Format(ibMcpText("setting %s of '%s' to '%s'"),
			ArgProperty().Text(params),
			ibMcpNameOf(params),
			shown);
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Set one property of a metadata object - an attribute's type, a string's length, "
			"a register dimension's type, a synonym. Read the object with metadata_get first: "
			"the answer shows every property by name and what it currently holds, and a value "
			"you send back has the shape you saw there.");
	}

	// ⭐ TWO OF THESE BELONG TO THE DOOR, NOT TO THIS TOOL. `id` is read by
	// ibMcpObjectNamed and `value` by ibMcpSetProperty — both in mcpTool.cpp — so this file
	// used to declare, in its own words, names it does not read and cannot check. Now it says WHICH
	// arguments it takes and the door says what each is called and means, so the spelling, the
	// wording and the ORDER are one thing everywhere instead of one copy per tool.
	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ibMcpIdArgument(), ArgProperty(), ibMcpValueArgument(), ibMcpLanguageArgument() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		const wxString name = ArgProperty().Text(params);
		if (name.IsEmpty()) {
			refusal = ibMcpText("No property named.");
			return false;
		}

		// ⚠ A NAME IS NOT AN ORDINARY PROPERTY. Writing it straight through the
		// property would walk past the check that keeps two siblings from
		// sharing one — a configuration where that happens is broken in a way
		// nothing downstream expects. So the same question the designer asks on
		// a rename is asked here, whichever door the name comes through.
		if (name.IsSameAs(wxT("Name"), false)) {

			const wxString wanted = ibMcpValueArgument().Text(params);
			if (wanted.IsEmpty()) {
				refusal = ibMcpText("A name cannot be empty.");
				return false;
			}

			if (!metaData->RenameMetaObject(object, wanted)) {
				refusal = wxString::Format(
					ibMcpText("Another object of this kind is already called '%s'."), wanted);
				return false;
			}

			result.SetValue(wxT("property"), name);
			result.SetValue(wxT("value"), object->GetName());
			return true;
		}

		ibProperty* property = object->GetProperty(name);
		if (property == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' has no property called '%s'. metadata_get lists the ones it has."),
				object->GetName(), name);
			return false;
		}

		// ⭐ AN ENUMERATED PROPERTY IS SET BY ITS WORD.
		//
		// Periodicity, RegisterType, WriteMode — each holds a NUMBER, and each is
		// named in the language by a WORD ("WithinSecond"). Handing the word
		// straight to the property failed with "wrong value kind (expected 2, got
		// 4)" — a true sentence about the storage and a useless one to a caller,
		// who has no way to learn the number and should not have to: the number is
		// an implementation of the word.
		//
		// The choices are the PROPERTY'S OWN (GetEnumList) — the same list the
		// designer's inspector drops down — so a value added to an enumeration
		// tomorrow is settable here the day it is added, and a wrong word is
		// refused WITH the list of right ones.
		// ⭐⭐ ONE QUESTION TO THE PROPERTY — what may you be set to, and how many at once. It used to
		// be two `dynamic_cast`s, an enumeration and a list, which meant the gate reached exactly the
		// two families whose names it had been told; a relationship offers choices the same way and
		// was not among them. `None` is itself the answer "not chosen from a list", so nothing has to
		// be tested for first.
		ibPropertyChoiceList choices;

		if (const ibPropertyChoiceMode mode = property->GetValueList(choices);
			mode != ibPropertyChoiceMode::None) {

			const wxString word = ibMcpValueArgument().Text(params);

			// ⚠ TWO VOCABULARIES FOR ONE VALUE. The inspector's list reads
			// "Within second"; the language writes it `WithinSecond`, and that is
			// the form type_members answers with. A caller that learned the value
			// from the language was refused by the property — the same value,
			// spelled the way the other half of the platform spells it.
			//
			// Compared with the spaces taken out, so both are accepted without a
			// table mapping one to the other: a table would be one more thing to
			// keep in step, and it would be wrong for the first value added after it.
			auto same = [](const wxString& a, const wxString& b) {
				wxString l(a), r(b);
				l.Replace(wxT(" "), wxEmptyString);
				r.Replace(wxT(" "), wxEmptyString);
				return l.IsSameAs(r, false);
			};

			// The id is the steady way to name a choice — it is what the property stores, and
			// metadata_properties answers it beside the name for exactly this.
			const ibDataValue* asked = params.FindField(ibMcpValueArgument().Name());
			const bool byNumber = asked != nullptr && asked->Kind() == ibDataKind::Number;
			const long askedId = byNumber ? (long)asked->AsNumber().ToInt() : 0;

			for (unsigned int index = 0; index < choices.GetCount(); ++index) {

				// By id when a number came, otherwise by name or by label: the name is what a
				// script writes, the label is what the designer shows.
				const bool matches = byNumber
					? choices.GetId(index) == askedId
					: same(choices.GetName(index), word) || same(choices.GetLabel(index), word);

				if (!matches)
					continue;

				// ⭐ THE CHOICE CARRIES ITS OWN VALUE, so there is no per-family setter here any
				// more. An enumeration's is its number, a relationship's is the metadescription the
				// property built while listing; this places whichever it was handed.
				if (!ibMcpApplyByHand(property, choices.GetValue(index), refusal))
					return false;

				metaData->Modify(true);

				result.SetValue(wxT("property"), name);
				result.SetValue(wxT("value"), choices.GetLabel(index));
				return true;
			}

			wxString allowed;
			for (unsigned int index = 0; index < choices.GetCount(); ++index)
				allowed << (allowed.IsEmpty() ? wxT("") : wxT(", ")) << choices.GetLabel(index);

			refusal = allowed.IsEmpty()
				? wxString::Format(ibMcpText("'%s' is chosen from the configuration, and there is nothing "
				                     "of that kind in it yet."), name)
				: wxString::Format(ibMcpText("'%s' takes one of: %s."), name, allowed);
			return false;
		}

		// A VALUE IS EITHER A SCALAR OR A SHAPE. The parser puts scalars in the
		// field area and objects in the properties area as a child, so both are
		// looked for — a caller sending back what it read should not have to know
		// which of the two it is holding.
		ibDataValue value;
		if (const ibDataValue* scalar = params.FindField(ibMcpValueArgument().Name()))
			value = *scalar;
		else if (const ibDataNode* composite = params.FindChild(ibMcpValueArgument().Name()))
			value = ibDataValue::Child(std::make_shared<ibDataNode>(*composite));

		if (!property->SetNodeValue(value)) {
			refusal = wxString::Format(
				ibMcpText("'%s' would not take that value."), name);
			return false;
		}

		metaData->Modify(true);

		// ANSWERED WITH WHAT IT NOW HOLDS, read back through the property rather
		// than echoed from the request: what was asked for and what was taken are
		// different facts, and only the second one is true.
		result.SetValue(wxT("property"), name);
		result.AddField(wxT("value"), property->GetNodeValue());
		ibMcpReportComplaints(result, object);
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataSet);

//---------------------------------------------------------------------------
// metadata_set_type
//---------------------------------------------------------------------------
//
// WHY A VERB OF ITS OWN, when metadata_set exists.
//
// A type is the commonest edit there is — an attribute without one stores
// nothing — and it is the one property whose value is a STRUCTURE. Sending that
// structure back through the JSON view does not work and cannot be made to:
// the reader takes the type list from the node's PROPERTY area, while a JSON
// array comes back in the FIELD area, because the view flattens the two on the
// way out and guesses on the way in (serialize/jsonProvider.h says so plainly).
// The value went in, nothing landed, and no error was raised.
//
// So the shape stops being the caller's problem: it says "String, 20" and this
// builds the description. Which is a better door anyway — asking a caller to
// reproduce an engine's internal node was never good design, only convenient
// for me.
//
class ibMcpToolMetadataSetType : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_set_type"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("giving '%s' the type %s"),
			ibMcpNameOf(params),
			ArgType().Text(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Give an attribute, a dimension or a resource its type - in words: String with a "
			"length, Number with precision and scale, Date, Boolean, or a reference such as "
			"CatalogRef.Goods. type_list shows what names exist.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgId(), ArgType(), ArgTypeShape(), ArgLength(), ArgPrecision(), ArgScale(), ArgTypeId() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		// ⭐ FIND THE TYPE PROPERTY, DO NOT NAME IT. Most objects call it "Type",
		// but a Command calls its own "ParameterType" — and the hard-coded name
		// answered "'PrintForm' has no type to set" about an object that plainly
		// has one, which is a refusal that teaches the caller something false.
		//
		// So the name is tried first (it is right nearly always and costs one
		// lookup), and otherwise the object is ASKED: a type property is one that
		// HOLDS A TYPE DESCRIPTION, and an object almost always has exactly one.
		// Two would be a real ambiguity, so that case is refused BY NAME rather
		// than guessed — the caller can then say which.
		//
		// ⭐ THE QUESTION GOES TO THE VALUE, NOT TO THE PROPERTY'S CLASS. It used to
		// `dynamic_cast<ibPropertyType*>` — three times, one of them inside a loop over every
		// property of the object, so the search for "which one is about a type" was a search for a
		// class name. What is actually being looked for is a value that hides an ibTypeDescription,
		// and that is what the variant IS. Same move as the relationship above: the description
		// lives in the value, so the value answers.
		// `find_`, not `get_`: this walks every property of the object asking "are you the type
		// one", and all but one answer no. The raising form is for a shape already known.
		auto typeValueOf = [](ibProperty* candidate) -> ibVariantDataAttribute* {
			return candidate != nullptr ? candidate->find_cell_variant<ibVariantDataAttribute>() : nullptr;
		};

		ibProperty*              typeProperty = object->GetProperty(wxT("Type"));
		ibVariantDataAttribute*  typeValue    = typeValueOf(typeProperty);

		if (typeValue == nullptr) {

			typeProperty = nullptr;
			std::vector<wxString> found;

			for (unsigned int idx = 0; idx < object->GetPropertyCount(); idx++) {
				ibProperty* candidate = object->GetProperty(idx);
				if (ibVariantDataAttribute* carried = typeValueOf(candidate)) {
					typeProperty = candidate;
					typeValue    = carried;
					found.push_back(candidate->GetName());
				}
			}

			if (found.size() > 1) {
				wxString names;
				for (const wxString& name : found)
					names << (names.IsEmpty() ? wxT("") : wxT(", ")) << name;
				refusal = wxString::Format(
					ibMcpText("'%s' has more than one type to set (%s). Say which with metadata_set."),
					object->GetName(), names);
				return false;
			}
		}

		if (typeValue == nullptr) {
			refusal = wxString::Format(
				ibMcpText("'%s' has no type to set."), object->GetName());
			return false;
		}

		const wxString typeName = ArgType().Text(params);

		// A NAME RESOLVES TWO WAYS, and the configuration's own goes first: a
		// reference type ("CatalogRef.Goods") exists only against the tree that
		// declared it, while a primitive is registered process-wide.
		// ⭐ AN ID IS ACCEPTED TOO, AND IT IS THE STABLE HALF. A name like
		// "CatalogRef.Goods" carries the OBJECT'S NAME inside it, so a rename
		// breaks every stored reference to it — silently, because the string
		// still looks like a type. The class id does not move: for a reference
		// type it is constructive, its body being the identity of what it points
		// at. So a caller that already has the id (type_list gives it) should
		// use it, and a name stays the convenient way to say it once.
		// ⭐ THE WHOLE DESCRIPTION IS ANSWERED FIRST, and before a type NAME is resolved — because a
		// caller sending one has said everything, and demanding a name beside it would refuse the
		// composite case this road exists for.
		if (const ibDataNode* shape = params.FindChild(ArgTypeShape().Name())) {

			ibTypeDescription described;
			ibDataValue carried = ibDataValue::Child(std::make_shared<ibDataNode>(*shape));

			if (!ibTypeDescriptionMemory::ReadNode(carried, described, metaData)) {
				refusal = ibMcpText("That is not a type description this platform can read. Send back the "
					"shape metadata_get gives, with your changes in it.");
				return false;
			}

			typeValue->SetFromTypeDesc(described);
			if (!ibMcpApplyByHand(typeProperty, wxVariant(typeValue->Clone()), refusal))
				return false;

			metaData->Modify(true);

			ibDataValue placed;
			if (!ibTypeDescriptionMemory::WriteNode(placed,
					typeProperty->get_cell_variant<ibVariantDataAttribute>()->GetTypeDesc(),
					metaData)) {
				refusal = ibMcpText("The description was placed, but could not be read back to confirm it.");
				return false;
			}

			result.AddField(wxT("type"), placed);
			ibMcpReportComplaints(result, object->GetParent());
			return true;
		}

		ibClassID clsid = 0;
		if (const ibDataValue* typeId = params.FindField(ArgTypeId().Name())) {
			if (typeId->Kind() == ibDataKind::Number)
				clsid = (ibClassID)typeId->AsUInt();
		}

		if (clsid == 0) {
			if (const ibCtorMetaValueType* ctor = metaData->GetTypeCtor(typeName))
				clsid = ctor->GetClassType();
			else if (const ibCtorAbstractType* builtin = ibValue::GetAvailableCtor(typeName))
				clsid = builtin->GetClassType();
		}

		if (clsid == 0) {
			refusal = wxString::Format(
				ibMcpText("'%s' is not a type this configuration knows. type_list shows the names."),
				typeName);
			return false;
		}


		s32 length = 10, precision = 10, scale = 0;
		params.GetValue(wxT("length"), length);
		params.GetValue(wxT("precision"), precision);
		params.GetValue(wxT("scale"), scale);

		ibTypeDescription description;
		description.SetDefaultMetaType(clsid);

		// The qualifiers only mean something for the type they belong to; set
		// unconditionally they would silently rewrite a neighbour's.
		if (typeName.IsSameAs(wxT("String"), false))
			description.SetString((unsigned short)length);
		else if (typeName.IsSameAs(wxT("Number"), false))
			description.SetNumber((unsigned char)precision, (unsigned char)scale);

		// ⭐ WRITTEN THROUGH THE VALUE, and then the value is placed — the inspector's own sequence,
		// so whatever watches a property change sees this one. The variant is CLONED because the one
		// read out is the property's live data: mutating it and handing it back as "the new value"
		// is a write nobody was told about.
		typeValue->SetFromTypeDesc(description);
		if (!ibMcpApplyByHand(typeProperty, wxVariant(typeValue->Clone()), refusal))
			return false;

		metaData->Modify(true);

		// 🛑 READ BACK THROUGH THE PROPERTY, NOT THROUGH THE HANDLE WE WROTE WITH. `typeValue` is the
		// variant data as it was BEFORE the write; ApplyByHand placed a CLONE of it, so the property
		// now holds a different object and the local one is stale. Reading it reported
		// `length 56797, precision 221` for a reference type whose stored qualifiers are 10 / 10 / 0
		// — a plausible-looking answer that was simply somebody else's memory.
		//
		// ⭐ The point of reading back at all is to say what the property HOLDS rather than what was
		// asked for, and that is only true if the property is the thing asked.
		// ⭐⭐ AND SAID BY THE DESCRIPTION ITSELF. ibTypeDescriptionMemory::WriteNode is how a type is
		// written to a FILE — every type it holds, by NAME, with the qualifiers that belong to each
		// — and it is the reading that stays in step, because it is the one the format depends on.
		//
		// 🛑 THREE FIELDS CHOSEN BY EYE were what stood here: length, precision, scale. They are the
		// qualifiers of the two PRIMITIVE types and say nothing about the case this verb exists for
		// — a COMPOSITE type, several types at once — which came back reported as a length of 10.
		// The same shape the answer now carries is the shape metadata_set takes back, so a caller
		// can read a type and place it somewhere else without translating anything.
		const ibTypeDescription& now =
			typeProperty->get_cell_variant<ibVariantDataAttribute>()->GetTypeDesc();

		// ⚠ ASKED WHETHER IT COULD SAY ITSELF. The family answers that, and a dropped answer here
		// would report a type that was never written as an empty one.
		ibDataValue described;
		if (!ibTypeDescriptionMemory::WriteNode(described, now, metaData)) {
			refusal = wxString::Format(
				ibMcpText("'%s' was set, but the type could not be read back to confirm it."),
				object->GetName());
			return false;
		}
		result.AddField(wxT("type"), described);

		// …and whether the OWNER is content now. A type set on an attribute can
		// leave the object it belongs to still unstorable, and that is knowable
		// here rather than at the next save.
		ibMcpReportComplaints(result, object->GetParent());
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataSetType);


//---------------------------------------------------------------------------
// module_write
//---------------------------------------------------------------------------


//===========================================================================
// metadata_properties — folded in from mcpToolProperties.cpp on 2026-09-01.
//
// It stood alone in a file while the rest of the metadata verbs were spread over
// five others. The walk it was built around now lives in ibMcpSayProperties and
// serves everything that describes an object, so what is left here is the door.
//===========================================================================
//---------------------------------------------------------------------------
// metadata_properties
//---------------------------------------------------------------------------
class ibMcpToolMetadataProperties : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_properties"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("reading the properties of '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("Every property of an object with what it holds now, what kind of value it "
			"takes, and - when the value is one of a closed set - the exact words allowed. "
			"Ask this before metadata_set: a property that takes a word will refuse anything "
			"else, and the words are this object's, not guessable from its name.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ibMcpIdArgument(), ArgOnlyProperty(), ArgEditableOnly() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		// This is where the right shape was written first — absent told apart from missing. It
		// lives in ibMcpObjectNamed now, so all eight callsites have it, this one included:
		// the open-configuration test that stood here said the same words the helper says.
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		// Read through the same declarations that were published — no second spelling of
		// `property` or `editableOnly` exists to drift from the first.
		const wxString only = ArgOnlyProperty().Text(params);

		// The walk itself is ibMcpSayProperties (mcpTool.cpp) — the same one anything else that
		// describes an object uses, so a property added to a metatype shows up wherever objects are
		// described and not only here.
		ibMcpSayObject(object, result);
		ibMcpSayProperties(object, result, only, ArgEditableOnly().Flag(params));

		if (result.GetValue<s32>(wxT("count")) == 0)
			result.SetValue(wxT("note"), only.IsEmpty()
				? ibMcpText("This object has no properties of its own.")
				: ibMcpText("It has no property of that name. Omit `property` to see the ones it has."));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataProperties);

//---------------------------------------------------------------------------
// metadata_used_by
//---------------------------------------------------------------------------
//
// ⭐⭐ THE QUESTION THAT HAD NO VERB: WHO POINTS AT THIS. Everything here answers FORWARD — what an
// object holds, what is under it, what it declares — and a configuration is read forward perfectly
// well by a person who wrote it. Somebody arriving into a base they did not build asks the other
// way round: *what breaks if I rename this*, *is this catalogue used at all*, *where did this
// register get filled from*. Nothing answered it, so the honest options were to read every object
// or to guess (measured on this server, 2026-09-02, building a warehouse application blind).
//
// ⭐ AND IT IS EXACT WHERE IT MATTERS, because a dynamic type carries the metaID as the BODY of its
// clsid (clsid.h — `metaID_from_clsid`). A reference to a catalogue is not a NAME stored somewhere:
// it is a number, so the type half of this answer is not a search at all. That is what makes the
// difference between "these mention the word" and "these would stop compiling".
//
// The text half is a search and says so: modules and composer queries are scanned for the object's
// NAME as a whole word. It is how a script names things, so it finds real uses; it also finds a
// comment that happens to say it, which is why the two halves are reported apart rather than added
// into one number a caller would trust equally.
class ibMcpToolMetadataUsedBy : public ibMcpTool {
public:

	wxString GetName() const override { return wxT("metadata_used_by"); }

	wxString GetActivity(const ibDataNode& params) const override
	{
		return wxString::Format(ibMcpText("looking for what uses '%s'"), ibMcpNameOf(params));
	}

	wxString GetDescription() const override
	{
		return ibMcpText("WHO POINTS AT THIS OBJECT - the backward question, which nothing else here "
			"answers. Two kinds of answer, kept apart: `types` are attributes, dimensions and "
			"resources DECLARED of this type, which is exact (a reference carries the object's id, "
			"not its name) and is what would break if it went; `mentions` are modules and composer "
			"queries naming it as a whole word, which is a search and can find a comment. Ask it "
			"before renaming or deleting anything, and to learn how a base you did not build hangs "
			"together.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ibMcpIdArgument() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, wxString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		const ibClassID target = (ibClassID)object->GetMetaID();
		const wxString name = object->GetName();

		std::vector<ibDataValue> types, mentions;

		for (ibValueMetaObject* other : metaData->GetAnyArrayObject<ibValueMetaObject>(true)) {

			if (other == nullptr || other == object)
				continue;

			// ⚠ ITS OWN INSIDES ARE NOT A USE. A catalogue's `Ref` and `Parent` are of its own type
			// BY CONSTRUCTION — every object in the tree would report two of them — and a register's
			// dimensions belong to the register the same way. What the question means is who ELSE
			// points here, so anything living under the object is skipped (measured on the first
			// live answer, 2026-09-02: the two loudest lines were the object describing itself).
			bool inside = false;
			for (const ibValueMetaObject* owner = other->GetParent();
				 owner != nullptr && !inside; owner = owner->GetParent())
				inside = owner == object;

			if (inside)
				continue;

			// --- DECLARED OF THIS TYPE. Every attribute-shaped thing answers with a type
			// description — an attribute, a dimension, a resource, a command's parameter, a common
			// attribute — so one cast covers the lot and a metatype added later is covered too.
			// ⚠ ConvertToValue, NOT ConvertToType — this is a QUESTION asked of every object in the
			// tree, and the latter is an assertion that raises on a miss outside the designer
			// (value_cast.h, _USE_CONTROL_VALUECAST). Both unwrap a reference-held value; only this
			// one answers "no".
			const ibValueMetaObjectAttributeBase* column = nullptr;
			if (other->ConvertToValue(column)) {

				for (const ibClassID& clsid : column->GetTypeDesc().m_listTypeClass) {

					// ⚠ THE KIND FIRST. Only a CONSTRUCTIVE id carries a metaID; a static one's body
					// is a name hash, and reading a hash as an id finds a match roughly never and
					// wrongly once (clsid.h says exactly this).
					if (clsid_kind(clsid) < ibClassKind_Reference)
						continue;

					if (metaID_from_clsid(clsid) != target)
						continue;

					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					ibMcpSayObject(other, *entry);
					if (const ibValueMetaObject* owner = other->GetParent())
						entry->SetValue(wxT("in"), owner->GetName());

					types.push_back(ibDataValue::Child(entry));
					break;
				}
			}

			// --- NAMED IN A TEXT. What a script and a query say is the other half of "used", and
			// it is the half a rename actually breaks: the type reference above follows the object
			// silently, the text does not.
			wxString text, where;

			const ibValueMetaObjectModuleBase* module = nullptr;
			const ibValueMetaObjectComposer* composer = nullptr;

			if (other->ConvertToValue(module)) {
				text = module->GetModuleText();
				where = ibMcpText("script");
			}
			else if (other->ConvertToValue(composer)) {
				text = composer->GetCompositionDesc().m_query;
				where = ibMcpText("query");
			}

			if (text.IsEmpty())
				continue;

			const wxString line = ibMcpLineNaming(text, name);
			if (line.IsEmpty())
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			ibMcpSayObject(other, *entry);
			if (const ibValueMetaObject* owner = other->GetParent())
				entry->SetValue(wxT("in"), owner->GetName());
			entry->SetValue(wxT("as"), where);
			entry->SetValue(wxT("line"), line);

			mentions.push_back(ibDataValue::Child(entry));
		}

		ibMcpSayObject(object, result);
		result.AddField(wxT("types"), ibDataValue::Array(types));
		result.AddField(wxT("mentions"), ibDataValue::Array(mentions));

		// ⭐ NOTHING IS AN ANSWER, AND IT IS THE ONE WORTH SAYING OUT LOUD: this object can be
		// renamed or removed without anything else noticing. An empty pair of lists says that only
		// to a reader who already knows what the lists mean.
		if (types.empty() && mentions.empty())
			result.SetValue(wxT("note"), wxString::Format(
				ibMcpText("Nothing in this configuration declares a field of type '%s' or names it in "
				  "a script or a query. It can be renamed or removed freely - and if it was meant "
				  "to be in use, that is the finding."), name));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataUsedBy);
