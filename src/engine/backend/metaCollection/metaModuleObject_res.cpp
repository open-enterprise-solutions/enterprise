#include "metaModuleObject.h"

/* XPM */
static const char *s_moduleGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	"O c #333399",
	"o c #FFFFFF",
	"X c #4D4D4D",
	"+ c #000080",
	". c #3333FF",
	"  c None",
	/* pixels */
	"                ",
	" .............. ",
	" XooooooooooooX ",
	"OOOOOOOOOOooooX ",
	"XooooooooXooooX ",
	"XoOOOOOOOOOOooX ",
	"XoXooooooooXooX ",
	"XoXoOOOOOOOOOOX ",
	"XoXoXooooooooXX ",
	"XoXoXo++++++oXX ",
	"XoXoXooooooooXX ",
	"XXXoXo+++++ooXX ",
	"  XoXooooooooX  ",
	"  XXXo++++++oX  ",
	"    XooooooooX  ",
	"    XXXXXXXXXX  "
};

/* XPM */
static const char *s_module_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 5 1",
	"o c #FFFFFF",
	"O c #339999",
	". c #3333FF",
	"X c #A0A0A4",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	"...........X    ",
	".oooooooo.oOX   ",
	".o......o.ooOX  ",
	".oooooooo.ooo.  ",
	".o......o.....X ",
	".oooooooooooo.X ",
	".o......ooooo.X ",
	".oooooooooooo.X ",
	".o......ooooo.X ",
	".oooooooooooo.X ",
	".oooooooooooo.X ",
	"..............X ",
	"XXXXXXXXXXXXXXX ",
	"                "
};

wxIcon CMetaObjectModule::GetIcon() const
{
	return wxIcon(s_module_xpm);
}

wxIcon CMetaObjectModule::GetIconGroup()
{
	return wxIcon(s_moduleGroup_xpm);
}

wxIcon CMetaObjectCommonModule::GetIcon() const
{
	return wxIcon(s_module_xpm);
}

wxIcon CMetaObjectCommonModule::GetIconGroup()
{
	return wxIcon(s_moduleGroup_xpm);
}

wxIcon CMetaObjectManagerModule::GetIcon() const
{
	return wxIcon(s_module_xpm);
}

wxIcon CMetaObjectManagerModule::GetIconGroup()
{
	return wxIcon(s_moduleGroup_xpm);
}
