#include "widgets.h"

/* XPM */
static char* s_checkbox_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"22 22 24 1",
	"  c #333366",
	". c #009900",
	"X c magenta",
	"o c #D5D5D5",
	"O c gray84",
	"+ c #D8D8D8",
	"@ c #DADADA",
	"# c gainsboro",
	"$ c #DFDFDF",
	"% c #E1E1E1",
	"& c gray89",
	"* c #E6E6E5",
	"= c #E6E6E6",
	"- c gray91",
	"; c gray92",
	": c #EEEEEE",
	"> c gray94",
	", c gray95",
	"< c gray96",
	"1 c gray97",
	"2 c #F9F9F9",
	"3 c #FBFBFB",
	"4 c gray99",
	"5 c None",
	/* pixels */
	"5555555555555555555555",
	"5555555555555555555555",
	"5555555555555555555555",
	"5555555555555555555555",
	"5555555555555555555555",
	"5555            555555",
	"5555 OOO@@$%*-- 555555",
	"5555 OOO#$&&*.- 555555",
	"5555 OO#$&&*..: 555555",
	"5555 O$%%&*...> 555555",
	"5555 %.%&*...>, 555555",
	"5555 %..*...>,< 555555",
	"5555 %.....>,<1 555555",
	"5555 &=...>,<12 555555",
	"5555 *-;.>><124 555555",
	"5555 -;:>,<1244 555555",
	"5555            555555",
	"5555555555555555555555",
	"5555555555555555555555",
	"5555555555555555555555",
	"5555555555555555555555",
	"5555555555555555555555"
};

wxIcon CValueCheckbox::GetIcon() const
{
	return wxIcon(s_checkbox_xpm);
}

wxIcon CValueCheckbox::GetIconGroup()
{
	return wxIcon(s_checkbox_xpm);
}