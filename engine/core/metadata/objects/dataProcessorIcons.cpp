#include "dataProcessor.h"

/* XPM */
static const char *s_dataProcessorGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 8 1",
	"X c #FFFFFF",
	"# c #000080",
	"@ c #040404",
	"+ c #800000",
	"O c #C0C0C0",
	". c #808000",
	"  c None",
	"o c #868686",
	/* pixels */
	"                ",
	" .........      ",
	" .XXXXXXX.o     ",
	" .X.........    ",
	" .X.XXXXXXX.o   ",
	" .X.XXXXXXX.Oo  ",
	" .X.X++++++.oo@ ",
	" .X.X+++++++XO@ ",
	" .X.X+++++++XO@ ",
	" .X.XXXXXXXXXO@ ",
	" .X.X#######XO@ ",
	" .X.X#######XO@ ",
	" .O.X#######XO@ ",
	" .@.XXXXXXXXXO@ ",
	"   .OOOOOOOOOO@ ",
	"   @@@@@@@@@@@@ "
};

/* XPM */
static const char *s_dataProcessor_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 8 1",
	"X c #FFFFFF",
	"# c #000080",
	"+ c #040404",
	"@ c #800000",
	"O c #C0C0C0",
	". c #808000",
	"  c None",
	"o c #868686",
	/* pixels */
	"                ",
	" .........      ",
	" .XXXXXXX.o     ",
	" .XXXXXXX.Oo    ",
	" .XXXXXXX.+++   ",
	" .X@@@@@@@XO+   ",
	" .X@@@@@@@XO+   ",
	" .X@@@@@@@XO+   ",
	" .XXXXXXXXXO+   ",
	" .X#######XO+   ",
	" .X#######XO+   ",
	" .X#######XO+   ",
	" .XXXXXXXXXO+   ",
	" .OOOOOOOOOO+   ",
	" ++++++++++++   ",
	"                "
};

wxIcon CMetaObjectDataProcessorValue::GetIcon()
{
	return wxIcon(s_dataProcessor_xpm);
}

wxIcon CMetaObjectDataProcessorValue::GetIconGroup()
{
	return wxIcon(s_dataProcessorGroup_xpm);
}
