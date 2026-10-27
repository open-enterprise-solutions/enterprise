#ifndef __SIZE_VARIANT_H__
#define __SIZE_VARIANT_H__

#include "backend/backend_core.h"
#include "backend/propertyManager/propertyValue.h"
#include "core/graphics/geometry.h"   // ibSize — the size, as data

// A size in a variant, our own — see variantPoint.h for why propgrid's
// WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxSize) / `variant << size` is not used.
class BACKEND_API ibPropertyValueSize : public ibPropertyValue {
public:

	void SetSize(const ibSize& size) { m_size = size; }
	ibSize& GetSize() { return m_size; }

	ibPropertyValueSize(const ibSize& size = ibDefaultSize) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_size(size) {}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValueSize* srcData = dynamic_cast<const ibPropertyValueSize*>(data.GetRef());
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
