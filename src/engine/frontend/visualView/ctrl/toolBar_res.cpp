#include "toolBar.h"

/* XPM */
static const char* s_toolbar_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 36 1",
	"  c None",
	"t c #E1DECE",
	"< c #DFF3FF",
	"q c #FFDA00",
	"* c #FFFFFF",
	"$ c #FFFD00",
	"; c #E2F4FF",
	"> c #0A87DA",
	", c #2F9AE3",
	"1 c #D6EFFF",
	"+ c #CC9900",
	"r c #48A5E9",
	"# c #F4FBFF",
	": c #EAE7D6",
	". c #ACA899",
	"y c #E3E0D0",
	"3 c #FFE200",
	"- c #EBF7FF",
	"9 c #E6E4D3",
	"O c #EBE8D7",
	"u c #E1DDCE",
	"4 c #168EDD",
	"6 c #42A2E8",
	"X c #ECE9D8",
	"5 c #2394E1",
	"w c #E5E2D2",
	"0 c #FFE700",
	"7 c #C9EAFF",
	"e c #C8C8C8",
	"& c #0083D7",
	"o c #666699",
	"= c #1E92DF",
	"% c #ECE8D7",
	"2 c #FFF200",
	"@ c #003399",
	"8 c #C1E7FF",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"................",
	"XXXXXXXXXXXXXXXX",
	"ooXXOO++XXXX@@@X",
	"o#oOX+$$+%O@&*=@",
	"o-;o:+$++O:@>*,@",
	"o<1o:++23+:@456@",
	"o78o9++0q+w@ere@",
	"ooooty+++yyt@@@y",
	"uuuttuutuuutuuuu",
	"................",
	"                ",
	"                ",
	"                "
};

wxIcon ibValueToolbar::GetIcon() const
{
	return wxIcon(s_toolbar_xpm);
}

wxIcon ibValueToolbar::GetIconGroup()
{
	return wxIcon(s_toolbar_xpm);
}