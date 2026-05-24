#ifndef __CLSID_H__
#define __CLSID_H__

typedef unsigned wxLongLong_t ibClassID;

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

// Runtime overload for wxString — used by dynamic ctors (constantCtor.h,
// characteristicCtor.h, objCtor.h: m_classType = ib_clsid_hash("R_" + IntToStr(metaID)))
// and by the legacy-compat string_to_clsid wrapper below.
// utf8_str() returns a scoped buffer valid through the end of the full
// expression; ib_clsid_hash(const char*) reads it and returns by value.
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

// string_to_clsid is now a thin wrapper over ib_clsid_hash. All existing
// callsites continue to work; CLSID values change from packed ASCII to
// FNV-1a 64 hash of the same input string. AOT cache invalidated via
// kAOTFormatVersion bump; persistent DB blobs holding old packed CLSIDs
// must be regenerated.
inline ibClassID string_to_clsid(const wxString& buf) {
	return ib_clsid_hash(buf);
}

#endif
