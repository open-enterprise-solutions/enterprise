#ifndef __POINT_VARIANT_H__
#define __POINT_VARIANT_H__

#include "backend/backend_core.h"

// wxPoint in a variant, our own.
//
// wxWidgets does ship one — WX_PG_DECLARE_VARIANT_DATA_EXPORTED(wxPoint) in
// wx/propgrid/propgriddefs.h, reachable through `variant << point`. But it belongs to
// PROPGRID: using it made the backend property layer depend on the editor library for a
// pair of ints. Same shape as ibVariantDataNumber — data in the variant, Eq for change
// detection and diff (docs/property-system.md §3, §6.1).
class BACKEND_API ibVariantDataPoint : public wxVariantData {
	wxString MakeString() const;
public:

	void SetPoint(const wxPoint& point) { m_point = point; }
	wxPoint& GetPoint() { return m_point; }

	ibVariantDataPoint(const wxPoint& point = wxDefaultPosition) : m_point(point) {}

	virtual bool Eq(wxVariantData& data) const {
		ibVariantDataPoint* srcData = dynamic_cast<ibVariantDataPoint*>(&data);
		if (srcData != nullptr) {
			return m_point == srcData->GetPoint();
		}
		return false;
	}

#if wxUSE_STD_IOSTREAM
	virtual bool Write(wxSTD ostream& str) const {
		str << MakeString();
		return true;
	}
#endif

	virtual bool Write(wxString& str) const {
		str = MakeString();
		return true;
	}

	virtual wxString GetType() const { return wxT("ibVariantDataPoint"); }

private:
	wxPoint m_point;
};

#endif
