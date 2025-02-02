#include "enumeration.h"

/* XPM */
static const char *s_enum_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"X c #FFFFFF",
	"@ c #040404",
	"+ c #800000",
	"O c #C0C0C0",
	". c #808000",
	"  c None",
	"o c #868686",
	/* pixels */
	"                ",
	"  .........     ",
	"  .XXXXXXX.o    ",
	"  .XXXOXOX.Oo   ",
	"  .XO+XOX+@@@@  ",
	"  .X+XOXOX+XO@  ",
	"  .X+OXOXO+XO@  ",
	"  .X+XOXOX+XO@  ",
	"  .+XOXOXOX+O@  ",
	"  .X+XOXOX+XO@  ",
	"  .X+O+O+O+XO@  ",
	"  .X+XOXOX+XO@  ",
	"  .XX+XXX+XXO@  ",
	"  .OOOOOOOOOO@  ",
	"  @@@@@@@@@@@@  ",
	"                "
};

/* XPM */
static const char *s_enumGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 8 1",
	"X c #FFFFFF",
	"+ c #000080",
	"# c #040404",
	"@ c #800000",
	"O c #C0C0C0",
	"o c #808000",
	". c None",
	"  c #868686",
	/* pixels */
	"         .......",
	" XXXXXXX  ......",
	" XXooooooooo....",
	" XOoXXXXXXXoo...",
	" X+oXXXOXOXoOo..",
	" X+oXO@XOX@####.",
	" X+oX@XOXOX@XO#.",
	" +XoX@OXOXO@XO#.",
	" X+oX@XOXOX@XO#.",
	" X+o@XOXOXOX@O#.",
	" X+oX@XOXOX@XO#.",
	" XXoX@OXOXO@XO#.",
	" OOoX@XOXOX@XO#.",
	"###oXX@XXX@XXO#.",
	"...oOOOOOOOOOO#.",
	"...############."
};

wxIcon CMetaObjectEnumeration::GetIcon() const
{
	return wxIcon(s_enum_xpm);
}

wxIcon CMetaObjectEnumeration::GetIconGroup()
{
	return wxIcon(s_enumGroup_xpm);
}
