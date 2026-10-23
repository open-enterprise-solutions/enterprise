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

// THE ENGINE'S CHARACTER — what an ibString (fstring.h) is made of, and what wx calls wxChar. Here, not in
// fstring.h, because headers under it (this one, the number's separators, the boolean's words) name it too.
#define ibChar wchar_t

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

// …and the value table's — the one table a type selector offers (ibSelectorDataType_table), on both sides.
constexpr ibClassID g_valueTableCLSID = value_to_clsid("VL_TABL");

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

typedef uint64_t ibPictureID;   // same base as ibClassID / std::uint64_t — see the note in clsid.h
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

// THE WIDEST NUMBER AND THE LONGEST STRING a type may be qualified with — `Number(32)`, `String(1024)`. The engine's
// and the client's: the query's parser checks a CAST against them on either side.
#define MAX_PRECISION_NUMBER 32
#define MAX_LENGTH_STRING 1024

// THE PRIMITIVE'S NAME AS THE LANGUAGE WRITES IT — the `Number` of `New TypeDescription("Number")` and of a query's
// `CAST(… AS Number(15, 2))`; nullptr for a kind that is no primitive. ONE TABLE: the engine registers its primitives
// by these words (backend/compiler/value.cpp, PRIMITIVE_TYPE_REGISTER) and ibValue::GetNameObjectFromVT answers by
// it, so the name a class is known by cannot part from the one the query's parser reads.
constexpr const char* GetNameObjectFromVT(ibValueTypes valueType) {
	switch (valueType) {
	case ibValueTypes::TYPE_EMPTY: return "Undefined";
	case ibValueTypes::TYPE_BOOLEAN: return "Boolean";
	case ibValueTypes::TYPE_NUMBER: return "Number";
	case ibValueTypes::TYPE_DATE: return "Date";
	case ibValueTypes::TYPE_STRING: return "String";
	case ibValueTypes::TYPE_NULL: return "Null";
	default: return nullptr;
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
inline const ibChar* const kValueFieldClsid = wxT("t");   // the type — written and read FIRST
inline const ibChar* const kValueFieldData  = wxT("v");   // the payload

// THE CLIPBOARD'S FORMATS — what a copied object is put on the system's clipboard under. Here, not with a designer,
// because both designers and the client copy and paste by them: one window pastes what another copied, so the
// name a copy goes under cannot part from the one a paste asks for.
#define oes_clipboard_metadata	wxT("oes_clipboard_metadata")
#define oes_clipboard_frame		wxT("oes_clipboard_frame")
#define oes_clipboard_interface	wxT("oes_clipboard_interface")
#define oes_clipboard_role		wxT("oes_clipboard_role")
#define oes_clipboard_template	wxT("oes_clipboard_template")
#define oes_clipboard_attribute	wxT("oes_clipboard_attribute")
#define oes_clipboard_command	wxT("oes_clipboard_command")
// A command of a control's command bar — on the system's clipboard as the others are: the desktop held it in its process
// (commandBar.cpp's s_commandClipboard), out of reach of another window.
#define oes_clipboard_command_bar_item	wxT("oes_clipboard_command_bar_item")

//*******************************************************************************************

#define COMPONENT_TYPE_ABSTRACT		 0
#define COMPONENT_TYPE_METADATA		 COMPONENT_TYPE_ABSTRACT

#endif
