#ifndef __POINT_VARIANT_H__
#define __POINT_VARIANT_H__

#include "backend/backend_core.h"
#include "backend/propertyManager/propertyValue.h"
#include "core/graphics/geometry.h"   // ibPoint — the point, as data

// A point in a variant, our own.
//
// wxWidgets does ship one — WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxPoint) in
// wx/propgrid/propgriddefs.h, reachable through `variant << point`. But it belongs to
// PROPGRID: using it made the backend property layer depend on the editor library for a
// pair of ints. Same shape as ibPropertyValueNumber — data in the variant, Eq for change
// detection and diff (docs/private/property-system.md §3, §6.1).
class BACKEND_API ibPropertyValuePoint : public ibPropertyValue {
public:

	void SetPoint(const ibPoint& point) { m_point = point; }
	ibPoint& GetPoint() { return m_point; }

	ibPropertyValuePoint(const ibPoint& point = ibDefaultPosition) : ibPropertyValue(ibValueTypes::TYPE_VALUE), m_point(point) {}

	virtual bool CompareValueEQ(const ibPropertyValue& data) const override {
		const ibPropertyValuePoint* srcData = dynamic_cast<const ibPropertyValuePoint*>(data.GetRef());
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
