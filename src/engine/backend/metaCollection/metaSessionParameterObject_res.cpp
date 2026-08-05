#include "metaSessionParameterObject.h"

// THE ATTRIBUTE'S SHAPE, IN ANOTHER COLOUR. A session parameter IS an attribute —
// same editor, same type page, same everything a declaration needs — so the icon
// keeps the attribute's outline and swaps its blue for amber. A different picture
// would claim a different kind of thing; the same picture would hide that this one
// belongs to the session rather than to a table.
//
// Placeholder in the honest sense: the attribute icon with R and B swapped, good
// enough to tell the branches apart until somebody draws one on purpose.

/* PNG */
static const wxString s_sessionParameter_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAAmSURBVDhPY2AYBZSDI9NK/lOCwQb8/3SbLDycDKAEo0fKKCADAAAlhUSAYRLvVAAAAABJRU5ErkJggg==");

wxIcon ibValueMetaObjectSessionParameter::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectSessionParameter::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_sessionParameter_16_png, wxSize(16, 16));

	return icon;
}