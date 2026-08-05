#ifndef __CLSID_H__
#define __CLSID_H__

// uint64_t, not `unsigned wxLongLong_t`. Same width everywhere, so nothing serialised
// changes — but the same TYPE as the u64 the reader/writer layer speaks in. Those two
// spellings are identical on Windows (both unsigned __int64) and DIFFERENT on LP64, where
// uint64_t is `unsigned long` and wxLongLong_t is `long long`; a chunk id could then not
// bind to `u64&` and every such call failed to compile. One base for every 64-bit id.
#include <cstdint>
typedef uint64_t ibClassID;

//*******************************************************************************************
//*                       FNV-1a 64 hash — encoding for ibClassID                           *
//*******************************************************************************************
//
// constexpr hash from a class name string to a 64-bit ibClassID.
// Replaces the legacy 8-byte ASCII pack (MK_CLSID / packed string_to_clsid):
//   - no 8-character length limit, names of any length supported
//   - dynamic prefix+metaID encoding no longer silently truncates at >9999999
//   - collision-checked: 0 collisions across 163 PascalCase names + 181
//     legacy XX_YYY names + 1.6M dynamic (16 prefixes x metaID 1..100000)
//
// Algorithm: FNV-1a 64-bit (offset 0xCBF29CE484222325, prime 0x100000001B3).
// Self-contained, deterministic across compilers / platforms / runs.

constexpr ibClassID ib_clsid_hash(const char* s) {
	ibClassID h = 14695981039346656037ULL;
	while (*s) {
		h ^= static_cast<unsigned char>(*s++);
		h *= 1099511628211ULL;
	}
	return h;
}

// Runtime overload for wxString — used by the per-kind generators' wxString overloads
// (make_clsid(const wxString&, kind), below) for callsites that pass wxT("XX") or a runtime
// string. utf8_str() returns a scoped buffer valid through the end of the full expression;
// ib_clsid_hash(const char*) reads it and returns by value.
inline ibClassID ib_clsid_hash(const wxString& s) {
	if (s.IsEmpty()) return 0;
	return ib_clsid_hash(static_cast<const char*>(s.utf8_str().data()));
}

// Reference values from the collision-check pass — if any of these fail,
// the FNV implementation has drifted and downstream registrations will
// silently produce different CLSIDs than expected.
static_assert(ib_clsid_hash("Catalog")  == 0x1F1750C1BC916638ULL, "ib_clsid_hash drift: Catalog");
static_assert(ib_clsid_hash("Document") == 0xA311A24C1471A974ULL, "ib_clsid_hash drift: Document");
static_assert(ib_clsid_hash("Number")   == 0xBBB97B1C63507DC0ULL, "ib_clsid_hash drift: Number");
static_assert(ib_clsid_hash("Button")   == 0x0312976FE9DA5951ULL, "ib_clsid_hash drift: Button");
static_assert(ib_clsid_hash("Iterator") == 0xBC8BD1C92C42730FULL, "ib_clsid_hash drift: Iterator");
static_assert(ib_clsid_hash("Form")     == 0x07AA10850D9DDCC7ULL, "ib_clsid_hash drift: Form");
static_assert(ib_clsid_hash("C_5")      == 0x0B6ADA19AA2FB1C6ULL, "ib_clsid_hash drift: C_5");
static_assert(ib_clsid_hash("R_42")     == 0x56AFDE2C3F2EF184ULL, "ib_clsid_hash drift: R_42");
static_assert(ib_clsid_hash("M_100")    == 0x2CDB7E80AB98A55CULL, "ib_clsid_hash drift: M_100");

//*******************************************************************************************
//*                         Legacy aliases — kept for source compatibility                  *
//*******************************************************************************************

// MK_CLSID / MK_CLSID_INV are no longer used — kept only because some
// external callers may still reference them. New code uses ib_clsid_hash.
// (Identical to historical definition; retained for source compatibility.)
#define MK_CLSID(a,b,c,d,e,f,g,h) \
    	ibClassID((ibClassID(a)<<ibClassID(56))|(ibClassID(b)<<ibClassID(48))|(ibClassID(c)<<ibClassID(40))|(ibClassID(d)<<ibClassID(32))|(ibClassID(e)<<ibClassID(24))|(ibClassID(f)<<ibClassID(16))|(ibClassID(g)<<ibClassID(8))|(ibClassID(h)))

