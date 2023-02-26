#include "catalog.h"

/* XPM */
static const char *s_catalogGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 11 1",
	"$ c #FFFFFF",
	"+ c #000080",
	"@ c #FFFF00",
	". c #040404",
	"% c #800000",
	"& c #FF0000",
	"o c #0000FF",
	"O c #C0C0C0",
	"# c #808000",
	"  c None",
	"X c #868686",
	/* pixels */
	"                ",
	"                ",
	"    ........... ",
	"   XoOo+O+X+X+..",
	"  o++++++++...X.",
	" oXXXXXXXO..X.X.",
	" oX.......@#..X.",
	" o.@#@#@#@#@..X.",
	" ooooooooo+++.X.",
	"o$$O%%%%%OOO+.X.",
	"o$OO&&&&$OOO+.X.",
	"o$OOOOOOOOOO+.X.",
	"o$O$$$$$$OOO+.X.",
	"oOOO......OO+.X.",
	"o$OOOOOOOOOO+..$",
	"+++++++++++++$$$"
};

/* XPM */
static const char *s_catalog_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"X c Black",
	"+ c #FFFFFF",
	"O c #808080",
	". c #000080",
	"o c #0000FF",
	"@ c #C0C0C0",
	"  c None",
	/* pixels */
	"          ...   ",
	"      XXXoooo.  ",
	"     O++...++X  ",
	"    XXXoooo.+X  ",
	"   O++...++X+XXX",
	"  XXXoooo.+X@XXX",
	" O+++++++X+X@X X",
	" O+++++++X@XXo X",
	" O++X+X++X@Xoo X",
	" O@@X+X@@XXoo X ",
	" XXXOXOXXXXo X  ",
	" X+oXoXoooXoX   ",
	" X+o+oo+ooXX    ",
	" XXXXXXXXXX     ",
	"                ",
	"                "
};

wxIcon CMetaObjectCatalog::GetIcon()
{
	return wxIcon(s_catalog_xpm);
}

wxIcon CMetaObjectCatalog::GetIconGroup()
{
	return wxIcon(s_catalogGroup_xpm);
}