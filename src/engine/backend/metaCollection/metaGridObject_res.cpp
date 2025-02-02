#include "metaGridObject.h"

/* XPM */
static const char *s_gridGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	"X c #FFFFFF",
	"+ c #999933",
	". c #4D4D4D",
	"o c #A0A0A4",
	"O c #FFFF33",
	"  c None",
	/* pixels */
	"                ",
	"     .........  ",
	"     .XXXXXXX.  ",
	"  ....XoooooX...",
	" .OXO.XoXXXoX.O.",
	" .X...XoooooX.X.",
	" .O.X.XoXXXoX.O.",
	" .X.X.XoooooX.X.",
	" .O.X.XXXXXXX.O.",
	"..............X.",
	".O+O+O+O+O+O+...",
	".+O+O+O+O+O+O+..",
	" .+O+O+O+O+O+O..",
	" .O+O+O+O+O+O+O.",
	"  ..............",
	"                "
};

/* XPM */
static const char *s_grid_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 5 1",
	"X c #FFFFFF",
	"o c #040404",
	"O c #C0C0C0",
	"  c None",
	". c #868686",
	/* pixels */
	"                ",
	"  ..........    ",
	"  .XXXXXXX.X.   ",
	"  .XoOXXXX.OX.  ",
	"  .XOOXXXX.XOX. ",
	"  .XXXXXXX..... ",
	"  .XoOXXXXXoXX. ",
	"  .XOOXXXXXOOX. ",
	"  .XXXXXXXXXXX. ",
	"  .XoOXXXXXoOX. ",
	"  .XOOXXXXXOOX. ",
	"  .XXXXXXXXXXX. ",
	"  .XoOXXXXXoOX. ",
	"  .XOOXXXXXOOX. ",
	"  .XXXXXXXXXXX. ",
	"  ............. "
};

wxIcon CMetaObjectGrid::GetIcon() const
{
	return wxIcon(s_grid_xpm);
}

wxIcon CMetaObjectGrid::GetIconGroup()
{
	return wxIcon(s_gridGroup_xpm);
}