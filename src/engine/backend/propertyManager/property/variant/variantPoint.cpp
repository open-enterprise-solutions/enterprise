#include "variantPoint.h"

wxString ibVariantDataPoint::MakeString() const
{
	return typeConv::PointToString(m_point);
}
