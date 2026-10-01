#include "metaData.h"

#include "serialize/dataBuilder.h"
#include "compiler/valueSerialization.h"
#include "backend_exception.h"

////////////////////////////////////////////////////////////////////////////
// Value serialization — the metadata's half of it
////////////////////////////////////////////////////////////////////////////
//
// THE ENTRY POINT is here:
//
//   metaData->Serialize(value, node);         // a value in, a filled tree out
//   value = metaData->Deserialize(node);      // a tree in, a live value out
//
// A caller takes the metadata it wants — the active one, or any other — and
// asks it. A value itself is blind to metadata and stays that way.
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

void ibMetaData::Serialize(const ibValue& cValue, ibDataNode& node) const
{
	// Writing needs nothing from the configuration — a value knows its own type
	// and its own contents. A refusal here is IsTransferable saying no (a form,
	// an open object, a lambda) or a type with no packed form at all.
	if (!cValue.Serialize(node))
		ibBackendCoreException::Error(_("The value '%s' cannot be serialized"),
			cValue.GetClassName());
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
	catch (const ibBackendException&) {
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
