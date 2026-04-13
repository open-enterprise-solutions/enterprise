#include "chartOfAccounts.h"

/* PNG - chart of accounts icon 16x16 (uses document icon) */
static const wxString s_chartOfAccounts_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAA3NCSVQICAjb4U/gAAAAIVBMVEUAAAF4i5x9j5////+Mnat9kJ/29/iOnaz09veLm6l9j6C1uLjiAAAAAXRSTlMAQObYZgAAADhJREFUGJVjYGCEAyYGMGBkhgJGFlY0ATZ2JlQBZg5OLiQBiDnIKsCiuAQgygmpoNQMVAEUwIABAB6/AfGpRQdOAAAAAElFTkSuQmCC");

wxIcon ibValueMetaObjectChartOfAccounts::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectChartOfAccounts::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_chartOfAccounts_16_png, wxSize(16, 16));
	return icon;
}
