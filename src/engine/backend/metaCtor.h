#ifndef _META_CTOR_H__
#define _META_CTOR_H__

#include "backend/compiler/typeCtor.h"  // ibCtorValueTypeBase, IB_DISPATCH, ib_clsid_hash
#include "backend/compiler/value.h"     // ibValue::GetAvailableCtor / RegisterCtor
#include "backend/objCtorDefs.h"        // prefixReference / prefixObject / prefixManager / prefixRecordSet

#include <set>
#include <vector>

////////////////////////////////////////////////////////////////////////////
// `CatalogRef`, `DocumentObject`, `CatalogManager` — every type of ONE KIND of ONE METATYPE
////////////////////////////////////////////////////////////////////////////
//
// `CatalogRef.Item` is one catalog's reference; `CatalogRef` is the family of
// them all — narrower than `AnyRef`, wider than any single catalog. Declaring it
// says "a reference to some catalog, I do not care which", which is what a
// posting routine or a generic handler actually means. The same holds for the
// other kinds a metatype has: `DocumentObject` is every document's object,
// `DocumentManager` every document's manager, `AccumulationRegisterRecordSet`
// every accumulation register's record set — which is what an event handler
// names as its source.
//
// A BARRIER: it creates nothing (no value is ever "a CatalogRef") and its gate
// lets through the types of ITS metatype and kind — a catalog's reference, not a
// document's, and not the catalog's object.
//
// The kind alone cannot tell those apart: every reference carries kind
// Reference — the family's own id among them — and the rest of its class id is
// a metaID that says WHICH metaobject, not which kind of one. So this barrier keeps its own membership,
// and the members ARRIVE ON THEIR OWN: registering a catalog reference adds it,
// unregistering removes it (objCtor.h). No metadata lookup on the hot path — the
// answer was recorded when the type came into existence.
class BACKEND_API ibCtorMetaAnyKind : public ibCtorValueTypeBase {
public:

	// The family's name: the metatype's name and its members' prefix without the dot — `Catalog` and
	// `Ref.` make `CatalogRef`, `Document` and `Object.` make `DocumentObject`. The name a MEMBER is
	// written with is exactly this and `.Item`: the dot is what separates a type from the metaobject
	// it belongs to, and a family belongs to none.
	static wxString NameOf(const wxString& metaClassName, ibCtorObjectMetaType memberKind) {
		return metaClassName + wxString(PrefixOf(memberKind)).BeforeLast(wxT('.'));
	}

	// The kinds a family is kept for, and the prefix each is written with (objCtorDefs.h).
	static const wxChar* PrefixOf(ibCtorObjectMetaType memberKind) {
		switch (memberKind) {
		case ibCtorObjectMetaType::ibCtorObjectMetaType_Reference: return prefixReference;
		case ibCtorObjectMetaType::ibCtorObjectMetaType_Object:    return prefixObject;
		case ibCtorObjectMetaType::ibCtorObjectMetaType_Manager:   return prefixManager;
		case ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet: return prefixRecordSet;
		default:                                                   return wxT("");
		}
	}

	// ⭐ OF ITS MEMBERS' KIND — `CatalogRef` is a reference like every catalog's, it only does not say which
	// catalog. Whatever reads the kind off a declared type takes it for what it holds: a field declared
	// `CatalogRef` is stored as a reference (the `_RTRef` / `_RRRef` pair, columnSpread), with no second
	// question asked anywhere. WHICH ones it admits is still its gate's answer, below.
	ibCtorMetaAnyKind(const wxString& className, ibCtorObjectMetaType memberKind, const ibCtorAbstractType* metaType)
		: ibCtorValueTypeBase(className, typeid(void), make_clsid(className, metatype_to_kind(memberKind))),
		m_metaType(metaType) {
	}

	// ITS METATYPE'S PICTURE — `DocumentObject` is every document's object, and it is shown as a document.
	// A type with none answers wxNullIcon, which the type picker put into its image list: one "Couldn't add an
	// image to the image list" per family, the moment an event handler's Source was opened.
	virtual wxIcon GetClassIcon() const override { return m_metaType != nullptr ? m_metaType->GetClassIcon() : wxNullIcon; }

	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_system; }
	virtual ibValue CreateObject() const { return wxEmptyValue; }

	// EMPTY PASSES (class id 0), as everywhere: a declaration says what a value
	// IS when there is one, not that there is one.
	virtual bool AllowValue(const ibClassID& clsid) const override {
		return clsid == g_valueUndefinedCLSID || m_members.find(clsid) != m_members.end();
	}

	// Told by the member itself, as it registers and unregisters.
	void AddMember(const ibClassID& clsid) { m_members.insert(clsid); }
	void RemoveMember(const ibClassID& clsid) { m_members.erase(clsid); }

