#include "metadataSerialization.h"
#include "metaData.h"

#include "core/serialize/dataBuilder.h"
#include "compiler/valueSerialization.h"
#include "appData.h"   // ibApplicationInstance — the configuration the session runs
#include "metadataConfiguration.h"   // ibMetaDataConfigurationBase — what it answers, an ibMetaData
#include "backend_exception.h"

////////////////////////////////////////////////////////////////////////////
// Value serialization — the metadata's half of it
////////////////////////////////////////////////////////////////////////////
//
// THE ENTRY POINT is here:
//
//   ibSerializeValue(value, node);            // a value in, a filled tree out
//   value = ibDeserializeValue(node);         // a tree in, a live value out, by the session's configuration
//   value = ibDeserializeValue(node, metaData);   // …by the one the caller knows
//
// A value itself is blind to metadata and stays that way — save one door: a
// container's elements (an array, a structure, a container) are read through
// ibDeserializeValue with no configuration handed in, so by the one the SESSION
// runs. A reference inside an array comes back at run time; a caller that passed
// another configuration has it for the container, the session's for what the
// container holds.
//
// The tree, not bytes: what it becomes is the PROVIDER's business — binary for
// storage and transport, JSON for a wire or for a human reading a dump — the
// same way the metadata itself is saved. A second pair of methods taking a
// buffer would be that choice made twice.
//
// What only a metadata can do is turn a class id back into an instance of a
// CONFIGURATION type — a catalog reference, a document reference, an enum
// member — whose id is derived from a metaID no static table could know.
//
// Everything else it REDIRECTS to ibValue::FromNode, which is THE mechanism:
// the same one a caller with no configuration in play reaches directly. This
// file is a step in front of it, never a copy of it.
//
// And when neither has the type, that mechanism THROWS. The caller asked for a
// value; there is no value; saying so is the only honest answer. A quiet empty
// would be a lie that surfaces three layers away as a blank field nobody can
// explain.
////////////////////////////////////////////////////////////////////////////

void ibSerializeValue(const ibValue& value, ibDataNode& node)
{
	// Writing needs nothing from the configuration — a value knows its own type
	// and its own contents. A refusal here is IsTransferable saying no (a form,
	// an open object, a lambda) or a type with no packed form at all.
	if (!value.Serialize(node))
		ibBackendCoreException::Error(_("The value '%s' cannot be serialized"),
			value.GetClassName());
}

void ibMetaData::Serialize(const ibValue& cValue, ibDataNode& node) const
{
	ibSerializeValue(cValue, node);
}

ibValue ibMetaData::Deserialize(const ibDataNode& node) const
{
	const ibClassID classType = ibReadNodeType(node);

	// MY OWN REGISTRY, asked directly — GetTypeCtor, not IsRegisterCtor, because
	// the latter already answers for ibValue's registry too and would swallow
	// the very question being asked here.
	//
	// Not mine: straight to the ONE mechanism. Everything past this point —
	// creation, the contents, the refusals — happens there, once, for both
	// doors.
	if (GetTypeCtor(classType) == nullptr)
		return ibValue::FromNode(node);

	ibValue createdValue;
	try {
		createdValue = CreateObject(classType);
	}
	catch (const ibCoreException&) {
		throw;
	}
	catch (...) {
		ibBackendCoreException::Error(_("Failed to create a value of type '%s'"),
			ibValue::GetNameObjectFromID(classType));
	}

	// CREATED, THEN HANDED THE WHOLE NODE — a reference sorts out its metaID and
	// guid, an array its elements. This layer never learns what they contain,
	// only whether they managed.
	if (!createdValue.Deserialize(node))
		ibBackendCoreException::Error(_("Failed to read the contents of a value of type '%s'"),
			ibValue::GetNameObjectFromID(classType));

	return createdValue;
}

ibValue ibDeserializeValue(const ibDataNode& node, const ibMetaData* metaData)
{
	// Asked, not required: a thread working for no base reads with the registry alone rather than refusing.
	if (metaData == nullptr)
		metaData = ibApplicationInstance::GetActiveMetaData(ibApplicationInstance::Get(false));
	return metaData != nullptr ? metaData->Deserialize(node) : ibValue::FromNode(node);
}
