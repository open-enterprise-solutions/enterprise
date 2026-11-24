////////////////////////////////////////////////////////////////////////////
//	Description : `Any*` — registered types that create nothing and admit a
//	              whole KIND, plus the default gate every type inherits.
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "core/graphics/picture.h"   // ibPicture::FromSvg — the drawing, at 16

////////////////////////////////////////////////////////////////////////////
// A BARRIER IS A TYPE, not a special case in the interpreter
////////////////////////////////////////////////////////////////////////////
//
// `Any`, `AnyRef`, `AnyControl` are registered like any other type and carry
// their own class id. What makes them different is only what they do with the
// two questions every registrar answers:
//
//   CreateObject — nothing. No value is ever "an AnyRef"; the name exists to be
//                  DECLARED, not instantiated.
//   AllowValue   — a whole KIND passes, including types that do not exist yet.
//                  The kind is the high byte of every class id, so this is one
//                  comparison: no registry, no metadata, no allocation.
//
// That is the reason they are types rather than a branch somewhere: the branch
// would have to be found and edited every time a family grows, and would be
// silently wrong until somebody noticed.
//
// TWO SCOPES, registered in two places:
//
//   * the KIND barriers below — `Any`, `AnyRef`, `AnyControl` — belong to the
//     platform, exist always, and are registered here, once.
//   * the METATYPE families — `CatalogRef`, `DocumentRef`, and whatever ships
//     next — belong to a metatype and arrive WITH it, on its registration event
//     (metaCtor.h). Nobody keeps a list of them: `Catalog` derives the
//     reference-bearing base, so `CatalogRef` follows.
////////////////////////////////////////////////////////////////////////////

namespace {

// Barrier over a KIND: `AnyRef` lets every reference through, `AnyControl`
// every control.
class ibCtorAnyKind : public ibCtorValueTypeBase {
public:
	ibCtorAnyKind(const ibString& className, ibClassKind kind, const ibClassID& clsid, const ibChar* picture = nullptr)
		: ibCtorValueTypeBase(className, typeid(void), clsid), m_kind(kind), m_picture(picture) {
	}

	// Its own picture where it has one — `AnyRef` stands in the type picker beside the families, which show their
	// metatype's. The others are never offered there, and answer none.
	ibPicture GetClassIcon() const override {
		if (m_picture == nullptr)
			return ibPicture();
		return ibPicture::FromSvg(m_picture);
	}

	ibCtorObjectType GetObjectTypeCtor() const override { return ibCtorObjectType::ibCtorObjectType_object_system; }
	ibValue CreateObject() const override { return ibEmptyValue; }

	// EMPTY PASSES (class id 0). A declared parameter nobody passed, a reference
	// not yet filled in — the declaration says what the value IS when there is
	// one; it does not promise there is one (script-language.md §4a).
	bool AllowValue(const ibClassID& clsid) const override {
		return clsid == g_valueUndefinedCLSID || clsid_kind(clsid) == m_kind;
	}

private:
	ibClassKind m_kind;   // the family: the high byte every member carries
	const ibChar* m_picture;   // its SVG; null — no picture
};

// `Any` on its own — declared, restricting nothing. It exists so an author can
// SAY "anything goes here" rather than leave a reader guessing whether the type
// was omitted on purpose.
class ibCtorAnyUnrestricted : public ibCtorValueTypeBase {
public:
	ibCtorAnyUnrestricted()
		: ibCtorValueTypeBase(ibT("Any"), typeid(void), g_valueAnyCLSID) {
	}

	ibCtorObjectType GetObjectTypeCtor() const override { return ibCtorObjectType::ibCtorObjectType_object_system; }
	ibValue CreateObject() const override { return ibEmptyValue; }

	bool AllowValue(const ibClassID&) const override { return true; }
};

} // namespace

// Two chain links: a reference, whichever it is — in the blue of the reference's own families.
static const ibChar s_anyRef_svg[] = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><g transform='rotate(-45 8 8)'><rect x='0.6' y='5.6' width='8.6' height='4.8' rx='2.4' fill='none' stroke='#1c6fd1' stroke-width='1.7'/><rect x='6.8' y='5.6' width='8.6' height='4.8' rx='2.4' fill='none' stroke='#1c6fd1' stroke-width='1.7'/></g></svg>");

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

GENERATE_REGISTER(ibT("Any"), s_cs_reg_any_all, new ibCtorAnyUnrestricted());
// `AnyRef`, `AnyObject`, `AnyManager` are spelled in the bits of what they admit, as `CatalogRef` is
// (metaCtor.h): their kind, no metaclass, any metaID (clsid.h) — so `AnyRef` is stored as a reference, the
// `_RTRef` / `_RRRef` pair, and a type test against it is the range of its kind. The others stay system ids —
// none of them is a type a field is stored as.
GENERATE_REGISTER(ibT("AnyRef"), s_cs_reg_any_ref, new ibCtorAnyKind(ibT("AnyRef"), ibClassKind_Reference, make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Reference), s_anyRef_svg));
GENERATE_REGISTER(ibT("AnyObject"), s_cs_reg_any_obj, new ibCtorAnyKind(ibT("AnyObject"), ibClassKind_Object, make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Object)));
GENERATE_REGISTER(ibT("AnyManager"), s_cs_reg_any_mgr, new ibCtorAnyKind(ibT("AnyManager"), ibClassKind_Manager, make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Manager)));
GENERATE_REGISTER(ibT("AnyControl"), s_cs_reg_any_ctl, new ibCtorAnyKind(ibT("AnyControl"), ibClassKind_Control, system_to_clsid("AnyControl")));
GENERATE_REGISTER(ibT("AnyValue"), s_cs_reg_any_val, new ibCtorAnyKind(ibT("AnyValue"), ibClassKind_Value, system_to_clsid("AnyValue")));
GENERATE_REGISTER(ibT("AnyEnum"), s_cs_reg_any_enm, new ibCtorAnyKind(ibT("AnyEnum"), ibClassKind_Enum, system_to_clsid("AnyEnum")));