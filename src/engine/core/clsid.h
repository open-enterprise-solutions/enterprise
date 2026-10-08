#ifndef __CLSID_H__
#define __CLSID_H__

// uint64_t, not `unsigned wxLongLong_t`. Same width everywhere, so nothing serialised
// changes — but the same TYPE as the u64 the reader/writer layer speaks in. Those two
// spellings are identical on Windows (both unsigned __int64) and DIFFERENT on LP64, where
// uint64_t is `unsigned long` and wxLongLong_t is `long long`; a chunk id could then not
// bind to `u64&` and every such call failed to compile. One base for every 64-bit id.
#include <cstdint>
typedef uint64_t ibClassID;

// SELF-CONTAINED: this header's own declarations use wxString (ib_clsid_hash, clsid_to_string,
// the make_clsid family), so it includes it rather than relying on whoever includes it first
// having pulled wx in already. It always had that dependency and never declared it — invisible
// while every includer happened to be a .cpp that included wx earlier, and a hard error the
// moment a translation unit (a test) reached for it directly. Clang says so; MSVC did not.
#include <wx/string.h>

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

// clsid_to_string previously unpacked 8-byte ASCII. With FNV hashing there is
// no inverse — return a hex representation for debug logs. For class-name
// lookup, callers should query the type registry via valueFactory.
inline wxString clsid_to_string(const ibClassID& clsid) {
	if (clsid == 0) return wxEmptyString;
	return wxString::Format(wxT("0x%016llX"), static_cast<uint64_t>(clsid));
}

// string_to_clsid is GONE — every callsite now uses a per-kind generator below
// (value_to_clsid / control_to_clsid / … keep the legacy "XX_YYY" string as the body,
// but bake the registrar's KIND into the high byte). The kind comes from WHERE the type is
// registered (the *_TYPE_REGISTER macro family), never from the string prefix.

//*******************************************************************************************
//*                     Class-kind tag — the high byte of ibClassID                         *
//*******************************************************************************************
//
//   ibClassID layout:   [63:56] kind (1 byte)   |   [55:0] body (56 bits)
//   …a dynamic one:     [63:56] kind   |   [55:32] metaclass (24 bits)   |   [31:0] metaID
//   …a metaclass's:     [63:56] Metadata   |   [55:32] itself   |   [31:0] 0
//
// The KIND is the class OF the id — read straight from the id, statelessly, with NO
// ibMetaData lookup (the old GetVTByID hung on one config image; a kind read is global).
// The BODY carries identity: the name-derived FNV hash (static types, masked to 56 bits;
// a metaclass's folded to 24) or the metaclass and the metaID (dynamic types). Uniqueness
// lives in the body — the kind is CLASSIFICATION, not identity. `IsReference(id)` etc. just
// read the byte.
//
// Plugins get NO kind bit: a plugin component registers under a name the CORE prefixes
// with the plugin's unique key, so the hash body stays unique across plugins — uniqueness
// is in the string/body, not extra bits. A plugin's new metaclass brings its families with
// its own metaclass id (below).

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
	//     kind = 0x0F + metatype; body = metaclass | metaID (constructive, collision-free). ---
	ibClassKind_Reference            = 0x10,   // _Reference          (was "R_<metaID>")
	ibClassKind_Object               = 0x12,   // _Object             (was "O_<metaID>"; 0x11 was a per-metaobject list)
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
};

// ⭐ THE METACLASS OF A DYNAMIC VALUE — the 24 bits under the kind: WHICH metaclass (Catalog, Document…) a
// reference, an object, a manager… belongs to, so the family is read off the id (clsid_any_of) and says what
// metaobject it is of. Nobody numbers them: the bits ARE the metaclass's own id, which is 24 bits wide for that
// (metadata_to_clsid, below) — a metaclass a plugin adds has its own the day it registers. 0 is none, and in an
// ANY id, any.
enum ibClassMetaclass : uint32_t { ibClassMetaclass_None = 0 };

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

