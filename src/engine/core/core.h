#ifndef _CORE_CORE_H__
#define _CORE_CORE_H__

#include <wx/defs.h>   // WXEXPORT / WXIMPORT — every header of the core includes this one

// THE SHARED MEANINGS — what the engine (backend) and its clients (frmclient, the protocol's sides) mean by the same
// words: the class ids and the ids of the configuration, the kinds of a value, a text, a date, a guid. Under both, and
// knowing neither: nothing here reads a base, runs a script or draws a window (docs/private/client-protocol-arc.md).
#if defined(CORE_EXPORTS)
#define CORE_API WXEXPORT
#else
#define CORE_API WXIMPORT
#endif

// `inline` IS A HINT AND THE INLINER DECLINES IT under a size budget — which it
// does exactly where it hurts, inside the big dispatch functions. procUnit.cpp
// found this first (its note: "the disassembly says so" — a two-instruction body
// still emitted a call from a 27 KB function) and defined these locally; they
// live here now because the second caller found the same wall, in
// ibNumber::Compare on the value comparator's hot arm.
//
// Reach for IB_FORCEINLINE only with a disassembly in hand. A forced body is
// pasted into EVERY caller, so the win has to be real and the body small.
#if defined(_MSC_VER)
#  define IB_FORCEINLINE __forceinline
#  define IB_NOINLINE    __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#  define IB_FORCEINLINE inline __attribute__((always_inline))
#  define IB_NOINLINE    __attribute__((noinline))
#else
#  define IB_FORCEINLINE inline
#  define IB_NOINLINE
#endif

#include <cstdint>

#include "core/clsid.h"

// The UNDEFINED value type's clsid — the canonical "no concrete type"; base-level so low-level code can name it.
constexpr ibClassID g_valueUndefinedCLSID = primitive_to_clsid("VL_UNDF");

// …and the primitives' — what every value is (anyValue.h), named the same by the engine and the client.
constexpr ibClassID g_valueBooleanCLSID = primitive_to_clsid("VL_BOOL");
constexpr ibClassID g_valueNumberCLSID = primitive_to_clsid("VL_NUMB");
constexpr ibClassID g_valueDateCLSID = primitive_to_clsid("VL_DATE");
constexpr ibClassID g_valueStringCLSID = primitive_to_clsid("VL_STRI");

constexpr ibClassID g_valueNullCLSID = primitive_to_clsid("VL_NULL");

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

// A command's id carries THIS bit to say "after running me on the model, ALSO start the row's inline editor on
// the front". A model BAKES it into its Edit id in the enum (`eEditValue = <n> | eStartEditingFlag`) — for the MODEL
// it is just an ordinary command value (its own `case eEditValue` carries the bit too). The TableBox's CallAsAction
// tests the bit on EVERY id it forwards to CallAsCommand (which dispatches its Edit case normally — a list opens the
// object form, a value-table does nothing there) and — if the bit is set — runs EditCurrentRow after (a value-table
// / tabular row edits inline; a list no-ops, its form already opened). Inline editing is a pure FRONT operation, not
// round-tripped through the backend to call back. In the core because the thin client's table reads it too: the id
// comes to it in the command bar's entries, and the bit is what it opens the editor by.
//
// The id (flag included) MUST stay a valid wxMenuItem id (< 32767): the TableBox appends its command ids straight as
// context-menu item ids (OnContextMenu) — a high bit here trips wxMenuItemBase's `itemid < 32767` assert. So it is a
// FREE bit no real id band sets: model ids (1..27), form chrome (10000..10003), TableBox band (20000..20004) all
// leave bit 12 clear, and `<n> | 0x1000` stays well under 32767 (e.g. eEditValue = 3 | 0x1000 = 4099). A new band
// must likewise keep bit 12 clear.
constexpr ibActionID eStartEditingFlag = 0x1000;

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

// THE PRIMITIVE'S CLASS BY ITS VALUE TYPE — the constants above, asked by the kind; 0 for a kind that is no
// primitive. One table for both sides: the engine's and the client's ibValue::GetIDByVT answer by it, and so
// does the type description, which the core keeps.
constexpr ibClassID GetIDByVT(ibValueTypes valueType) {
	switch (valueType) {
	case ibValueTypes::TYPE_EMPTY: return g_valueUndefinedCLSID;
	case ibValueTypes::TYPE_BOOLEAN: return g_valueBooleanCLSID;
	case ibValueTypes::TYPE_NUMBER: return g_valueNumberCLSID;
	case ibValueTypes::TYPE_DATE: return g_valueDateCLSID;
	case ibValueTypes::TYPE_STRING: return g_valueStringCLSID;
	case ibValueTypes::TYPE_NULL: return g_valueNullCLSID;
	default: return 0;
	}
}

// What a date keeps — the date, both, the time. Here, beside the value types, because the type
// description keeps it (typeDescription.h) and the format string reads it (formatString.h).
enum ibDateFractions {
	ibDateFractions_Date = 0,
	ibDateFractions_DateTime,
	ibDateFractions_Time
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
