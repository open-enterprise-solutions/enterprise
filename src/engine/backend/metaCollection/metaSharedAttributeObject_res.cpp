#include "metaSharedAttributeObject.h"

// THE ATTRIBUTE'S SHAPE AGAIN, in a third colour. A shared attribute IS an attribute,
// so the icon keeps the plate — two of them, one behind the other, in cyan; what it
// has to say is that this one is not owned by the object it appears in.
//
// The COPY carries the same picture as the declaration, and that is deliberate: inside
// an object it must read as "an attribute, but not yours" at a glance — the tree is
// where somebody will try to rename or delete it, and the icon is the first thing that
// says where to go instead.

static const ibString s_sharedAttribute_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='3' y='1.5' width='12' height='7' rx='2' fill='#66d9e8'/><rect x='1' y='6.5' width='12' height='8' rx='2' fill='#1098ad'/><path d='M3.5 10.5 H6.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><path d='M8.5 10.5 H10.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectSharedAttribute::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectSharedAttribute::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_sharedAttribute_svg);

	return picture;
}

ibPicture ibValueMetaObjectSharedAttributeColumn::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectSharedAttributeColumn::GetIconGroup()
{
	// The declaration's — same thing, seen from inside an object.
	return ibValueMetaObjectSharedAttribute::GetIconGroup();
}
