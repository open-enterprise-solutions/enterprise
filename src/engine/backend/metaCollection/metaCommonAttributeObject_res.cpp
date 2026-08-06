#include "metaCommonAttributeObject.h"

// THE ATTRIBUTE'S SHAPE AGAIN, in a third colour. A common attribute IS an attribute,
// so the icon keeps the outline; what it has to say is that this one is not owned by
// the object it appears in.
//
// The COPY carries the same picture as the declaration, and that is deliberate: inside
// an object it must read as "an attribute, but not yours" at a glance — the tree is
// where somebody will try to rename or delete it, and the icon is the first thing that
// says where to go instead.
//
// Placeholder in the honest sense: the attribute icon with its channels rotated, good
// enough to tell the branches apart until somebody draws one on purpose.

/* PNG */
static const wxString s_commonAttribute_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAYAAAAf8/9hAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAJcEhZcwAADsMAAA7DAcdvqGQAAAAmSURBVDhPY2AYBZSDI9NK/lOCwQb8/3SbLDycDKAEo0fKKCADAAAlhUSAYRLvVAAAAABJRU5ErkJggg==");

wxIcon ibValueMetaObjectCommonAttribute::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectCommonAttribute::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_commonAttribute_16_png, wxSize(16, 16));

	return icon;
}

wxIcon ibValueMetaObjectCommonAttributeColumn::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectCommonAttributeColumn::GetIconGroup()
{
	// The declaration's — same thing, seen from inside an object.
	return ibValueMetaObjectCommonAttribute::GetIconGroup();
}
