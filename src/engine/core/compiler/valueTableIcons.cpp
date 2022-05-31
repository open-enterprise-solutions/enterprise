#include "valueTable.h"

/* XPM */
static const char* s_table_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	". c Black",
	"o c #FFFFFF",
	"O c #808080",
	"+ c #00FFFF",
	"X c #0000FF",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"                ",
	"..............  ",
	".XXXoXXXXoXXX.  ",
	".OOOOOOOOOOOO.  ",
	".oooOoo+oOooo.  ",
	".OOOOOOOOOOOO.  ",
	".oooOoo+oOooo.  ",
	".OOOOOOOOOOOO.  ",
	".oooOoo+oOooo.  ",
	"..............  ",
	"                ",
	"                ",
	"                "
};

/* XPM */
static const char* s_tableGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 5 1",
	". c Black",
	"o c #FFFFFF",
	"O c #0000FF",
	"X c #C0C0C0",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"                ",
	"..............  ",
	".XXXXXXXXXXXX.  ",
	".X....X.....X.  ",
	".X.oooX.OOOOX.  ",
	".XXXXXX.ooooX.  ",
	".X....X.ooooX.  ",
	".X.oooX.ooooX.  ",
	".XXXXXXXXXXXX.  ",
	"..............  ",
	"                ",
	"                ",
	"                "
};

wxIcon CValueTable::GetIcon()
{
	return wxIcon(s_table_xpm);
}

wxIcon CValueTable::GetIconGroup()
{
	return wxIcon(s_tableGroup_xpm);
}