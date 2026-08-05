#ifndef _META_CTOR_H__
#define _META_CTOR_H__

#include "backend/compiler/typeCtor.h"  // ibCtorValueTypeBase, IB_DISPATCH, ib_clsid_hash
#include "backend/compiler/value.h"     // ibValue::GetAvailableCtor / RegisterCtor

#include <set>

////////////////////////////////////////////////////////////////////////////
// `CatalogRef` — every reference of ONE METATYPE
////////////////////////////////////////////////////////////////////////////
//
// `CatalogRef.Item` is one catalog's reference; `CatalogRef` is the family of
// them all — narrower than `AnyRef`, wider than any single catalog. Declaring it
// says "a reference to some catalog, I do not care which", which is what a
// posting routine or a generic handler actually means.
//
// A BARRIER: it creates nothing (no value is ever "a CatalogRef") and its gate
// lets through the references of ITS metatype — a catalog's, not a document's.
//
// The kind alone cannot tell those apart: every reference carries kind
// Reference, and the rest of its class id is a metaID that says WHICH
// metaobject, not which kind of one. So this barrier keeps its own membership,
// and the members ARRIVE ON THEIR OWN: registering a catalog reference adds it,
// unregistering removes it (objCtor.h). No metadata lookup on the hot path — the
// answer was recorded when the type came into existence.
class BACKEND_API ibCtorMetaAnyReference : public ibCtorValueTypeBase {
public:

	// The suffix that turns a metatype name into its family name: `Catalog` ->
	// `CatalogRef`. No dot, deliberately — `CatalogRef.Item` names ONE catalog's
	// reference, and the dot is what separates a type from the metaobject it
	// belongs to. A family belongs to none.
	static const wxChar* Suffix() { return wxT("Ref"); }

	ibCtorMetaAnyReference(const wxString& className)
		: ibCtorValueTypeBase(className, typeid(void), make_clsid(className, ibClassKind_System)) {
	}

	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_system; }
	virtual ibValue* CreateObject() const { return nullptr; }

	// EMPTY PASSES (class id 0), as everywhere: a declaration says what a value
	// IS when there is one, not that there is one.
	virtual bool AllowValue(const ibClassID& clsid) const override {
		return clsid == g_valueUndefinedCLSID || m_members.find(clsid) != m_members.end();
	}

	// Told by the reference itself, as it registers and unregisters.
	void AddMember(const ibClassID& clsid) { m_members.insert(clsid); }
	void RemoveMember(const ibClassID& clsid) { m_members.erase(clsid); }

private:
	std::set<ibClassID> m_members;
};

// The family a reference belongs to, by the metatype's own name — `Catalog` ->
// `CatalogRef`. Null when the metatype has none (it is not reference-bearing, or
// nothing has registered yet), which callers treat as "nothing to tell".
inline ibCtorMetaAnyReference* ib_find_any_reference(const wxString& metaClassName) {
	return dynamic_cast<ibCtorMetaAnyReference*>(
		ibValue::GetAvailableCtor(metaClassName + ibCtorMetaAnyReference::Suffix()));
}

//metaobject register document, form, etc ...
template <class T>
class ibCtorMetaType : public ibCtorValueTypeBase {

	// THE FAMILY ARRIVES WITH THE METATYPE, on the registration event below —
	// `Catalog` registers, `CatalogRef` appears. Which metatypes have one is not
	// a list anybody maintains: each class declares WHAT IT HAS (s_features,
	// metaObject.h), and everything deriving it inherits the set. A register, a
	// form, an external data processor take the empty branch and cost nothing.
	//
	// Asked as a CLASS constant rather than std::is_base_of so this header needs
	// no metaobject definition — metaObject.h already includes THIS one, and the
	// other direction would close the circle.
	//
	// Registered here rather than by each catalog at run time because the family
	// belongs to the METATYPE, not to any object of it: `CatalogRef` is a legal
	// thing to declare in a configuration that has no catalogs yet.
	void RegisterAnyReference() {
		if constexpr ((T::s_features & T::ibMetaFeature_Reference) != 0) {
			if (m_anyReference == nullptr) {
				m_anyReference = new ibCtorMetaAnyReference(GetClassName() + ibCtorMetaAnyReference::Suffix());
				ibValue::RegisterCtor(m_anyReference);
			}
		}
	}

	void UnRegisterAnyReference() {
		if (m_anyReference != nullptr) {
			// UnRegisterCtor takes a reference to a BASE pointer (it nulls the
			// caller's copy after freeing), and a derived pointer does not bind to
			// that — so hand it one of the type it can write through.
			ibCtorAbstractType* ctor = m_anyReference;
			ibValue::UnRegisterCtor(ctor);
			m_anyReference = nullptr;   // the registry owns it — freed there
		}
	}

public:

	ibCtorMetaType(const wxString& className, const ibClassID& clsid) :ibCtorValueTypeBase(className, typeid(T), clsid) {}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_metadata; }

	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register) {
			T::OnRegisterObject(GetClassName(), this);
			RegisterAnyReference();
		}
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister) {
			UnRegisterAnyReference();
			T::OnUnRegisterObject(GetClassName());
		}
	}

	virtual ibValue* CreateObject() const { return new T(); }

private:
	ibCtorMetaAnyReference* m_anyReference = nullptr;
};

// 3-arg (legacy): explicit clsid.
#define METADATA_TYPE_REGISTER_3(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_m_), new ibCtorMetaType<class_info>(wxT(class_name), clsid))
// 2-arg (new): clsid = ib_clsid_hash(class_name).
#define METADATA_TYPE_REGISTER_2(class_info, class_name)\
METADATA_TYPE_REGISTER_3(class_info, class_name, metadata_to_clsid(class_name))
#define METADATA_TYPE_REGISTER(...) IB_DISPATCH(METADATA_TYPE_REGISTER_, __VA_ARGS__)

#endif
