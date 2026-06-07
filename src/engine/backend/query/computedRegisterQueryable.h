#ifndef __COMPUTED_REGISTER_QUERYABLE_H__
#define __COMPUTED_REGISTER_QUERYABLE_H__

// ibComputedRegisterQueryable<TReg> — the shared base for a register's call-scoped,
// RAM-computed virtual table (the information-register SLICE, the accumulation-register
// BALANCE / TURNOVER). All of them are the same shape: IsComputedInRam() is true and the
// eight navigation methods forward to the register's OWN queryable (a computed table
// returns the register's columns), so the only thing a concrete subclass adds is its
// call-scoped filters (in the ctor) + ComputeRows (which calls the register's compute).
// TReg = ibValueMetaObjectInformationRegister / ibValueMetaObjectAccumulationRegister —
// both derive ibValueMetaObjectRegisterData, which vends GetQueryable(). The forwarding
// lived duplicated in two register families before this base.

#include "queryable.h"   // ibBackendQueryable + ibQueryCondition / ibQuerySortItem

template <typename TReg>
class ibComputedRegisterQueryable : public ibBackendQueryable
{
public:
	explicit ibComputedRegisterQueryable(const TReg* reg) : m_reg(reg) {}

	// computed (RAM) virtual table — the door builds the selection from ComputeRows(),
	// never a physical scan (the concrete subclass overrides ComputeRows).
	virtual bool IsComputedInRam() const override { return true; }

	// navigation forwards to the register's own queryable — a computed table carries the
	// register's columns, so it navigates exactly like the register.
	virtual const ibValueMetaObjectAttributeBase* ResolveAttribute(const wxString& name) const override { return m_reg->GetQueryable()->ResolveAttribute(name); }
	virtual const ibValueMetaObjectAttributeBase* ResolveAttribute(const ibMetaID& id)   const override { return m_reg->GetQueryable()->ResolveAttribute(id); }
	virtual wxString GetQueryTableName() const override { return m_reg->GetQueryable()->GetQueryTableName(); }
	virtual ibMetaID GetQueryMetaID()    const override { return m_reg->GetQueryable()->GetQueryMetaID(); }
	virtual wxString GetRowKeyColumn()   const override { return m_reg->GetQueryable()->GetRowKeyColumn(); }
	virtual std::vector<ibQuerySortItem> GetIdentitySort() const override { return m_reg->GetQueryable()->GetIdentitySort(); }
	virtual bool IsReferenceAttribute(const ibMetaID& id) const override { return m_reg->GetQueryable()->IsReferenceAttribute(id); }
	virtual wxString MaterializeRowKey(ibDatabaseResultSet* rs) const override { return m_reg->GetQueryable()->MaterializeRowKey(rs); }
	virtual ibValue MaterializeAttribute(const ibValueMetaObjectAttributeBase* attr, ibDatabaseResultSet* rs) const override { return m_reg->GetQueryable()->MaterializeAttribute(attr, rs); }

protected:
	const TReg* m_reg;
};

#endif // __COMPUTED_REGISTER_QUERYABLE_H__
