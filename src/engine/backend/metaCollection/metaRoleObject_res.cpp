#include "metaRoleObject.h"

/* XPM */
static const char* s_roleGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	"  c None",
	"+ c #FFFFFF",
	"X c #808080",
	"o c #FFFF00",
	". c #C0C0C0",
	"O c #808000",
	/* pixels */
	"                ",
	"    . ..        ",
	" .  X X oO      ",
	" .  o  X o      ",
	" .X ooo  oO     ",
	" .X oo  XoO     ",
	"  .X oooooO     ",
	"   X. XXXooO    ",
	"   .. +.  ooO   ",
	"    . ..  oooO  ",
	"    . ..    ooO ",
	"    . ..    ooo ",
	"    . ..     oo ",
	"   .. ..        ",
	"      ..        ",
	"                "
};

/* XPM */
static const char* s_role_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 5 1",
	"  c None",
	"O c #FFFFFF",
	"o c #000080",
	". c #FFFF00",
	"X c #808000",
	/* pixels */
	"                ",
	"    .X.X.X      ",
	"   X..oO X.     ",
	"  ..X.  X.X.    ",
	"  XX.X.X.X.X    ",
	"   .O.X.X.X     ",
	"    .X.X.X      ",
	"      O X       ",
	"      O X       ",
	"      O         ",
	"      O .       ",
	"      O .       ",
	"      O         ",
	"      O .       ",
	"                ",
	"                "
};

wxIcon CMetaObjectRole::GetIcon() const
{
	return wxIcon(s_role_xpm);
}

wxIcon CMetaObjectRole::GetIconGroup()
{
	return wxIcon(s_roleGroup_xpm);
}
