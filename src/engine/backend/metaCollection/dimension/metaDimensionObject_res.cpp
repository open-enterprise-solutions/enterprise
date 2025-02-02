#include "metaDimensionObject.h"

/* XPM */
static const char* s_dimensionGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"@ c Black",
	"X c #FFFFFF",
	"o c #808080",
	"O c #000080",
	"+ c #C0C0C0",
	". c #808000",
	"  c None",
	/* pixels */
	"                ",
	"  .........     ",
	"  .XXXXXXX.o    ",
	"  .XOX+X+X.+o   ",
	"  .XOOX+X+.@@@  ",
	"  .XOX+X+XXX+@  ",
	"  .XOOX+X+XX+@  ",
	"  .XOX+X+X+X+@  ",
	"  .XOOX+X+XX+@  ",
	"  .XOX+X+X+X+@  ",
	"  .XO+XOXOXX+@  ",
	"  .XOOOOOOOO+@  ",
	"  .XXXXXXXXX+@  ",
	"  .++++++++++@  ",
	"  @@@@@@@@@@@@  ",
	"                "
};

/* XPM */
static const char* s_dimension_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"O c Black",
	"o c #808080",
	"+ c #FFFF00",
	"X c #00FFFF",
	". c #C0C0C0",
	"@ c #808000",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"  .X.X.X.X.X    ",
	"  X.X.X.X.X.oO  ",
	"  .++++++++@oO  ",
	"  X.X.X.X.X.oO  ",
	"  .X.X.X.X.XoO  ",
	"  oooooooooooO  ",
	"   OOOOOOOOOOO  ",
	"                ",
	"                ",
	"                ",
	"                "
};


wxIcon CMetaObjectDimension::GetIcon() const
{
	return wxIcon(s_dimension_xpm);
}

wxIcon CMetaObjectDimension::GetIconGroup()
{
	return wxIcon(s_dimensionGroup_xpm);
}