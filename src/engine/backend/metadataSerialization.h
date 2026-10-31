#ifndef _METADATA_SERIALIZATION_H__
#define _METADATA_SERIALIZATION_H__

#include "backend/backend_core.h"

class ibValue;
class ibDataNode;
class ibMetaData;

////////////////////////////////////////////////////////////////////////////
// A value through a node, against a configuration — the one road
////////////////////////////////////////////////////////////////////////////
//
// A reference and an enumeration member are types only a configuration has, so reading a value takes one: the one
// handed in by whoever knows it, else the one the session runs. Everything else in a node is the value registry's
// (ibValue::FromNode, compiler/valueSerialization.h says what the node holds).
//
// What cannot be read raises: a value read as an empty one is another value (Max, 2026-10-30).
////////////////////////////////////////////////////////////////////////////

// `metaData` — the configuration the value belongs to; none given, the one the session runs; a thread working for no
// base has none, and the registry alone reads. What a container holds is always read the second way: an array, a
// structure and a container read their elements and keys with this door, no configuration handed in.
BACKEND_API ibValue ibDeserializeValue(const ibDataNode& node, const ibMetaData* metaData = nullptr);

// Writing needs no configuration: a value knows its own type and contents. Raises for a value that cannot travel
// (a form, an open object, a lambda) or has no packed form.
BACKEND_API void ibSerializeValue(const ibValue& value, ibDataNode& node);

#endif
