#include "dataReport.h"

/* XPM */
static const char *s_reportGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	"X c #FFFFFF",
	"+ c #040404",
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
	" .X.Xo++++X.+++ ",
	" .X.XO+oX+XXXO+ ",
	" .X.XXX+XXXXXO+ ",
	" .X.XXXO+XXXXO+ ",
	" .X.XXX+XXXXXO+ ",
	" .X.XO+oX+XXXO+ ",
	" .O.X+++++XXXO+ ",
	" .+.XXXXXXXXXO+ ",
	"   .OOOOOOOOOO+ ",
	"   ++++++++++++ "
};

/* XPM */
static const char *s_report_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"X c #FFFFFF",
	"@ c #008000",
	"+ c #040404",
	"O c #C0C0C0",
	". c #808000",
	"  c None",
	"o c #868686",
	/* pixels */
	"                ",
	" .........      ",
	" .XXXXXXX.o     ",
	" .XXXXXXX.Oo    ",
	" .Xo++++X.+++   ",
	" .XO+oX+XXXO+   ",
	" .XXX+XXXXXO+   ",
	" .XXXO+XXXXO+   ",
	" .XXX+XXXXXO+   ",
	" .XO+oX+XXXO+   ",
	" .X+++++X@XO+   ",
	" .XXXXXX@@@O+   ",
	" .XXXXXXX@XO+   ",
	" .OOOOOOOOOO+   ",
	" ++++++++++++   ",
	"                "
};

wxIcon CMetaObjectReport::GetIcon()
{
	return wxIcon(s_report_xpm);
}

wxIcon CMetaObjectReport::GetIconGroup()
{
	return wxIcon(s_reportGroup_xpm);
}