private:
	const ibCtorAbstractType* const m_metaType;   // the metatype that registered it — and unregisters it before itself
	std::set<ibClassID> m_members;
};

// The family of one kind of a metatype, by the metatype's own name and the kind — `Catalog` and a
// reference -> `CatalogRef`. Null when the metatype has none (it has no types of that kind, or nothing
// has registered yet), which callers treat as "nothing to tell".
inline ibCtorMetaAnyKind* ib_find_meta_any_kind(const wxString& metaClassName, ibCtorObjectMetaType memberKind) {
	// A kind none is kept for (a selection, a tabular section) is not looked up: its name would be the
	// metatype's own. Every type registered asks this (ibCtorMetaValueType::CallEvent).
	if (wxIsEmpty(ibCtorMetaAnyKind::PrefixOf(memberKind)))
		return nullptr;
	return dynamic_cast<ibCtorMetaAnyKind*>(
		ibValue::GetAvailableCtor(ibCtorMetaAnyKind::NameOf(metaClassName, memberKind)));
}

//metaobject register document, form, etc ...
template <class T>
class ibCtorMetaType : public ibCtorValueTypeBase {
public:

	ibCtorMetaType(const wxString& className, const ibClassID& clsid) :ibCtorValueTypeBase(className, typeid(T), clsid) {}

	virtual wxIcon GetClassIcon() const { return T::GetIconGroup(); }
	virtual ibCtorObjectType GetObjectTypeCtor() const { return ibCtorObjectType::ibCtorObjectType_object_metadata; }

	// ⭐ THE "ANY" OF EACH KIND ARRIVES WITH THE METATYPE, on its registration — `Catalog` registers, `CatalogRef`
	// appears — because it belongs to the METATYPE, not to any object of it: `CatalogRef` is a legal thing to
	// declare in a configuration that has no catalogs yet. One per kind the metatype HAS — a reference, an
	// object, a manager, a record set — and which it has is not a list anybody maintains: each class declares
	// it (s_features, metaObject.h), asked as a CLASS constant rather than std::is_base_of so this header needs
	// no metaobject definition. A kind it lacks is the empty branch and costs nothing.
	virtual void CallEvent(ibCtorObjectTypeEvent event) {
		if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register) {
			T::OnRegisterObject(GetClassName(), this);
			if (!m_anyKinds.empty())
				return;
			const auto registerAnyKind = [this](ibCtorObjectMetaType kind) {
				m_anyKinds.push_back(new ibCtorMetaAnyKind(ibCtorMetaAnyKind::NameOf(GetClassName(), kind), kind, this));
				ibValue::RegisterCtor(m_anyKinds.back());
			};
			if constexpr ((T::s_features & T::ibMetaFeature_Reference) != 0)
				registerAnyKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
			if constexpr ((T::s_features & T::ibMetaFeature_Object) != 0)
				registerAnyKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Object);
			if constexpr ((T::s_features & T::ibMetaFeature_Manager) != 0)
				registerAnyKind(ibCtorObjectMetaType::ibCtorObjectMetaType_Manager);
			if constexpr ((T::s_features & T::ibMetaFeature_RecordSet) != 0)
				registerAnyKind(ibCtorObjectMetaType::ibCtorObjectMetaType_RecordSet);
		}
		else if (event == ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister) {
			// UnRegisterCtor nulls the BASE pointer it is handed once the registry frees it — a copy of that type.
			for (ibCtorAbstractType* anyKind : m_anyKinds)
				ibValue::UnRegisterCtor(anyKind);
			m_anyKinds.clear();
			T::OnUnRegisterObject(GetClassName());
		}
	}

	virtual ibValue CreateObject() const { return new T(); }

private:
	std::vector<ibCtorMetaAnyKind*> m_anyKinds;
};

// 3-arg (legacy): explicit clsid.
#define METADATA_TYPE_REGISTER_3(class_info, class_name, clsid)\
GENERATE_REGISTER(wxT(class_name), wxMAKE_UNIQUE_NAME(s_cs_reg_m_), new ibCtorMetaType<class_info>(wxT(class_name), clsid))
// 2-arg (new): clsid = ib_clsid_hash(class_name).
#define METADATA_TYPE_REGISTER_2(class_info, class_name)\
METADATA_TYPE_REGISTER_3(class_info, class_name, metadata_to_clsid(class_name))
#define METADATA_TYPE_REGISTER(...) IB_DISPATCH(METADATA_TYPE_REGISTER_, __VA_ARGS__)

#endif
