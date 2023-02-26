#include "metaResourceObject.h"

/* XPM */
static const char* s_resourceGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 8 1",
	"+ c Black",
	"X c #FFFFFF",
	"o c #808080",
	"# c #000080",
	"@ c #800000",
	"O c #C0C0C0",
	". c #808000",
	"  c None",
	/* pixels */
	"                ",
	"  .........     ",
	"  .XXXXXXX.o    ",
	"  .XOXOXOX.Oo   ",
	"  .XXOXOXO.+++  ",
	"  .XOXOX@@@XO+  ",
	"  .XXOXO@@@XO+  ",
	"  .XOXOX@@@XO+  ",
	"  .X###O@@@XO+  ",
	"  .X###X@@@XO+  ",
	"  .X###O@@@XO+  ",
	"  .X###X@@@XO+  ",
	"  .XXXXXXXXXO+  ",
	"  .OOOOOOOOOO+  ",
	"  ++++++++++++  ",
	"                "
};

/* XPM */
static const char* s_resource_xpm[] = {
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

wxIcon CMetaResourceObject::GetIcon()
{
	return wxIcon(s_resource_xpm);
}

wxIcon CMetaResourceObject::GetIconGroup()
{
	return wxIcon(s_resourceGroup_xpm);
}