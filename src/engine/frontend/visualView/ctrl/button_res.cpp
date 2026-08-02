#include "widgets.h"

/* XPM */
static const char* s_button_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 36 1",
	"  c None",
	"8 c #D0EDFF",
	"4 c #2D2D2D",
	", c #E6F4FF",
	"= c #E4F4FF",
	"- c #262626",
	"6 c #D1EEFF",
	"e c #CAEDFF",
	"# c #272727",
	"1 c #D9F0FF",
	"w c #C8EAFF",
	"O c #F4FBFF",
	"5 c #DEF1FF",
	"u c #CDEBFF",
	"9 c #D7F0FF",
	"$ c #EDF7FF",
	"2 c #DCF1FF",
	". c #CFD9EC",
	"t c #CBEBFF",
	"+ c #282828",
	"r c #C9EBFF",
	"o c #FAFDFF",
	"3 c #292929",
	": c #E5F4FF",
	"; c #E3F4FF",
	"7 c #D2EEFF",
	"> c #2A2A2A",
	"< c #DAF0FF",
	"q c #C9EAFF",
	"% c #2B2B2B",
	"0 c #C7EAFF",
	"* c #EEF7FF",
	"& c #ECF7FF",
	"@ c #2C2C2C",
	"y c #CDECFF",
	"X c #003399",
	/* pixels */
	"                ",
	"                ",
	"                ",
	" .XXXXXXXXXXXX. ",
	" XooooooooooooX ",
	" XOO+@OO#OOOOOX ",
	" X$%&&@*#*$#*&X ",
	" X=-=;@:>$%,;;X ",
	" X<%1<+2345111X ",
	" X6378#9>9%888X ",
	" X00@%qw#er%q0X ",
	" XttyytttttuttX ",
	" .XXXXXXXXXXXX. ",
	"                ",
	"                ",
	"                "
};

wxIcon ibValueButton::GetIcon() const
{
	return wxIcon(s_button_xpm);
}

wxIcon ibValueButton::GetIconGroup()
{
	return wxIcon(s_button_xpm);
}