#ifndef __POINT_VARIANT_H__
#define __POINT_VARIANT_H__

#include "backend/backend_core.h"
#include "core/variant.h"
#include "core/graphics/geometry.h"   // ibPoint — the point, as data

// A point in a variant, our own.
//
// wxWidgets does ship one — WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxPoint) in
// wx/propgrid/propgriddefs.h, reachable through `variant << point`. But it belongs to
// PROPGRID: using it made the backend property layer depend on the editor library for a
// pair of ints. Same shape as ibVariantNumber — data in the variant, Eq for change
// detection and diff (docs/private/property-system.md §3, §6.1).
class BACKEND_API ibVariantPoint : public ibVariant {
public:

	void SetPoint(const ibPoint& point) { m_point = point; }
	ibPoint& GetPoint() { return m_point; }

	ibVariantPoint(const ibPoint& point = ibDefaultPosition) : ibVariant(ibValueTypes::TYPE_VALUE), m_point(point) {}
	ibVariantPoint(const ibVariantPoint& src) : ibVariant(ibValueTypes::TYPE_VALUE), m_point(src.m_point) {}

	virtual bool CompareValueEQ(const ibVariant& data) const override {
		const ibVariantPoint* srcData = dynamic_cast<const ibVariantPoint*>(data.GetRef());
		if (srcData != nullptr) {
			return m_point == srcData->m_point;
		}
		return false;
	}

	virtual ibString GetString() const override;

private:
	ibPoint m_point;
};

#endif