#define MK_CLSID_INV(a,b,c,d,e,f,g,h) MK_CLSID(h,g,f,e,d,c,b,a)

// clsid_to_string previously unpacked 8-byte ASCII. With FNV hashing there is
// no inverse — return a hex representation for debug logs. For class-name
// lookup, callers should query the type registry via valueFactory.
inline wxString clsid_to_string(const ibClassID& clsid) {
	if (clsid == 0) return wxEmptyString;
	return wxString::Format(wxT("0x%016llX"), static_cast<uint64_t>(clsid));
}

// string_to_clsid is GONE — every callsite now uses a per-kind generator below
// (value_to_clsid / metadata_to_clsid / … keep the legacy "XX_YYY" string as the body,
// but bake the registrar's KIND into the high byte). The kind comes from WHERE the type is
// registered (the *_TYPE_REGISTER macro family), never from the string prefix.

//*******************************************************************************************
//*                     Class-kind tag — the high byte of ibClassID                         *
//*******************************************************************************************
//
//   ibClassID layout:   [63:56] kind (1 byte)   |   [55:0] body (56 bits)
//
// The KIND is the class OF the id — read straight from the id, statelessly, with NO
// ibMetaData lookup (the old GetVTByID hung on one config image; a kind read is global).
// The BODY carries identity: the name-derived FNV hash (static types, masked to 56 bits)
// or the metaID itself (dynamic types). Uniqueness lives in the body — the kind is
// CLASSIFICATION, not identity. `IsReference(id)` etc. just read the byte.
//
// Plugins get NO kind bit: a plugin component registers under a name the CORE prefixes
// with the plugin's unique key, so the hash body stays unique across plugins — uniqueness
// is in the string/body, not extra bits. A plugin inventing a NEW family draws a custom
// kind from the reserved 0x80.. range via register_clsid_kind (the extensibility seam).

enum ibClassKind : unsigned char {
	ibClassKind_None      = 0x00,   // unspecified — pure hash body (legacy string_to_clsid)

	// --- static families — one per *_TYPE_REGISTER macro (the ctor already knows its kind) ---
	ibClassKind_Primitive = 0x01,   // PRIMITIVE_TYPE_REGISTER  (Number / String / Date / …)
	ibClassKind_Value     = 0x02,   // VALUE_TYPE_REGISTER      (Array / Struct / DynamicList / …)
	ibClassKind_Control   = 0x03,   // CONTROL_TYPE_REGISTER    (Button / form controls)
	ibClassKind_System    = 0x04,   // SYSTEM_TYPE_REGISTER     (non-creatable system objects)
	ibClassKind_Enum      = 0x05,   // ENUM_TYPE_REGISTER       (system enumerations)
	ibClassKind_Context   = 0x06,   // CONTEXT_TYPE_REGISTER    (singleton context objects)
	ibClassKind_Metadata  = 0x07,   // a metaobject DEFINITION (the MD_* umbrella)
	ibClassKind_Picture   = 0x08,   // a picture / icon id (the PC_* g_pic*CLSID — not a creatable type)

	// --- dynamic metaobject runtime values — mirror ibCtorObjectMetaType (objCtorDefs.h),
	//     kind = 0x0F + metatype; body = the metaID itself (constructive, collision-free). ---
	ibClassKind_Reference            = 0x10,   // _Reference          (was "R_<metaID>")
	ibClassKind_List                 = 0x11,   // _List               (Ref-list "L_" + Reg-list "J_")
	ibClassKind_Object               = 0x12,   // _Object             (was "O_<metaID>")
	ibClassKind_Manager              = 0x13,   // _Manager            (was "M_<metaID>")
	ibClassKind_Selection            = 0x14,   // _Selection          (was "S_<metaID>")
	ibClassKind_TabularSection       = 0x15,   // _TabularSection     (was "T_"; this IS "table" — tabular parts ONLY)
	ibClassKind_TabularSectionString = 0x16,   // _TabularSection_String (was "B_")
	ibClassKind_Characteristic       = 0x17,   // _Characteristic
	ibClassKind_RecordSet            = 0x18,   // _RecordSet          (was "H_")
	ibClassKind_RecordSetString      = 0x19,   // _RecordSet_String   (was "P_")
	ibClassKind_RecordKey            = 0x1A,   // _RecordKey          (was "A_")
	ibClassKind_RecordManager        = 0x1B,   // _RecordManager      (was "D_")