// Dynamic (CONSTRUCTIVE) id: [63:56] kind | [55:32] metaclass | [31:0] metaID. (kind, metaID) is unique by
// construction, no hash, no possible collision; the metaclass says what the metaID is of.
//
// ⭐ ANY: A FIELD LEFT AT 0 IS EVERY VALUE IT COULD HOLD. A metaID of 0 is no metaobject's — a configuration
// numbers from its root's 1000 up, a synthetic object's is negative — so it is EVERY one of its kind and
// metaclass, and a metaclass of 0 besides makes it every one of its kind. `CatalogRef` is Reference | Catalog | 0,
// `AnyRef` Reference | 0 | 0: a barrier is spelled in the bits of what it admits, and whether it admits a value
// is read off the two ids (clsid_admits, below).
constexpr ibClassID kIbClsidMetaIDMask    = 0xFFFFFFFFULL;
constexpr int       kIbClsidMetaclassShift = 32;
constexpr ibClassID kIbClsidMetaclassMask  = 0xFFFFFFULL;
constexpr ibClassID kIbClsidAnyMetaID     = 0;

// ⭐ A METACLASS'S ID IS 24 BITS OF ITS NAME, IN THE BITS WHERE A VALUE CARRIES ITS METACLASS — so the field reads
// the same off the class's own id and off every value of it (clsid_metaclass, below), and the family read off a
// value names its metaclass exactly. All 64 bits of the name's hash are folded in. Unique as metaclass ids are:
// the registry refuses a second one at startup (ibValue::RegisterCtor), and the platform's 50 names hash apart
// (checked 2026-09-27). Never 0, which is no metaclass.
constexpr ibClassID make_clsid_metaclass(ibClassID nameHash) {
	const ibClassID body = (nameHash ^ (nameHash >> 24) ^ (nameHash >> 48)) & kIbClsidMetaclassMask;
	return (ibClassID(static_cast<unsigned char>(ibClassKind_Metadata)) << 56) | ((body != 0 ? body : 1) << kIbClsidMetaclassShift);
}

constexpr ibClassID make_clsid_dynamic(ibClassID metaID, ibClassKind kind, ibClassMetaclass metaclass = ibClassMetaclass_None) {
	return (ibClassID(static_cast<unsigned char>(kind)) << 56)
		| ((ibClassID(metaclass) & kIbClsidMetaclassMask) << kIbClsidMetaclassShift)
		| (metaID & kIbClsidMetaIDMask);
}

// --- static name→id wrappers — the ergonomic replacements for string_to_clsid at a
//     kind-known callsite: value_to_clsid("DynamicList"), control_to_clsid("Button"), … ---
constexpr ibClassID primitive_to_clsid(const char* n) { return make_clsid(n, ibClassKind_Primitive); }
constexpr ibClassID value_to_clsid    (const char* n) { return make_clsid(n, ibClassKind_Value); }
constexpr ibClassID control_to_clsid  (const char* n) { return make_clsid(n, ibClassKind_Control); }
constexpr ibClassID system_to_clsid   (const char* n) { return make_clsid(n, ibClassKind_System); }
constexpr ibClassID enum_to_clsid     (const char* n) { return make_clsid(n, ibClassKind_Enum); }
constexpr ibClassID context_to_clsid  (const char* n) { return make_clsid(n, ibClassKind_Context); }
constexpr ibClassID metadata_to_clsid (const char* n) { return make_clsid_metaclass(ib_clsid_hash(n)); }
constexpr ibClassID picture_to_clsid  (const char* n) { return make_clsid(n, ibClassKind_Picture); }
// wxString overloads — for callsites passing wxT("XX") (wide literal) or a runtime string.
inline ibClassID primitive_to_clsid(const wxString& n) { return make_clsid(n, ibClassKind_Primitive); }
inline ibClassID value_to_clsid    (const wxString& n) { return make_clsid(n, ibClassKind_Value); }
inline ibClassID control_to_clsid  (const wxString& n) { return make_clsid(n, ibClassKind_Control); }
inline ibClassID system_to_clsid   (const wxString& n) { return make_clsid(n, ibClassKind_System); }
inline ibClassID enum_to_clsid     (const wxString& n) { return make_clsid(n, ibClassKind_Enum); }
inline ibClassID context_to_clsid  (const wxString& n) { return make_clsid(n, ibClassKind_Context); }
inline ibClassID metadata_to_clsid (const wxString& n) { return make_clsid_metaclass(ib_clsid_hash(n)); }
inline ibClassID picture_to_clsid  (const wxString& n) { return make_clsid(n, ibClassKind_Picture); }

