#include "toolBar.h"

/* XPM */
static const char* s_tool_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 52 1",
	"  c None",
	"2 c #FFE300",
	"3 c #A3A3FC",
	"v c #D1D1F7",
	"9 c #F7F7FF",
	"l c #6984C9",
	"p c #939EDD",
	"e c #F0F0FF",
	"a c #9D9DE6",
	"O c #CC9900",
	"h c #9B9AE0",
	"* c #FFEE00",
	"q c #A0A1F2",
	"f c #E2E2FE",
	"- c #A5A5FF",
	". c #ACA899",
	"7 c #E3E0CF",
	"b c #B3B5E5",
	"; c #E6E4D3",
	"# c #EBE8D7",
	"o c #E1DDCE",
	"g c #8995D4",
	"d c #E3E3FF",
	"j c #A2A9DF",
	"@ c #FFFF00",
	"i c #EBEBFF",
	"& c #FFF600",
	"w c #F3F3FF",
	"X c #ECE9D8",
	"t c #9EA9E6",
	"= c #FFED00",
	"1 c #E5E2D2",
	"$ c #EAE6D6",
	"k c #778ACD",
	"6 c #E3DFCF",
	"4 c #FCFCFF",
	"u c #EDEDFF",
	", c #D4D3EF",
	"% c #E9E7D6",
	"0 c #B6B8F0",
	"s c #E6E6FF",
	"5 c #C8C8F7",
	"+ c #ECE8D7",
	"8 c #A2A2F7",
	"x c #DFDFFE",
	"< c #B4B5FA",
	": c #E5E1D1",
	"c c #9494CD",
	"> c #FFE900",
	"z c #9998DA",
	"y c #9E9FEC",
	"r c #EFEFFF",
	/* pixels */
	"                ",
	"                ",
	"................",
	".XooooooooooXXX.",
	".XXXOOXXXXXXoXX.",
	".X+O@@O##+##o$$.",
	".$%O@OOOOO%#o%$.",
	".%%OO&*=--O%o%;.",
	".:;OO**>-,<1o%:.",
	".11OO=>23451o11.",
	".677OOOO8990ooo.",
	".oooooooqwertoo.",
	"........yruiip..",
	"        aiisdfg ",
	"        hssdjkl ",
	"        zxcvb   "
};

wxIcon ibValueToolBarItem::GetIcon() const
{
	return wxIcon(s_tool_xpm);
}

wxIcon ibValueToolBarItem::GetIconGroup()
{
	return wxIcon(s_tool_xpm);
}

/* XPM */
static const char* s_toolSeparator_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 32 1",
	"  c None",
	"q c #E1DECE",
	", c #DFF3FF",
	"O c #FFFFFF",
	"- c #E2F4FF",
	"; c #0A87DA",
	": c #2F9AE3",
	"< c #D6EFFF",
	"0 c #48A5E9",
	"# c #F4FBFF",
	". c #ACA899",
	"w c #E3E0D0",
	"5 c #CBEBFF",
	"> c #E6F6FF",
	"= c #EBF7FF",
	"$ c #EBE8D7",
	"e c #E1DDCE",
	"@ c #FAFDFF",
	"1 c #168EDD",
	"3 c #42A2E8",
	"X c #ECE9D8",
	"2 c #2394E1",
	"* c #F2FAFF",
	"8 c #C9EAFF",
	"6 c #C8C8C8",
	"% c #0083D7",
	"o c #666699",
	"4 c #DBF1FF",
	"& c #1E92DF",
	"7 c #D2EDFF",
	"+ c #003399",
	"9 c #C1E7FF",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"................",
	".XXXXXXXXXXXXXX.",
	".XoooXXOXX+++XX.",
	".Xo@#o$OX+%O&+X.",
	".Xo*=-oOX+;O:+X.",
	".Xo>,<oOX+123+X.",
	".Xo4<5oOX+666+X.",
	".Xo789oOX+606+X.",
	".XoooooOXq+++wX.",
	".qeeqqeOeeeqeqe.",
	"................",
	"                ",
	"                "
};

wxIcon ibValueToolBarSeparator::GetIcon() const
{
	return wxIcon(s_toolSeparator_xpm);
}

wxIcon ibValueToolBarSeparator::GetIconGroup()
{
	return wxIcon(s_toolSeparator_xpm);
}