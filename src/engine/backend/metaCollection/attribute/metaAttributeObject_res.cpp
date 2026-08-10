#include "metaAttributeObject.h"

/* PNG */
static const wxString s_attribute_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAA3NCSVQICAjb4U/gAAAACVBMVEUAAAF0lsTb8v+YmrAhAAAAAXRSTlMAQObYZgAAABlJREFUGJVjYKAFYEQDDIxMKIA8AXRDaQEAUdAAmeJBSsoAAAAASUVORK5CYII=");

// THE DEFAULT COLUMN PICTURE — declared on the column face (query/queryColumn.h) and defined here,
// beside the picture it hands out. Any column that is not a metaobject — a view's column, a temp
// table's, a synthetic projection — reads as a plain attribute, which is what it is from the
// reader's point of view. Metaobject columns override and wear their own.
wxIcon ibBackendSourceColumn::GetColumnIcon() const
{
	return ibValueMetaObjectAttribute::GetIconGroup();
}

wxIcon ibValueMetaObjectAttribute::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectAttribute::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_attribute_16_png, wxSize(16, 16));

	return icon;
}