// --- dynamic metaID→id wrappers (constructive body) — one per metaobject runtime value ---
constexpr ibClassID reference_to_clsid           (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_Reference, metaclass); }
constexpr ibClassID object_to_clsid              (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_Object, metaclass); }
constexpr ibClassID manager_to_clsid             (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_Manager, metaclass); }
constexpr ibClassID selection_to_clsid           (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_Selection, metaclass); }
constexpr ibClassID tabularSection_to_clsid      (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_TabularSection, metaclass); }
constexpr ibClassID tabularSectionString_to_clsid(ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_TabularSectionString, metaclass); }
constexpr ibClassID characteristic_to_clsid      (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_Characteristic, metaclass); }
constexpr ibClassID recordSet_to_clsid           (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_RecordSet, metaclass); }
constexpr ibClassID recordSetString_to_clsid     (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_RecordSetString, metaclass); }
constexpr ibClassID recordKey_to_clsid           (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_RecordKey, metaclass); }
constexpr ibClassID recordManager_to_clsid       (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_RecordManager, metaclass); }
constexpr ibClassID externalObject_to_clsid      (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_ExternalObject, metaclass); }
constexpr ibClassID externalManager_to_clsid     (ibClassID metaID, ibClassMetaclass metaclass = ibClassMetaclass_None) { return make_clsid_dynamic(metaID,ibClassKind_ExternalManager, metaclass); }

// --- stateless kind reads — no metadata, no registry, just the high byte ---
constexpr ibClassKind clsid_kind (ibClassID c) { return static_cast<ibClassKind>(static_cast<unsigned char>(c >> 56)); }

// THE METAID A DYNAMIC VALUE IS OF — its low 32 bits (make_clsid_dynamic), so the
// metaobject a dynamic value names is readable straight off the bits — no ibMetaData search, no type
// ctor. Worth having a name: a caller that already holds a reference clsid can key on the metaobject
// WITHOUT resolving it, which is the difference between a hash probe and a metadata lookup on a path
// that runs once per reference cell.
//
// ⚠ MEANINGLESS FOR A STATIC ID, whose body is a name hash. Ask the kind first (IsReference / IsObject
// / … ) — every kind from ibClassKind_Reference up is constructive; below it, the body is a hash.
//
// Returns the low 32 bits, not an ibMetaID: this header is self-contained and backend_core.h includes IT,
// so the metaID typedef is not available here and cannot be without inverting that. The caller narrows,
// which is where the decision belongs anyway.
constexpr ibClassID clsid_metaID(ibClassID c) { return c & kIbClsidMetaIDMask; }

// …and the metaclass an id is of — the 24 bits under the kind: a dynamic value's, or a metaclass's own (it stands
// there itself, make_clsid_metaclass), so `g_metaCatalogCLSID` and every catalog's reference answer alike.
constexpr ibClassMetaclass clsid_metaclass(ibClassID c) { return static_cast<ibClassMetaclass>((c >> kIbClsidMetaclassShift) & kIbClsidMetaclassMask); }

constexpr bool IsReference     (ibClassID c) { return clsid_kind(c) == ibClassKind_Reference; }
constexpr bool IsObject        (ibClassID c) { return clsid_kind(c) == ibClassKind_Object || clsid_kind(c) == ibClassKind_ExternalObject; }
constexpr bool IsManager       (ibClassID c) { return clsid_kind(c) == ibClassKind_Manager || clsid_kind(c) == ibClassKind_ExternalManager; }
constexpr bool IsSelection     (ibClassID c) { return clsid_kind(c) == ibClassKind_Selection; }
// NOTE: this is the narrow "tabular section" KIND only — NOT the general "is this a table?"
// predicate. Table-ness spans several meta-kinds (TabularSection(+String) /
// RecordSet(+String)) and is answered through the FACTORY (ibValue::IsTableValue → the ctor's
// IsTableValue), not reconstructed from the clsid byte. Don't use this as "is a table".
constexpr bool IsTabularSection(ibClassID c) { return clsid_kind(c) == ibClassKind_TabularSection; }
constexpr bool IsCharacteristic(ibClassID c) { return clsid_kind(c) == ibClassKind_Characteristic; }
constexpr bool IsMetadata      (ibClassID c) { return clsid_kind(c) == ibClassKind_Metadata; }
constexpr bool IsControl       (ibClassID c) { return clsid_kind(c) == ibClassKind_Control; }
constexpr bool IsValue         (ibClassID c) { return clsid_kind(c) == ibClassKind_Value; }
constexpr bool IsEnum          (ibClassID c) { return clsid_kind(c) == ibClassKind_Enum; }
constexpr bool IsPrimitive     (ibClassID c) { return clsid_kind(c) == ibClassKind_Primitive; }
// Any DYNAMIC metaobject runtime value (Reference / Object / Manager / … / External*) —
// the stateless equivalent of ibCtorObjectType_object_meta_value.
constexpr bool IsMetaValue     (ibClassID c) { return clsid_kind(c) >= ibClassKind_Reference && clsid_kind(c) <= ibClassKind_ExternalManager; }

