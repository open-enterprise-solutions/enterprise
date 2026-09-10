#include "chartOfCalculationTypes.h"

/* PNG - chart of calculation types icon 16x16 (attribute-style, blue) */
static const wxString s_chartOfCalculationTypes_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAA3NCSVQICAjb4U/gAAAACVBMVEUAAAF0lsTb8v+YmrAhAAAAAXRSTlMAQObYZgAAABlJREFUGJVjYKAFYEQDDIxMKIA8AXRDaQEAUdAAmeJBSsoAAAAASUVORK5CYII=");

wxIcon ibValueMetaObjectChartOfCalculationTypes::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectChartOfCalculationTypes::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_chartOfCalculationTypes_16_png, wxSize(16, 16));

	return icon;
}