	// external (file-loaded) DataProcessor/Report variants — same metaID AND meta-kind as the
	// internal Object/Manager, so they need their OWN kind to stay collision-free under a
	// constructive (metaID) body (the old "EO_"/"EM_" prefixes; GetMetaTypeCtor still reports
	// Object/Manager).
	ibClassKind_ExternalObject       = 0x1C,
	ibClassKind_ExternalManager      = 0x1D,

	ibClassKind_CustomBase = 0x80,  // 0x80..0xFF — plugin-defined kinds (register_clsid_kind)
};

// A dynamic metatype (objCtorDefs.h, 1-based) → its kind byte. kind = 0x0F + metatype.
constexpr ibClassKind metatype_to_kind(int metaType) { return static_cast<ibClassKind>(0x0F + metaType); }

constexpr ibClassID kIbClsidBodyMask = (ibClassID(1) << 56) - 1;   // low 56 bits = body

// Bake a kind into a name-derived id: high byte = kind, body = FNV hash (masked to 56 bits).
constexpr ibClassID make_clsid(const char* name, ibClassKind kind) {
	return (ibClassID(static_cast<unsigned char>(kind)) << 56) | (ib_clsid_hash(name) & kIbClsidBodyMask);
}
inline ibClassID make_clsid(const wxString& name, ibClassKind kind) {
	return (ibClassID(static_cast<unsigned char>(kind)) << 56) | (ib_clsid_hash(name) & kIbClsidBodyMask);
}

// Dynamic (CONSTRUCTIVE) id: body = the metaID itself — (kind, metaID) is unique by
// construction, no hash, no possible collision. For the metaobject runtime values.
constexpr ibClassID make_clsid_dynamic(ibClassID metaID, ibClassKind kind) {
	return (ibClassID(static_cast<unsigned char>(kind)) << 56) | (metaID & kIbClsidBodyMask);
}

// --- static name→id wrappers — the ergonomic replacements for string_to_clsid at a
//     kind-known callsite: value_to_clsid("DynamicList"), control_to_clsid("Button"), … ---
constexpr ibClassID primitive_to_clsid(const char* n) { return make_clsid(n, ibClassKind_Primitive); }
constexpr ibClassID value_to_clsid    (const char* n) { return make_clsid(n, ibClassKind_Value); }
constexpr ibClassID control_to_clsid  (const char* n) { return make_clsid(n, ibClassKind_Control); }
constexpr ibClassID system_to_clsid   (const char* n) { return make_clsid(n, ibClassKind_System); }
constexpr ibClassID enum_to_clsid     (const char* n) { return make_clsid(n, ibClassKind_Enum); }
constexpr ibClassID context_to_clsid  (const char* n) { return make_clsid(n, ibClassKind_Context); }
constexpr ibClassID metadata_to_clsid (const char* n) { return make_clsid(n, ibClassKind_Metadata); }
constexpr ibClassID picture_to_clsid  (const char* n) { return make_clsid(n, ibClassKind_Picture); }
// wxString overloads — for callsites passing wxT("XX") (wide literal) or a runtime string.
inline ibClassID primitive_to_clsid(const wxString& n) { return make_clsid(n, ibClassKind_Primitive); }
inline ibClassID value_to_clsid    (const wxString& n) { return make_clsid(n, ibClassKind_Value); }
inline ibClassID control_to_clsid  (const wxString& n) { return make_clsid(n, ibClassKind_Control); }
inline ibClassID system_to_clsid   (const wxString& n) { return make_clsid(n, ibClassKind_System); }
inline ibClassID enum_to_clsid     (const wxString& n) { return make_clsid(n, ibClassKind_Enum); }
inline ibClassID context_to_clsid  (const wxString& n) { return make_clsid(n, ibClassKind_Context); }
inline ibClassID metadata_to_clsid (const wxString& n) { return make_clsid(n, ibClassKind_Metadata); }
inline ibClassID picture_to_clsid  (const wxString& n) { return make_clsid(n, ibClassKind_Picture); }

