#ifndef __QUERYABLE_FACTORY_H__
#define __QUERYABLE_FACTORY_H__

// Queryable-source FACTORY — a NON-OWNING registry of source DESCRIPTORS, owned by
// ibApplicationData (GetQueryableFactory(); the `query_sources` macro; nullptr
// pre/post-appData — the ibLockManager ownership pattern, token-gated ctor).
//
// The query language covers ONLY the relational metaclasses: records with a data-reference
// (catalogs / documents / charts of characteristic types & accounts / enumerations),
// registers, and constants. Reports & data processors register no descriptor → a query
// against them simply fails to resolve.
//
// A DESCRIPTOR is OWNED BY THE METAOBJECT (a member, like m_queryable), registered with the
// factory by POINTER when the object runs and unregistered by pointer when it closes — no
// name / clsid round-trip. The factory just maps (namespace, name) -> descriptor* and asks
// it to CREATE the queryable.
//
// The standard descriptor is the TEMPLATE ibMetaSourceDescriptor<TQueryable, TMeta> — it
// CONTAINS the metaobject's queryable and replaces its former plain `m_queryable` field. A
// separate table (a register's balance / turnover / slice) or an external source uses its OWN
// descriptor subclass whose CreateQueryable BUILDS a fresh configured queryable from the call
// params (these are registered per concrete register / per external source). (docs §22.0 / §23.)

#include "backend/backend_core.h"     // core prelude (wx set up first) — provides ibClassID + BACKEND_API in order
#include "backend/appDataCtorToken.h" // ib::AppDataCtorToken (owner-only construction)
#include "backend/compiler/value.h"   // ibValue::GetNameObjectFromID (the metaobject's registered name = the namespace token)

#include <wx/string.h>
#include <map>
#include <vector>

class ibBackendQueryable;
class ibBackendQueryableHolder;

// A source DESCRIPTION, identified by (namespace, name): it CREATES the queryable from the
// metadata it stores + the call-scoped params (count + pointer-to-pointer of ibValue — the
// ibValue::Init idiom). No separate setup step — construction happens in CreateQueryable.
class BACKEND_API ibQueryableSourceDescriptor
{
public:
	virtual ~ibQueryableSourceDescriptor() = default;

	virtual wxString GetNamespace() const = 0;
	virtual wxString GetName() const = 0;

	// CREATE the queryable from the call-scoped params (count + pointer-to-pointer of ibValue —
	// the ibValue::Init idiom). NON-const: a parameterized source (a register's balance / slice)
	// builds + configures its call-scoped companion HERE from the params and OWNS the result; a
	// standard source returns its stable contained member. The returned pointer is owned by the
	// descriptor (borrowed by the caller) — valid for the descriptor's life.
	virtual const ibBackendQueryable* CreateQueryable(ibValue** paParams, long lSizeArray) = 0;
};

// The standard descriptor, TEMPLATED on the queryable type TQueryable + the metaobject type
// TMeta. It REPLACES the metaobject's former plain `ibXxxQueryable m_queryable{this}` field:
// the descriptor CONTAINS the queryable (built from the metaobject, like before) AND carries
// the L4 source identity. The metaobject holds ONE field — this descriptor — and its
// GetQueryable() returns the contained queryable. CreateQueryable (the factory path) returns
// the same. GetNamespace / GetName come from the metaobject. Instantiated in the metaobject's
// TU (where the types are complete).
template <typename TQueryable, typename TMeta>
class ibMetaSourceDescriptor : public ibQueryableSourceDescriptor
{
public:
	explicit ibMetaSourceDescriptor(TMeta* meta) : m_meta(meta), m_queryable(meta) {}

	wxString GetNamespace() const override { return ibValue::GetNameObjectFromID(m_meta->GetClassType()); }
	wxString GetName() const override { return m_meta->GetName(); }
	const ibBackendQueryable* CreateQueryable(ibValue** /*paParams*/, long /*lSizeArray*/) override { return &m_queryable; }

	// The contained queryable — the metaobject's GetQueryable() forwards here (stable for the
	// object's life, as the former plain member was).
	const ibBackendQueryable* GetQueryable() const { return &m_queryable; }

protected:
	TMeta*     m_meta;        // for name / clsid + to build the queryable (non-const, like the old `this`)
	TQueryable m_queryable;   // the contained queryable (ibRecordQueryable / ibRegisterDataQueryable / …)
};

class BACKEND_API ibQueryableFactory
{
public:
	// Construction restricted to ibApplicationData via the ib::AppDataCtorToken gate
	// (mirrors ibLockManager). Reached through ibApplicationData::GetQueryableFactory();
	// dtor public for unique_ptr's default_delete.
	explicit ibQueryableFactory(ib::AppDataCtorToken);
	~ibQueryableFactory() = default;

	// Register / unregister a descriptor BY POINTER (the descriptor is owned by the metaobject
	// / external source — the factory only references it). Register keys on the descriptor's
	// (namespace, name); Unregister drops it only if it is the SAME pointer (a baseline object
	// closing can't drop the active one's same-named descriptor).
	void Register(ibQueryableSourceDescriptor* descriptor);
	void Unregister(ibQueryableSourceDescriptor* descriptor);
	// Drop ALL references (config teardown). Does NOT delete — descriptors are owned elsewhere.
	void Clear();

	bool HasNamespace(const wxString& ns) const;

	// Resolve <ns>.<objectName> (+ call-scoped params) -> queryable: find the (ns,name)
	// descriptor and ask it to CREATE the queryable. Null when unknown.
	const ibBackendQueryable* Resolve(const wxString& ns, const wxString& objectName,
	                                  ibValue** paParams = nullptr, long lSizeArray = 0) const;

	// Enumerate registered source descriptors — the dynamic-list source picker lists
	// them by GetNamespace()/GetName(). Non-owning pointers, valid for owners' life.
	std::vector<ibQueryableSourceDescriptor*> GetDescriptors() const;

	// Resolve a source by its table id (queryable->GetQueryTableId()) — the stable id the
	// dynamic-source property serializes. Null when none matches.
	const ibBackendQueryable* ResolveById(ibMetaID tableId) const;

private:
	ibQueryableFactory(const ibQueryableFactory&) = delete;
	ibQueryableFactory& operator=(const ibQueryableFactory&) = delete;

	static wxString Key(const wxString& ns, const wxString& name);   // upper("ns|name")
	std::map<wxString, ibQueryableSourceDescriptor*> m_descriptors;  // non-owning
};

#endif
