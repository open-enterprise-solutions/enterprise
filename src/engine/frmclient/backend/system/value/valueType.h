#ifndef __VALUE_TYPE_H__
#define __VALUE_TYPE_H__

// A TYPE AS A VALUE'S SHAPE — the engine's ibValueTypeDescription (backend/system/value/valueType.h) as the client's
// settings windows ask it: what a value of a declared type is made, and what a value typed into a cell becomes. A
// primitive is shaped here as the engine shapes it; a type of the server's (a reference, an enum member) is not made
// here — its empty value is written as the engine writes one ({t}), and the server reads it so.

#include "frmclient/backend/compiler/value.h"
#include "frmclient/backend/typeDescription.h"

class FRMCLIENT_API ibMetaData;

class FRMCLIENT_API ibValueTypeDescription {
public:

	// The empty value of a type of one class — Undefined when it declares none or several.
	static ibValue AdjustValue(const ibTypeDescription& typeDescription,
		const ibMetaData* metaData = nullptr);
	// A value made one of the type — itself when the type admits it, shaped by its qualifiers; else read as the type's
	// one class, or the type's empty value.
	static ibValue AdjustValue(const ibTypeDescription& typeDescription, const ibValue& varValue,
		const ibMetaData* metaData = nullptr);

	// ⭐ DOES THE DESCRIPTION ADMIT A VALUE OF THIS CLASS — its classes, each as a range (a family takes its members).
	static bool AllowValue(const ibTypeDescription& typeDescription, const ibClassID& clsid,
		const ibMetaData* metaData = nullptr);
};

#endif