// --- dynamic metaID→id wrappers (constructive body) — one per metaobject runtime value ---
constexpr ibClassID reference_to_clsid           (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_Reference); }
constexpr ibClassID list_to_clsid                (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_List); }
constexpr ibClassID object_to_clsid              (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_Object); }
constexpr ibClassID manager_to_clsid             (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_Manager); }
constexpr ibClassID selection_to_clsid           (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_Selection); }
constexpr ibClassID tabularSection_to_clsid      (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_TabularSection); }
constexpr ibClassID tabularSectionString_to_clsid(ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_TabularSectionString); }
constexpr ibClassID characteristic_to_clsid      (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_Characteristic); }
constexpr ibClassID recordSet_to_clsid           (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_RecordSet); }
constexpr ibClassID recordSetString_to_clsid     (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_RecordSetString); }
constexpr ibClassID recordKey_to_clsid           (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_RecordKey); }
constexpr ibClassID recordManager_to_clsid       (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_RecordManager); }
constexpr ibClassID externalObject_to_clsid      (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_ExternalObject); }
constexpr ibClassID externalManager_to_clsid     (ibClassID metaID) { return make_clsid_dynamic(metaID, ibClassKind_ExternalManager); }

// --- stateless kind reads — no metadata, no registry, just the high byte ---
constexpr ibClassKind clsid_kind (ibClassID c) { return static_cast<ibClassKind>(static_cast<unsigned char>(c >> 56)); }

constexpr bool IsReference     (ibClassID c) { return clsid_kind(c) == ibClassKind_Reference; }
constexpr bool IsList          (ibClassID c) { return clsid_kind(c) == ibClassKind_List; }
constexpr bool IsObject        (ibClassID c) { return clsid_kind(c) == ibClassKind_Object || clsid_kind(c) == ibClassKind_ExternalObject; }
constexpr bool IsManager       (ibClassID c) { return clsid_kind(c) == ibClassKind_Manager || clsid_kind(c) == ibClassKind_ExternalManager; }
constexpr bool IsSelection     (ibClassID c) { return clsid_kind(c) == ibClassKind_Selection; }
// NOTE: this is the narrow "tabular section" KIND only — NOT the general "is this a table?"
// predicate. Table-ness spans several meta-kinds (List / TabularSection(+String) /
// RecordSet(+String)) and is answered through the FACTORY (ibValue::IsTableValue → the ctor's
// IsTableValue), not reconstructed from the clsid byte. Don't use this as "is a table".
constexpr bool IsTabularSection(ibClassID c) { return clsid_kind(c) == ibClassKind_TabularSection; }
constexpr bool IsCharacteristic(ibClassID c) { return clsid_kind(c) == ibClassKind_Characteristic; }
constexpr bool IsRecordSet     (ibClassID c) { return clsid_kind(c) == ibClassKind_RecordSet; }
constexpr bool IsRecordKey     (ibClassID c) { return clsid_kind(c) == ibClassKind_RecordKey; }
constexpr bool IsRecordManager (ibClassID c) { return clsid_kind(c) == ibClassKind_RecordManager; }
constexpr bool IsMetadata      (ibClassID c) { return clsid_kind(c) == ibClassKind_Metadata; }
constexpr bool IsControl       (ibClassID c) { return clsid_kind(c) == ibClassKind_Control; }
constexpr bool IsValue         (ibClassID c) { return clsid_kind(c) == ibClassKind_Value; }
constexpr bool IsEnum          (ibClassID c) { return clsid_kind(c) == ibClassKind_Enum; }
constexpr bool IsSystem        (ibClassID c) { return clsid_kind(c) == ibClassKind_System; }
constexpr bool IsPrimitive     (ibClassID c) { return clsid_kind(c) == ibClassKind_Primitive; }
// Any DYNAMIC metaobject runtime value (Reference / List / Object / Manager / … / External*) —
// the stateless equivalent of ibCtorObjectType_object_meta_value.
constexpr bool IsMetaValue     (ibClassID c) { return clsid_kind(c) >= ibClassKind_Reference && clsid_kind(c) <= ibClassKind_ExternalManager; }

// Plugin-defined custom kinds — the extensibility seam. A small name→byte registry over
// the 0x80.. range (stable within a run). Declared here; impl lives in a .cpp so this
// very-widely-included header stays free of <map>/<mutex>. Wired when the first plugin
// needs its own family; core kinds above never call it.
ibClassKind register_clsid_kind(const wxString& kindName);

#endif
