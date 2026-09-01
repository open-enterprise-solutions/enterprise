#ifndef __METADESC_VARIANT_H__
#define __METADESC_VARIANT_H__

#include "backend/metaData.h"

// ==========================================================================
// ibVariantDataMetaDesc — THE VALUE OF A RELATIONSHIP.
//
// Three variant classes carried a metadescription and an owner: Owner, Record, Generation (the
// charts of accounts and of characteristic types reuse Owner's). They were the same class written
// three times — same two members, same accessors, same everything but the string they render and
// the type name they report.
//
// Being three, they could only be recognised three times, which is how `metadata_bind` came to ask
// `dynamic_cast` once per member and how it came to miss the two it did not know about. One base
// makes "does this variant hold a relationship" a single question with a single answer.
//
// ⚠ THE OWNER IS A PROPERTY OBJECT. All any of them ever did with it was call GetMetaData(), which
// ibPropertyObject answers virtually — the narrower ibValueMetaObjectGenericData* it used to take
// forced a dynamic_cast at every callsite for nothing.
// ==========================================================================
class BACKEND_API ibVariantDataMetaDesc : public wxVariantData {
public:

	ibMetaDescription& GetMetaDesc() { return m_metaDesc; }
	const ibMetaDescription& GetMetaDesc() const { return m_metaDesc; }

	const ibPropertyObject* GetOwnerProperty() const { return m_ownerProperty; }

	// 🛑 EACH OF THE THREE USED TO SAY `return true` — every such variant equal to every other,
	// including to one of a different kind. Comparison is the cheapest thing anything can do with a
	// variant and the only way to ask "is this already the value" or "did my write land", and both
	// answered yes unconditionally.
	//
	// The value IS the metadescription: the same metaIDs in the same order is the same relationship.
	// GetType() is compared too, so an owner binding and a generation binding holding the same ids
	// are still not the same value.
	virtual bool Eq(wxVariantData& data) const override
	{
		const ibVariantDataMetaDesc* other = dynamic_cast<const ibVariantDataMetaDesc*>(&data);
		return other != nullptr
			&& GetType() == other->GetType()
			&& m_metaDesc.m_listMetaClass == other->m_metaDesc.m_listMetaClass;
	}

protected:

	ibVariantDataMetaDesc(const ibPropertyObject* prop, const ibMetaDescription& typeDesc)
		: wxVariantData(), m_ownerProperty(prop), m_metaDesc(typeDesc) {}

	const ibPropertyObject* m_ownerProperty;
	ibMetaDescription       m_metaDesc;
};

#endif
