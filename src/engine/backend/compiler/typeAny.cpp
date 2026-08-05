////////////////////////////////////////////////////////////////////////////
//	Description : `Any*` — registered types that create nothing and admit a
//	              whole KIND, plus the default gate every type inherits.
////////////////////////////////////////////////////////////////////////////

#include "value.h"

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
	ibCtorAnyKind(const wxString& className, ibClassKind kind, const ibClassID& clsid)
		: ibCtorValueTypeBase(className, typeid(void), clsid), m_kind(kind) {
	}

	ibCtorObjectType GetObjectTypeCtor() const override { return ibCtorObjectType::ibCtorObjectType_object_system; }
	ibValue* CreateObject() const override { return nullptr; }

	// EMPTY PASSES (class id 0). A declared parameter nobody passed, a reference
	// not yet filled in — the declaration says what the value IS when there is
	// one; it does not promise there is one (script-language.md §4a).
	bool AllowValue(const ibClassID& clsid) const override {
		return clsid == g_valueUndefinedCLSID || clsid_kind(clsid) == m_kind;
	}

private:
	ibClassKind m_kind;   // the family: the high byte every member carries
};

// `Any` on its own — declared, restricting nothing. It exists so an author can
// SAY "anything goes here" rather than leave a reader guessing whether the type
// was omitted on purpose.
class ibCtorAnyUnrestricted : public ibCtorValueTypeBase {
public:
	ibCtorAnyUnrestricted()
		: ibCtorValueTypeBase(wxT("Any"), typeid(void), system_to_clsid("Any")) {
	}

	ibCtorObjectType GetObjectTypeCtor() const override { return ibCtorObjectType::ibCtorObjectType_object_system; }
	ibValue* CreateObject() const override { return nullptr; }

	bool AllowValue(const ibClassID&) const override { return true; }
};

} // namespace

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

GENERATE_REGISTER(wxT("Any"), s_cs_reg_any_all, new ibCtorAnyUnrestricted());
GENERATE_REGISTER(wxT("AnyRef"), s_cs_reg_any_ref, new ibCtorAnyKind(wxT("AnyRef"), ibClassKind_Reference, system_to_clsid("AnyRef")));
GENERATE_REGISTER(wxT("AnyObject"), s_cs_reg_any_obj, new ibCtorAnyKind(wxT("AnyObject"), ibClassKind_Object, system_to_clsid("AnyObject")));
GENERATE_REGISTER(wxT("AnyManager"), s_cs_reg_any_mgr, new ibCtorAnyKind(wxT("AnyManager"), ibClassKind_Manager, system_to_clsid("AnyManager")));
GENERATE_REGISTER(wxT("AnyControl"), s_cs_reg_any_ctl, new ibCtorAnyKind(wxT("AnyControl"), ibClassKind_Control, system_to_clsid("AnyControl")));
GENERATE_REGISTER(wxT("AnyValue"), s_cs_reg_any_val, new ibCtorAnyKind(wxT("AnyValue"), ibClassKind_Value, system_to_clsid("AnyValue")));
GENERATE_REGISTER(wxT("AnyEnum"), s_cs_reg_any_enm, new ibCtorAnyKind(wxT("AnyEnum"), ibClassKind_Enum, system_to_clsid("AnyEnum")));