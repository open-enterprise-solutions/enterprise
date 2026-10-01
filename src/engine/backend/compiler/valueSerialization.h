#ifndef _VALUE_SERIALIZATION_H__
#define _VALUE_SERIALIZATION_H__

#include "backend/backend_core.h"
#include "backend/clsid.h"

class ibValue;
class ibDataNode;

////////////////////////////////////////////////////////////////////////////
// Reading a value OUT of a node — the part that is not the value's own
////////////////////////////////////////////////////////////////////////////
//
// Creating a value out of a node is ibValue::FromNode — one mechanism, in
// value.h, reached both directly and through ibMetaData::Deserialize.
//
// What is left here is the smaller question: what the header SAYS. It lives in
// its own narrow header rather than on ibValue because it changes with the
// reading format, not with the value, and value.h is included by half the
// engine.
////////////////////////////////////////////////////////////////////////////

// The type out of the header — the one place that knows how it is spelled.
BACKEND_API ibClassID ibReadNodeType(const ibDataNode& node);

#endif