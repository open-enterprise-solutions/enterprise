#ifndef _VALUE_SERIALIZATION_H__
#define _VALUE_SERIALIZATION_H__

#include "backend/backend_core.h"
#include "core/clsid.h"

class ibValue;
class ibDataNode;

////////////////////////////////////////////////////////////////////////////
// Reading a value OUT of a node — the part that is not the value's own
////////////////////////////////////////////////////////////////////////////
//
// Creating a value out of a node is ibValue::FromNode — one mechanism, in
// value.h, reached both directly and through ibMetaData::Deserialize.
//
// What is left here is the smaller question: what the header SAYS — and the
// door a container reads its elements through (ibDeserializeValue, defined
// beside ibMetaData::Deserialize). It lives in its own narrow header rather
// than on ibValue because it changes with the reading format, not with the
// value, and value.h is included by half the engine.
////////////////////////////////////////////////////////////////////////////

// The type out of the header — the one place that knows how it is spelled.
BACKEND_API ibClassID ibReadNodeType(const ibDataNode& node);

// A value out of a node at run time — through the configuration the session runs (ibMetaData::Deserialize), which
// makes the types only it has, a reference and an enumeration member; with none open, the value registry alone
// (ibValue::FromNode), and so does a thread that works for no base. What an array, a structure and a container read
// their elements and keys with, so a reference inside an array comes back as the
// one at the top does (Max, 2026-10-30: "at run time the active metadata is enough — it comes from the session").
BACKEND_API ibValue ibDeserializeValue(const ibDataNode& node);

#endif