// ⭐⭐ A BARRIER, AND WHAT IT ADMITS — read off the ids, with no registry and no metadata. A dynamic id whose
// metaID is ANY is every value of its kind and metaclass — of its kind alone when it names no metaclass.
constexpr bool clsid_is_any(ibClassID c) { return IsMetaValue(c) && clsid_metaID(c) == kIbClsidAnyMetaID; }

// …and the barrier a dynamic value belongs to: its own id with the metaID left ANY — `CatalogRef.Goods` is under
// `CatalogRef`. Whether that barrier is registered is the registry's answer: a metaclass keeps one only for the
// kinds it declares (s_features).
constexpr ibClassID clsid_any_of(ibClassID c) { return make_clsid_dynamic(kIbClsidAnyMetaID, clsid_kind(c), clsid_metaclass(c)); }

// ⭐ THE IDS A TYPE ADMITS ARE ONE RANGE, from its own id up to this. A barrier's zeros are the bottom of what
// they stand for and its members fill the rest; any other type is a range of one.
constexpr ibClassID clsid_admitted_max(ibClassID type) {
	return !clsid_is_any(type) ? type
		: type | (clsid_metaclass(type) == ibClassMetaclass_None ? kIbClsidBodyMask : kIbClsidMetaIDMask);
}

// Does a value of class `value` pass as `type`: inside its range — the same class, or a member of the barrier.
// Two comparisons, and the same two a type test is in SQL (`_RTRef BETWEEN type AND clsid_admitted_max`).
constexpr bool clsid_admits(ibClassID type, ibClassID value) {
	return type <= value && value <= clsid_admitted_max(type);
}

// A catalog's and a document's metaclass — their classes' ids (g_metaCatalogCLSID, g_metaDocumentCLSID).
static_assert(clsid_metaclass(metadata_to_clsid("MD_CAT")) != clsid_metaclass(metadata_to_clsid("MD_DOC")), "two metaclasses, two ids");
static_assert(clsid_metaclass(metadata_to_clsid("MD_CAT")) != ibClassMetaclass_None && clsid_metaID(metadata_to_clsid("MD_CAT")) == 0,
	"a metaclass's id carries itself where its values carry it");
static_assert(clsid_admits(make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Reference, clsid_metaclass(metadata_to_clsid("MD_CAT"))),
	reference_to_clsid(42, clsid_metaclass(metadata_to_clsid("MD_CAT")))), "a family admits its member");
static_assert(!clsid_admits(make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Reference, clsid_metaclass(metadata_to_clsid("MD_CAT"))),
	reference_to_clsid(42, clsid_metaclass(metadata_to_clsid("MD_DOC")))), "…and not another metaclass's");
static_assert(clsid_admits(make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Reference),
	reference_to_clsid(42, clsid_metaclass(metadata_to_clsid("MD_DOC")))), "AnyRef admits every reference");
static_assert(!clsid_admits(make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Reference),
	object_to_clsid(42, clsid_metaclass(metadata_to_clsid("MD_DOC")))), "…and nothing that is not one");
static_assert(!clsid_admits(reference_to_clsid(42, clsid_metaclass(metadata_to_clsid("MD_CAT"))),
	reference_to_clsid(43, clsid_metaclass(metadata_to_clsid("MD_CAT")))), "a single type is a range of one");
static_assert(clsid_any_of(reference_to_clsid(42, clsid_metaclass(metadata_to_clsid("MD_CAT"))))
	== make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Reference, clsid_metaclass(metadata_to_clsid("MD_CAT"))),
	"a catalog's reference is under CatalogRef");

#endif
