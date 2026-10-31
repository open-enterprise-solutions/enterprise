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

#include "frmserver/mcp/mcpTool.h"
#include "frmserver/mcp/tools/mcpToolMetadata.h"
#include "backend/metaCollection/attribute/metaAttributeObject.h"   // anything that DECLARES a type
#include "backend/metaCollection/metaComposerObject.h" // …and a query is the other place a name is said
#include "backend/metaCollection/metaIntrospect.h"
#include "backend/metaCollection/metaModuleObject.h"   // the standard handlers a module may implement
#include "backend/metaCollection/metaObject.h"
#include "backend/metadataConfiguration.h"
#include "backend/propertyManager/propertyObject.h"   // GetProperty(idx) — what can be set
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // how a field is chosen — what metadata_used_by reads
#include "core/description/sourceDescription.h"       // a source's path — …and this

namespace {

using namespace ibMcpMetadata;

// ⚠ THE STORED NODE, ASKED FOR RATHER THAN GIVEN. It is how the configuration is SERIALISED - every
// internal field, and a base64 blob (Interface, Roles, Composition) on every predefined attribute -
// so reading one synonym off a catalogue used to cost the whole storage form of the object
// (measured while driving this server, 2026-09-03). What a caller needs is above it; whoever wants
// the node still has it, by name.
const ibArg& ArgStored()
{
	static const ibArg s_a(ibT("stored"), ibArg::Kind::Flag,
		ibMcpText("Add the whole serialised node beside the properties - the storage form, including "
			  "the base64 blobs. Off by default: it is several times the size of the answer and "
			  "nothing in it is set through this server."));
	return s_a;
}

const ibArg& ArgOnlyProperty()
{
	static const ibArg s_property(ibT("property"), ibArg::Kind::Text,
		ibMcpText("One property by name. Omit for all of them."));
	return s_property;
}

const ibArg& ArgEditableOnly()
{
	static const ibArg s_editableOnly(ibT("editableOnly"), ibArg::Kind::Flag,
		ibMcpText("Leave out the ones that cannot be changed - usually most of the answer."));
	return s_editableOnly;
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
	static const ibArg s_a(ibT("kind"), ibArg::Kind::Text,
		ibMcpText("The kind, spelled the way a script writes it: Catalog, Document, Enumeration, "
			  "InformationRegister, AccumulationRegister, Report, DataProcessor... "
			  "Not needed when you ask by id."));
	return s_a;
}

const ibArg& ArgDepth()
{
	static const ibArg s_a(ibT("depth"), ibArg::Kind::Whole,
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
	static const ibArg s_a(ibT("deleted"), ibArg::Kind::Flag,
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

	ibString GetName() const override { return ibT("metadata_tree"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		const ibString named = ibMcpNameOf(params);
		return named.IsEmpty()
			? ibString(ibMcpText("reading the metadata tree"))
			: ibString::Format(ibMcpText("reading what is under '%s'"), named);
	}

	ibString GetDescription() const override
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

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* from = nullptr;

		if (auto id = params.FindField(ArgId().Name()); id && id->Kind() == ibDataKind::Number) {

			from = ibFindMetaObjectById(metaData, (ibMetaID)id->AsInt());
			if (from == nullptr) {
				refusal = ibString::Format(ibMcpText("Nothing in this configuration has id %s."),
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
		const bool given = params.Has(ArgDepth().Name());
		const int asked = given ? (int)ArgDepth().Whole(params) : 2;
		const int depth = asked <= 0 ? -1 : asked;

		const bool withDeleted = ArgDeleted().Flag(params);

		std::vector<ibDataValue> children;
		Walk(from, depth, withDeleted, children);

		result.AddField(ibT("id"), ibDataValue::Int((std::int64_t)from->GetMetaID()));
		result.SetValue(ibT("kind"), from->GetClassName());
		result.SetValue(ibT("name"), from->GetName());

		// The node asked ABOUT answers for itself the same way its children do (see Walk).
		const ibString comment = from->GetComment();
		if (!comment.IsEmpty())
			result.SetValue(ibT("comment"), comment);
		if (!from->GetNoteContent().IsEmpty())
			result.AddField(ibT("noted"), ibDataValue::Bool(true));

		result.AddField(ibT("children"), ibDataValue::Array(children));
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
			entry->AddField(ibT("id"), ibDataValue::Int((std::int64_t)child->GetMetaID()));
			entry->SetValue(ibT("kind"), child->GetClassName());
			entry->SetValue(ibT("name"), child->GetName());

			if (deleted)
				entry->AddField(ibT("deleted"), ibDataValue::Bool(true));

			if (!child->IsEnabled())
				entry->AddField(ibT("disabled"), ibDataValue::Bool(true));

			// ⭐ THE SHORT LINE THE OBJECT ALREADY CARRIES. `Comment` is one of the three texts every
			// metaobject has and the only one meant to be read AT A GLANCE — what this thing is for,
			// beside its name, in the property list a person opens it in. The map is exactly where a
			// glance happens, and a tree of names alone cannot tell a live catalog from a probe left
			// over from an experiment. Answered when there is one, so nothing is added to a
			// configuration that does not use them.
			const ibString comment = child->GetComment();
			if (!comment.IsEmpty())
				entry->SetValue(ibT("comment"), comment);

			// ⭐ AND THAT THERE IS MORE TO READ HERE. A note is the long text — why this exists, what
			// was tried and rejected — and it does not belong on a map; but its EXISTENCE does, and it
			// was the one thing the map did not say. Two catalogues left over from an experiment,
			// one of them carrying "Delete on sight", were indistinguishable from working objects: the
			// only way to find them was to open all twenty-two, one at a time (measured 2026-09-02).
			// The flag costs a word and turns note_read from something a reader must think to try into
			// something the tree tells them to do.
			if (!child->GetNoteContent().IsEmpty())
				entry->AddField(ibT("noted"), ibDataValue::Bool(true));

			std::vector<ibDataValue> grand;
			Walk(child, depth < 0 ? -1 : depth - 1, withDeleted, grand);

			if (!grand.empty())
				entry->AddField(ibT("children"), ibDataValue::Array(grand));

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

	ibString GetName() const override { return ibT("metadata_list"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		const ibString kind = ArgKind().Text(params);
		return kind.IsEmpty()
			? ibString(ibMcpText("looking through the configuration"))
			: ibString::Format(ibMcpText("looking at the %s objects"), kind);
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Every metadata object of a kind in the open configuration, each with the "
			"NodeId the other verbs are addressed by - metadata_get, metadata_delete, "
			"metadata_rename and the rest all take an id, and this is where one comes from.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ArgKind() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		const ibString kind = ArgKind().Text(params);
		if (kind.IsEmpty()) {
			refusal = ibMcpText("No kind named.");
			return false;
		}

		// A KIND THAT DOES NOT RESOLVE IS A REFUSAL, not an empty list: "there
		// are no catalogs" and "Catallog is not a word" are different answers,
		// and a caller acting on the first would go on to create one.
		if (ibResolveMetaKind(metaData, kind) == 0) {
			refusal = ibString::Format(
				ibMcpText("'%s' is not a kind of metadata object in this configuration."), kind);
			return false;
		}

		// ⭐⭐ THE LIST ANSWERS WITH WHAT THE NEXT VERB TAKES. It used to answer bare NAMES, while
		// every verb it leads into — metadata_get, metadata_delete, metadata_rename, section_*,
		// predefined_* — is addressed by NodeId; so listing a kind and then acting on one of its
		// objects cost a THIRD call to metadata_get purely to learn the id. Measured on this
		// server 2026-09-05, deleting one leftover module.
		//
		// 🛑 And three places in the corpus already told the caller "metadata_list gives it"
		// (mcpTool.cpp, mcpToolPredefined.cpp, mcpToolSection.cpp) — the promise was written, the
		// answer was not. A projection the neighbouring verb cannot eat is not a shorter answer,
		// it is a round trip charged to every caller.
		//
		// The SHAPE is metadata_tree's, entry for entry, because a list of one kind and a tree of
		// all of them are the same question asked at two depths.
		std::vector<ibDataValue> objects;
		for (const ibValueMetaObject* object : ibListMetaObjects(metaData, kind)) {

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			entry->AddField(ibT("id"), ibDataValue::Int((std::int64_t)object->GetMetaID()));
			entry->SetValue(ibT("name"), object->GetName());

			// The glance line and the flag that says there is more to read — the same two the map
			// carries, and for the same reason: a name alone cannot tell a live object from a
			// probe left over from an experiment.
			const ibString comment = object->GetComment();
			if (!comment.IsEmpty())
				entry->SetValue(ibT("comment"), comment);

			if (!object->GetNoteContent().IsEmpty())
				entry->AddField(ibT("noted"), ibDataValue::Bool(true));

			if (!object->IsEnabled())
				entry->AddField(ibT("disabled"), ibDataValue::Bool(true));

			objects.push_back(ibDataValue::Child(entry));
		}

		result.SetValue(ibT("kind"), kind);
		result.AddField(ibT("objects"), ibDataValue::Array(objects));
		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataList);

//---------------------------------------------------------------------------
// metadata_get
//---------------------------------------------------------------------------
class ibMcpToolMetadataGet : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("metadata_get"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		ibString named = ibMcpNameOf(params);
		if (named.IsEmpty())
			named = ArgName().Text(params);

		return named.IsEmpty()
			? ibString(ibMcpText("reading an object"))
			: ibString::Format(ibMcpText("reading '%s'"), named);
	}

	ibString GetDescription() const override
	{
		return ibMcpText("One metadata object: its properties, its attributes with their types, its "
			"tabular sections and its children. Ask by id when you have one - the answer carries the "
			"id it was found by, so a follow-up needs no name.\n"
			"`property` narrows it to ONE property, which is what most reads actually want; `stored` "
			"adds the whole serialised node beside it - every internal field and a base64 blob per "
			"predefined attribute - and is off unless asked for.");
	}

	// 🛑 `name` WAS MISSING HERE AND READ BELOW. Call() looks it up, and the refusal when nothing
	// matches says *"Ask by id, or by kind and name together"* — while the undeclared-argument gate
	// answered `'metadata_get' takes no argument called 'name'` and never let the call through. The
	// road the error message points at was the one road that could not be taken (found 2026-09-01,
	// sweeping every tool). An argument a tool READS has to be an argument it DECLARES.
	// ⭐⭐ ONE VERB FOR "READ A PROPERTY", not two that differ by which one takes the argument.
	// `metadata_get {id, property}` was the first thing I reached for while driving this server and
	// it was refused: the narrow read lived on `metadata_properties`, and the names invite exactly
	// the wrong guess — *get* sounds like the reader of one thing, *properties* like the lister of
	// all of them, and they are the other way round. The refusal cost one call and taught the
	// vocabulary, which is the cheap failure; but a road that both names point at should exist
	// rather than be learned. Both verbs now take `property`, and both answer the same shape.
	const std::vector<ibMcpArgument>& Arguments() const override
	{
		// ⚠ NEITHER OF THE FINDERS IS REQUIRED, because either road is enough — see
		// ArgKindOptional. Call() says which two shapes it accepts when it gets neither.
		static const std::vector<ibMcpArgument> s_arguments = {
			ArgId(), ArgKindOptional(), ArgName(), ArgOnlyProperty(), ArgStored() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* found = nullptr;

		// BY ID FIRST — it is the identity. A name is what a person types and
		// what a rename changes.
		if (auto id = params.FindField(ArgId().Name())) {
			if (id->Kind() == ibDataKind::Number) {
				const ibMetaID metaId = (ibMetaID)id->AsInt();
				found = ibFindMetaObjectById(metaData, metaId);
				if (found == nullptr) {
					refusal = ibString::Format(ibMcpText("Nothing in this configuration has id %s."),
						id->AsNumber().ToString());
					return false;
				}
			}
		}

		if (found == nullptr) {

			const ibString kind = ArgKind().Text(params);
			const ibString name = ArgName().Text(params);

			if (kind.IsEmpty() || name.IsEmpty()) {
				refusal = ibMcpText("Ask by id, or by kind and name together.");
				return false;
			}

			found = ibFindMetaObject(metaData, kind, name);
			if (found == nullptr) {
				refusal = ibString::Format(ibMcpText("No %s is named '%s'."), kind, name);
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
		const ibString only = ArgOnlyProperty().Text(params);

		ibMcpSayObject(found, result, /*withText*/ only.IsEmpty());
		ibMcpSayProperties(found, result, only);

		// ⚠ NAMING ONE PROPERTY MEANS THAT PROPERTY. The stored node is the whole object however
		// narrow the question was, so asking for a synonym and being handed the serialised form of
		// the catalogue is the opposite of what was asked.
		if (only.IsEmpty() && ArgStored().Flag(params)) {
			if (!ibBuildMetaObjectNode(found, result.Child(ibT("stored")))) {
				refusal = ibMcpText("The object could not describe itself.");
				return false;
			}
		}

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataGet);


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

	ibString GetName() const override { return ibT("metadata_properties"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("reading the properties of '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("Every property of an object with what it holds now, what kind of value it "
			"takes, and - when the value is one of a closed set - the exact words allowed. "
			"Ask this before metadata_set: a property that takes a word will refuse anything "
			"else, and the words are this object's, not guessable from its name.\n"
			"`metadata_get` with a `property` answers the same thing - either name reaches the "
			"narrow read, so neither has to be the one you remembered.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = {
			ibMcpIdArgument(), ArgOnlyProperty(), ArgEditableOnly() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		// This is where the right shape was written first — absent told apart from missing. It
		// lives in ibMcpObjectNamed now, so all eight callsites have it, this one included:
		// the open-configuration test that stood here said the same words the helper says.
		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		// Read through the same declarations that were published — no second spelling of
		// `property` or `editableOnly` exists to drift from the first.
		const ibString only = ArgOnlyProperty().Text(params);

		// The walk itself is ibMcpSayProperties (mcpTool.cpp) — the same one anything else that
		// describes an object uses, so a property added to a metatype shows up wherever objects are
		// described and not only here.
		ibMcpSayObject(object, result);
		ibMcpSayProperties(object, result, only, ArgEditableOnly().Flag(params));

		if (result.GetValue<std::int32_t>(ibT("count")) == 0)
			result.SetValue(ibT("note"), only.IsEmpty()
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
// ⭐ AND IT IS EXACT WHERE IT MATTERS, because a dynamic type carries the metaID in the low bits of its
// clsid (clsid.h — `clsid_metaID`). A reference to a catalogue is not a NAME stored somewhere:
// it is a number, so the type half of this answer is not a search at all. That is what makes the
// difference between "these mention the word" and "these would stop compiling".
//
// The text half is a search and says so: modules and composer queries are scanned for the object's
// NAME as a whole word. It is how a script names things, so it finds real uses; it also finds a
// comment that happens to say it, which is why the two halves are reported apart rather than added
// into one number a caller would trust equally.
class ibMcpToolMetadataUsedBy : public ibMcpTool {
public:

	ibString GetName() const override { return ibT("metadata_used_by"); }

	ibString GetActivity(const ibDataNode& params) const override
	{
		return ibString::Format(ibMcpText("looking for what uses '%s'"), ibMcpNameOf(params));
	}

	ibString GetDescription() const override
	{
		return ibMcpText("WHO POINTS AT THIS OBJECT - the backward question, which nothing else here "
			"answers. Three kinds of answer, kept apart: `types` are attributes, dimensions and "
			"resources DECLARED of this type, which is exact (a reference carries the object's id, "
			"not its name) and is what would break if it went; `links` are fields whose CHOICE names "
			"it - a link by type, a choice parameter, or the field a parameter takes its value from - "
			"exact too, and removed it leaves that row narrowing nothing; `mentions` are modules and "
			"composer queries naming it as a whole word, which is a search and can find a comment. Ask "
			"it before renaming or deleting anything, and to learn how a base you did not build hangs "
			"together.");
	}

	const std::vector<ibMcpArgument>& Arguments() const override
	{
		static const std::vector<ibMcpArgument> s_arguments = { ibMcpIdArgument() };
		return s_arguments;
	}

	bool Call(const ibDataNode& params, ibDataNode& result, ibString& refusal) const override
	{
		ibMetaData* metaData = OpenConfiguration(refusal);
		if (metaData == nullptr)
			return false;

		ibValueMetaObject* object = ibMcpObjectNamed(params, refusal);
		if (object == nullptr)
			return false;

		const ibClassID target = (ibClassID)object->GetMetaID();
		const ibString name = object->GetName();

		// Does this id name the object asked about, or something standing under it — a catalog's field goes
		// where the catalog goes.
		const auto namesIt = [metaData, object](ibMetaID id) {
			for (const ibValueMetaObject* at = id > 0 ? metaData->FindAnyObjectByFilter(id, true) : nullptr;
				 at != nullptr; at = at->GetParent()) {
				if (at == object)
					return true;
			}
			return false;
		};
		const auto pathNamesIt = [&namesIt](const ibSourceDescription& path) {
			for (const ibSourceHop& hop : path.m_listSource) {
				if (namesIt(hop.m_id))
					return true;
			}
			return false;
		};

		std::vector<ibDataValue> types, mentions, links;

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

					if (clsid_metaID(clsid) != target)
						continue;

					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					ibMcpSayObject(other, *entry);
					if (const ibValueMetaObject* owner = other->GetParent())
						entry->SetValue(ibT("in"), owner->GetName());

					types.push_back(ibDataValue::Child(entry));
					break;
				}

				// --- NAMED BY A CHOICE. A link by type and a parameter row hold fields by id, at either end
				// of the row, and neither half here looked at them: asked about the contracts' `Export`, this
				// answered "remove it freely" while a document's choice of contracts narrowed by it
				// (2026-09-24). Removed, such a row narrows nothing — which is worth knowing BEFORE.
				const auto say = [&](const ibString& as, const ibString& parameter) {
					std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
					ibMcpSayObject(other, *entry);
					if (const ibValueMetaObject* owner = other->GetParent())
						entry->SetValue(ibT("in"), owner->GetName());
					entry->SetValue(ibT("as"), as);
					if (!parameter.IsEmpty())
						entry->SetValue(ibT("parameter"), parameter);
					links.push_back(ibDataValue::Child(entry));
				};

				if (pathNamesIt(column->GetTypeLink().m_source))
					say(ibMcpText("link by type"), ibString());

				for (const ibChoiceParameterRowDescription& row : column->GetChoiceParameters().m_rows) {
					const ibString parameter = ibChoiceTargetFieldName(column, row.m_parameter);
					if (namesIt(row.m_parameter))
						say(ibMcpText("choice parameter"), parameter);
					else if (pathNamesIt(row.m_source))
						say(ibMcpText("value of a choice parameter"), parameter);
				}
			}

			// --- NAMED IN A TEXT. What a script and a query say is the other half of "used", and
			// it is the half a rename actually breaks: the type reference above follows the object
			// silently, the text does not.
			ibString text, where;

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

			const ibString line = ibMcpLineNaming(text, name);
			if (line.IsEmpty())
				continue;

			std::shared_ptr<ibDataNode> entry = std::make_shared<ibDataNode>();
			ibMcpSayObject(other, *entry);
			if (const ibValueMetaObject* owner = other->GetParent())
				entry->SetValue(ibT("in"), owner->GetName());
			entry->SetValue(ibT("as"), where);
			entry->SetValue(ibT("line"), line);

			mentions.push_back(ibDataValue::Child(entry));
		}

		ibMcpSayObject(object, result);
		result.AddField(ibT("types"), ibDataValue::Array(types));
		result.AddField(ibT("mentions"), ibDataValue::Array(mentions));
		result.AddField(ibT("links"), ibDataValue::Array(links));

		// ⭐ NOTHING IS AN ANSWER, AND IT IS THE ONE WORTH SAYING OUT LOUD: this object can be
		// renamed or removed without anything else noticing. Empty lists say that only to a reader who
		// already knows what the lists mean.
		if (types.empty() && mentions.empty() && links.empty())
			result.SetValue(ibT("note"), ibString::Format(
				ibMcpText("Nothing in this configuration declares a field of type '%s', names it in "
				  "a script or a query, or chooses by it. It can be renamed or removed freely - and if "
				  "it was meant to be in use, that is the finding."), name));

		return true;
	}
};

MCP_TOOL_REGISTER(ibMcpToolMetadataUsedBy);
