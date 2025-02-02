#include "metaFormObject.h"

/* XPM */
static const char *s_formGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	". c #333399",
	"X c #FFFFFF",
	"o c #4D4D4D",
	"+ c #A0A0A4",
	"@ c #FFFF33",
	"O c #D7D7D7",
	"  c None",
	/* pixels */
	"                ",
	"..............  ",
	".X............  ",
	".............o  ",
	"oOOOoXXXXXXXXo  ",
	"oOOOoXXXXXXXXo  ",
	"oOOOoXX+++XXXo  ",
	"oOOOoX+O@O+XXo  ",
	"oOOOo+O@O@+++++ ",
	"oOOOo+XXXXXXXX+o",
	"oOOOo+X@O@O@O@+o",
	"oOOOo+XO@O@O@O+o",
	"ooooo+X@O@O@O@+o",
	"     ++++++++++o",
	"      oooooooooo",
	"                "
};

/* XPM */
static const char *s_form_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	". c #333399",
	"X c #FFFFFF",
	"o c #4D4D4D",
	"+ c #A0A0A4",
	"O c #D7D7D7",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	"..............  ",
	".X............  ",
	".............o  ",
	"oOOOoXXXXXXXXo  ",
	"oOOOoX+++++oXo  ",
	"oOOOoX+XXXXoXo  ",
	"oOOOoXooooooXo  ",
	"oOOOoX+++++oXo  ",
	"oOOOoX+XXXXoXo  ",
	"oOOOoXooooooXo  ",
	"oOOOoXXXXXXXXo  ",
	"oooooooooooooo  ",
	"                ",
	"                "
};

wxIcon CMetaObjectForm::GetIcon() const
{
	return wxIcon(s_form_xpm);
}

wxIcon CMetaObjectForm::GetIconGroup()
{
	return wxIcon(s_formGroup_xpm);
}

wxIcon CMetaObjectCommonForm::GetIcon() const
{
	return wxIcon(s_form_xpm);
}

wxIcon CMetaObjectCommonForm::GetIconGroup()
{
	return wxIcon(s_formGroup_xpm);
}