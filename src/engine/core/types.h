#ifndef _CORE_TYPES_H__
#define _CORE_TYPES_H__

#include <cstdint>

#include "core/clsid.h"

// The UNDEFINED value type's clsid — the canonical "no concrete type"; base-level so low-level code can name it.
constexpr ibClassID g_valueUndefinedCLSID = primitive_to_clsid("VL_UNDF");

//*******************************************************************************************
//*                                 Special structures                                      *
//*******************************************************************************************

typedef int ibRoleID;
typedef int ibMetaID;
// A metaId acting as a SOURCE-binding hop (an element of a control's binding
// path: attribute id, then field / reference / column ids). A distinct name so a
// binding chain reads as source ids, not as arbitrary metaIds.
typedef int ibSourceId;
typedef int ibFormID;
typedef int ibActionID;

typedef uint64_t ibPictureID;   // same base as ibClassID / u64 — see the note in clsid.h
typedef unsigned int ibVersionID;

//*******************************************************************************************
//*                                 Special enumeration                                     *
//*******************************************************************************************

// Underlying type fixed at 1 byte: the largest enumerator (TYPE_ITERATOR
// = 204) fits in unsigned char, and the AOT wire format already narrows
// m_typeClass to uint8_t (byteCodeAOT.cpp), so this is binary-compatible
// with persisted bytecode. Shrinks the m_typeClass slot in every ibValue.
enum ibValueTypes : unsigned char {

	TYPE_EMPTY = 0,
	TYPE_BOOLEAN = 1,
	TYPE_NUMBER = 2,
	TYPE_DATE = 3,
	TYPE_STRING = 4,
	TYPE_NULL = 5,

	TYPE_REFFER = 100, // object reference (owned: IncrRef/DecrRef, Reset may delete)
	TYPE_CONST_REFFER = 101, // read-only reference to a NON-owned object (e.g. a
	                         // const ibValueMetaObject* from the metadata tree).
	                         // No ref-count, Reset never deletes it; mutation blocked.

	TYPE_VALUE = 200, // value
	TYPE_ENUM = 201, // enumeration
	TYPE_OLE = 202, // ole object
	TYPE_FUNCTION = 203, // anonymous-function / lambda value (ibValueFunction)
	TYPE_ITERATOR = 204, // iterator wrapper (ibValueIterator)

	TYPE_LAST,
};

// WHAT KIND OF THING A REGISTERED TYPE IS, and what the registry tells it. Here, beside the value
// types, because ibValue's registry surface names them (value.h) while the ctors that carry them
// (compiler/typeCtor.h) are built on a complete ibValue — so they cannot live with the ctors.
enum ibCtorObjectType {
	ibCtorObjectType_object_primitive = 1,
	ibCtorObjectType_object_value,
	ibCtorObjectType_object_control,
	ibCtorObjectType_object_system,
	ibCtorObjectType_object_enum,
	ibCtorObjectType_object_context,

	ibCtorObjectType_object_metadata,
	ibCtorObjectType_object_meta_value
};

enum ibCtorObjectTypeEvent {
	ibCtorObjectTypeEvent_Register,
	ibCtorObjectTypeEvent_UnRegister,
};

// THE PACKED NODE'S TWO FIELD NAMES. Short because they repeat per element in a binary stream,
// stable because they are what a JSON dump shows — and declared HERE, not in the serialiser, because
// more than one place writes into that node: the value base writes the primitives, an enumeration
// writes its member from the template that defines it, the client reads what the server wrote. Two
// spellings of the same key is how a value gets written under one name and read under another.
inline const wxChar* const kValueFieldClsid = wxT("t");   // the type — written and read FIRST
inline const wxChar* const kValueFieldData  = wxT("v");   // the payload

//*******************************************************************************************

#define COMPONENT_TYPE_ABSTRACT		 0
#define COMPONENT_TYPE_METADATA		 COMPONENT_TYPE_ABSTRACT

#endif
