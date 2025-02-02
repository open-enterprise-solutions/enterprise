#include "metaEnumObject.h"

/* XPM */
static const char *s_enumeration_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 5 1",
	"X c #808080",
	"o c #FF00FF",
	". c #C0C0C0",
	"  c None",
	"O c #800080",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	" ...........X   ",
	" .ooooooooooO   ",
	" .ooooooooooO   ",
	" .ooooooooooO   ",
	" XOOOOOOOOOOO   ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                "
};

wxIcon CMetaObjectEnum::GetIcon() const
{
	return wxIcon(s_enumeration_xpm);
}

wxIcon CMetaObjectEnum::GetIconGroup()
{
	return wxIcon(s_enumeration_xpm);
}