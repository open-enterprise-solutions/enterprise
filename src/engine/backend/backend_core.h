#ifndef _CORE_OES_H__
#define _CORE_OES_H__

#include <wx/wx.h>

#include <map>

#include "backend.h"
#include "rowValues.h"

extern BACKEND_API unsigned int GetBuildId();

#include "guid.h"
#include "clsid.h"
#include "fnumber.h"
#include "fstring.h"
#include "typeconv.h"
#include "stringUtils.h"

// The UNDEFINED value type's clsid — the canonical "no concrete type"; base-level so low-level code can name it.
constexpr ibClassID g_valueUndefinedCLSID = primitive_to_clsid("VL_UNDF");

//*******************************************************************************************

#define oes_clipboard_metadata	wxT("oes_clipboard_metadata")
#define oes_clipboard_frame		wxT("oes_clipboard_frame")
#define oes_clipboard_interface	wxT("oes_clipboard_interface")
#define oes_clipboard_role		wxT("oes_clipboard_role")
#define oes_clipboard_template	wxT("oes_clipboard_template")
#define oes_clipboard_attribute	wxT("oes_clipboard_attribute")
#define oes_clipboard_command	wxT("oes_clipboard_command")

//*******************************************************************************************
//*                                 Special structures                                      *
//*******************************************************************************************

#define emptyDate -62135604000000ll

// ⭐⭐ THE MEMBER NUMBER THAT MEANS "NO MEMBER" — the enumeration's emptyDate.
//
// A member's number is whatever its declaration says: 0, 1, 2 — or 50, 51, 52, or any set at all.
// What holds for EVERY enumeration is the SIGN: members are NON-NEGATIVE, and the negative range is
// reserved for "nothing was chosen". That is the only reservation that can be made without knowing
// which numbers a particular enumeration happens to use, so the test is the sign — never a
// comparison against a member.
//
// Which is why ZERO CANNOT MEAN EMPTY: it is an ordinary member number like any other. Where an enum
// column defaulted to it, a row nobody ever filled in came back holding whichever member is 0, as
// though someone had chosen it. Read by the write placeholder, the DDL default, the packed form and
// the emptiness test alike, so the four cannot drift apart.
#define emptyEnum -1

// ⭐⭐ WHAT A PARENT MAY BE — ONE declaration with FOUR answers, and every layer asks it by name.
//
//   None            — a FLAT list. No parent at all: the field is gone, not merely unused.
//   Subordination   — a parent that is DATA: the field exists, is filled and is shown like any other
//                     attribute, and the list stays FLAT. This is a chart of accounts — an account
//                     records which account it sits under, and nobody browses it as a tree.
//   Items           — every element may hold elements; the platform drills into any of them.
//   FoldersAndItems — items live INSIDE folders: a folder is a container, an item is a leaf.
//
// It lives HERE, at the bottom, because three different tiers need the same four answers: the
// metaobject declares it, the query tier reads it off a source, the list decides how to walk it.
// Each of them used to be handed a BOOLEAN PROJECTION instead — `GetHierarchyColumn() != nullptr`
// for "is there a parent", `IsItemHierarchy()` for "may an item hold items" — and a projection
// answers one question while the caller has another. That is how a chart of accounts, which
// declares Subordination, first lost its hierarchy groupings (the accessor said "no parent") and
// then grew a tree in its list (the same accessor, corrected, now said "yes" to a caller asking
// whether to DRILL). One value with four states cannot be misread that way: whoever needs a
// distinction names the state it turns on.
//
// ⚠ Stored as its INTEGER — these numbers are the wire. New members APPEND.
enum ibHierarchyType {
	eFoldersAndItems = 0,
	eItems           = 1,
	eNone            = 2,   // the editor lists them in reading order (see CreateEnumeration), which is free
	eSubordination   = 3,
};

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

// metaID -> ibValue set of one record object / table row.
// ibRowValues (sorted vector) — same std::map API & sorted order, but one
// allocation, contiguous lookup and no per-entry RB-node overhead. See rowValues.h.
// (Alias only — ibValue is forward-declared inline; instantiated at member sites
// where value.h is complete.)
typedef ibRowValues<ibMetaID, class BACKEND_API ibValue> ibRowMetaValues;

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

//*******************************************************************************************
//*                                 Declare special var                                     *
//*******************************************************************************************

#define _USE_CONTROL_VALUECAST 1 
//firebird doesn't support multiple transaction 
#define _USE_SAVE_METADATA_IN_TRANSACTION 1
// full parser is very slowly ...
#define _USE_OLD_TEXT_PARSER_IN_CODE_EDITOR 0
//debugger
#define _USE_64_BIT_POINT_IN_DEBUGGER 1
//have bugs....
#define _USE_NET_COMPRESSOR 0
//use dynamic linking 
#define _USE_DYNAMIC_DATABASE_LAYER_LINKING 1

//max precision 
#define MAX_PRECISION_NUMBER 32
//max precision 
#define MAX_LENGTH_STRING 1024
//stack guard
#define MAX_OBJECTS_LEVEL 100
//max record level 
#define MAX_REC_COUNT 200

#if defined(_LP64) || defined(__LP64__) || defined(__arch64__) || defined(_WIN64)
#define MAX_STATIC_VAR 25ll
#else 
#define MAX_STATIC_VAR 10ll
#endif 

//*******************************************************************************************
//*                                 Versions support									    *
//*******************************************************************************************

#define version_generate(major, minor, release) \
		( (major * 1000) + (minor * 100) + release )

enum ibProgramVersion {
	version_oes_1_0_0 = version_generate(1, 0, 0),
	version_oes_1_0_1 = version_generate(1, 0, 1),
	version_oes_last  = version_oes_1_0_1
};

enum ibProgramSyntax {
	syntax_ves,    // Visual Basic-style ES, a legacy business-scripting dialect — keyword-fenced (Then/Do/EndIf/...).
	syntax_ces,    // C-style ES — paren conditions, brace bodies, `;` terminators (default).
};

//*******************************************************************************************

#define COMPONENT_TYPE_ABSTRACT		 0
#define COMPONENT_TYPE_METADATA		 COMPONENT_TYPE_ABSTRACT

#endif 