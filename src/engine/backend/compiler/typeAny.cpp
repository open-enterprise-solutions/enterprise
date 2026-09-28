////////////////////////////////////////////////////////////////////////////
//	Description : `Any*` — registered types that create nothing and admit a
//	              whole KIND, plus the default gate every type inherits.
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "backend/backend_picture.h"   // ibBackendPicture::GetIconFromBase64 — AnyRef's own picture

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
	ibCtorAnyKind(const wxString& className, ibClassKind kind, const ibClassID& clsid, const wxChar* picture = nullptr)
		: ibCtorValueTypeBase(className, typeid(void), clsid), m_kind(kind), m_picture(picture) {
	}

	// Its own picture where it has one — `AnyRef` stands in the type picker beside the families, which show their
	// metatype's. The others are never offered there, and answer none.
	wxIcon GetClassIcon() const override {
		if (m_picture == nullptr)
			return wxNullIcon;
		if (!m_icon.IsOk())
			m_icon = ibBackendPicture::GetIconFromBase64(m_picture, wxSize(16, 16));
		return m_icon;
	}

	ibCtorObjectType GetObjectTypeCtor() const override { return ibCtorObjectType::ibCtorObjectType_object_system; }
	ibValue CreateObject() const override { return wxEmptyValue; }

	// EMPTY PASSES (class id 0). A declared parameter nobody passed, a reference
	// not yet filled in — the declaration says what the value IS when there is
	// one; it does not promise there is one (script-language.md §4a).
	bool AllowValue(const ibClassID& clsid) const override {
		return clsid == g_valueUndefinedCLSID || clsid_kind(clsid) == m_kind;
	}

private:
	ibClassKind m_kind;   // the family: the high byte every member carries
	const wxChar* m_picture;   // PNG, base64; null — no picture
	mutable wxIcon m_icon;     // …decoded on the first ask, when the image handlers are there
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
	ibValue CreateObject() const override { return wxEmptyValue; }

	bool AllowValue(const ibClassID&) const override { return true; }
};

} // namespace

/* PNG — two chain links: a reference, whichever it is. The flat style of the families beside it, in a hue of its own */
static const wxChar s_anyRef_16_png[] = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAADTElEQVR42u2av67TMBTG7yP0EfocmSoxI2VixU8QRUhMgJQBJiQ6IjGkApYLAxnuwILIcAEJCdEBBiQkOrAwIPIIRl/lIPf0OE1C7di+jWQpbRLHv2P7/Pnas7PTEdaRZOUsyco0ycoiycplkpV1kpUr9VkkWTmPFXyhQGWPtoYxYprxqic4bRsYLvRZb0bC660IEV6YgK7feyHvPL2Uj6rP8uX7H/Lx6y/ywflHeeP+qy4jrIKHv/nwYgv85uuvf9D47taTt9vzi08/t9fwOdiVYILHbH/4/qcLbsdIaNduP+fuWQQHj9nG7BqA2NauCOaZTfTwuhGwEphrIgb4jUp+EClyU6iEAZhtsw4dPu8bNuET4BiZPuahwoseucPOM+iLCZEiOnit74r6AjRvQqJNeNV/rj8LH4C+vUiMbMNz26DNDUifdZTw6j0F3QJIpEi/yykKG+vw6l0b+g7UDpP5AFXSNo7gc9oXwiAKKPJ96tIAlSP4vS0GB2jIBmeTLX3sRxfw6B8FFJzgZBGAylgYDAblAh5GZpyfu4pQ7f0+ubkVeCb2o1UuZz+lSo4hL88dwTfW977+AhqPEYqYgW0cwdtd+pp6K7TvltT5HSMnHwkvbHv6hlpZ/Wixs/8Zj6zfn4YIL0wJBo0AGCDjAHOSxKxCht9Z0lxOzmyBisnk0lDh5d1n75ameyBKYMBDnVMw8BjM5bfftXbfnMvLGT/QmIzA5fbewmvprR4K1z1zc6miSK6cakGrulDgJQmFgssGGV9wsIUCv6e20JlsQYYYIST4PdmZqwjxTPsTFuMT9kpaFFCGwsZLeHFIptLh4BjbFYHPMAjO8Q5cO2Ak/+FNiRENkS00gDHbqB0YJSdM+EMrYWBrbBc2tnX7BRfierbKaknrWL0VNE/o+oeHdSXHpXpL3jtXxigUaK1K6UKJKs4ETFfqrX//5ZtAva3V+cwXA0ym3qKAQhWpLfmFU+Oc1NurqN52JSdRq7eGQV4N9bZjoIPU26jgx6i3UcGPVW+jgY9avR2Yi8el3o4wQjzq7bHidZDq7X8aIWz11kZFGIx6ayskBqHe2k6MvFZvXa8Er9RbRz7BT/V2ghDpj3o7ccY4vXp7Oo53/AXLECU7NCFXOwAAAABJRU5ErkJggg==");

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

GENERATE_REGISTER(wxT("Any"), s_cs_reg_any_all, new ibCtorAnyUnrestricted());
// `AnyRef`, `AnyObject`, `AnyManager` are spelled in the bits of what they admit, as `CatalogRef` is
// (metaCtor.h): their kind, no metaclass, any metaID (clsid.h) — so `AnyRef` is stored as a reference, the
// `_RTRef` / `_RRRef` pair, and a type test against it is the range of its kind. The others stay system ids —
// none of them is a type a field is stored as.
GENERATE_REGISTER(wxT("AnyRef"), s_cs_reg_any_ref, new ibCtorAnyKind(wxT("AnyRef"), ibClassKind_Reference, make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Reference), s_anyRef_16_png));
GENERATE_REGISTER(wxT("AnyObject"), s_cs_reg_any_obj, new ibCtorAnyKind(wxT("AnyObject"), ibClassKind_Object, make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Object)));
GENERATE_REGISTER(wxT("AnyManager"), s_cs_reg_any_mgr, new ibCtorAnyKind(wxT("AnyManager"), ibClassKind_Manager, make_clsid_dynamic(kIbClsidAnyMetaID, ibClassKind_Manager)));
GENERATE_REGISTER(wxT("AnyControl"), s_cs_reg_any_ctl, new ibCtorAnyKind(wxT("AnyControl"), ibClassKind_Control, system_to_clsid("AnyControl")));
GENERATE_REGISTER(wxT("AnyValue"), s_cs_reg_any_val, new ibCtorAnyKind(wxT("AnyValue"), ibClassKind_Value, system_to_clsid("AnyValue")));
GENERATE_REGISTER(wxT("AnyEnum"), s_cs_reg_any_enm, new ibCtorAnyKind(wxT("AnyEnum"), ibClassKind_Enum, system_to_clsid("AnyEnum")));