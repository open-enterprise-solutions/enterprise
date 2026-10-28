#ifndef __SIZE_VARIANT_H__
#define __SIZE_VARIANT_H__

#include "backend/backend_core.h"
#include "core/variant.h"
#include "core/graphics/geometry.h"   // ibSize — the size, as data

// A size in a variant, our own — see variantPoint.h for why propgrid's
// WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxSize) / `variant << size` is not used.
class BACKEND_API ibVariantSize : public ibVariant {
public:

	void SetSize(const ibSize& size) { m_size = size; }
	ibSize& GetSize() { return m_size; }

	ibVariantSize(const ibSize& size = ibDefaultSize) : ibVariant(ibValueTypes::TYPE_VALUE), m_size(size) {}
	ibVariantSize(const ibVariantSize& src) : ibVariant(ibValueTypes::TYPE_VALUE), m_size(src.m_size) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantSize* srcData = dynamic_cast<const ibVariantSize*>(data.GetRef());
		if (srcData != nullptr) {
			return m_size == srcData->m_size;
		}
		return false;
	}

	virtual ibString GetString() const override;

private:
	ibSize m_size;
};

#endif
