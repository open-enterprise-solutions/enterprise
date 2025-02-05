#include "widgets.h"

/* XPM */
static char* s_gauge_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"22 22 53 1",
	"  c #333366",
	". c #009900",
	"X c #12A50C",
	"o c #13A50C",
	"O c #13A50D",
	"+ c #12A60C",
	"@ c #13A60D",
	"# c #2DB71E",
	"$ c #2DB71F",
	"% c #2EB71E",
	"& c #2EB71F",
	"* c #2EB81F",
	"= c #4CCC33",
	"- c #4DCC33",
	"; c #6CE147",
	": c #6BE148",
	"> c #6CE048",
	", c #6CE148",
	"< c magenta",
	"1 c #DEFFFF",
	"2 c #DFFFFF",
	"3 c LightCyan",
	"4 c #E1FFFF",
	"5 c #E2FFFF",
	"6 c #E3FFFF",
	"7 c #E4FFFF",
	"8 c #E5FFFF",
	"9 c #E6FFFF",
	"0 c #E7FFFF",
	"q c #E8FFFF",
	"w c #E9FFFF",
	"e c #EAFFFF",
	"r c #EBFFFF",
	"t c #ECFFFF",
	"y c #EDFFFF",
	"u c #EEFFFF",
	"i c azure",
	"p c #F1FFFF",
	"a c #F2FFFF",
	"s c #F3FFFF",
	"d c #F4FFFF",
	"f c #F5FFFF",
	"g c #F6FFFF",
	"h c #F7FFFF",
	"j c #F8FFFF",
	"k c #F9FFFF",
	"l c #FAFFFF",
	"z c #FBFFFF",
	"x c #FCFFFF",
	"c c #FDFFFF",
	"v c #FEFFFF",
	"b c gray100",
	"n c None",
	/* pixels */
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"n                    n",
	" bbbbbbbbbbbbbbbbbkkb ",
	" b...b...b...k...kkgg ",
	" bXXXbXXXgXXXgXXXuugu ",
	" k%%*g%%%g#%*u&&%uuue ",
	" g---u---u=--e---e666 ",
	" u::;u:,:e::,6,;:6616 ",
	" ee6e6666666661611111 ",
	"n                    n",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn",
	"nnnnnnnnnnnnnnnnnnnnnn"
};

wxIcon CValueGauge::GetIcon() const
{
	return wxIcon(s_gauge_xpm);
}

wxIcon CValueGauge::GetIconGroup()
{
	return wxIcon(s_gauge_xpm);
}