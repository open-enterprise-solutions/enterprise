#include "form.h"

/* XPM */
static const char* s_form_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 54 1",
	", c #6C94C9",
	"  c None",
	"d c #CACACA",
	"2 c #678DC3",
	"e c #F7F7F7",
	"x c #587BB1",
	"= c #79A3DA",
	"j c #C3C3C3",
	"6 c #FFFFFF",
	"y c #5678AD",
	"m c #5171A9",
	"w c #F8F8F8",
	"5 c #6387BD",
	"b c #5475AB",
	"u c #5475AC",
	"* c #7AA4DB",
	"4 c #D3D3D3",
	"% c #668BC0",
	"c c #5779AE",
	"1 c #698FC4",
	"$ c #6E90C0",
	"+ c #A0C1EA",
	"@ c #C5C8CD",
	"h c #C5C5C5",
	"3 c #D4D4D4",
	"t c #F2F2F2",
	"n c #5373A9",
	"i c #5373AA",
	"p c #AFAFAF",
	"- c #79A2D8",
	"o c #92B9E8",
	"7 c #6085BA",
	"q c #FAFAFA",
	"g c #C6C6C6",
	"a c #CECECE",
	"< c #6B91C7",
	"0 c #FBFBFB",
	". c #A7C4E8",
	"# c #FF6444",
	"s c #D6D6D6",
	"> c #6E95CB",
	"z c #5A7CB2",
	"& c #7BA7DC",
	"9 c #FCFCFC",
	"f c #C8C8C8",
	"r c #F5F5F5",
	"l c #5172A9",
	"v c #5676AD",
	"; c #729ACF",
	"8 c #FDFDFD",
	"X c #5E81B7",
	"O c #CEDCF1",
	": c #7097CD",
	"k c #C2C2C2",
	/* pixels */
	"                ",
	".XXXXXXXXXXXXXX.",
	"XoOO+oo@@o@oo#.$",
	"XoOOooo@@o@oo#o%",
	"X&&*=-;::>,<<12%",
	"X333333333344445",
	"X666666666666667",
	"X68888900qweer6X",
	"X68888900qweer6X",
	"X6qqqwtttttttt6y",
	"X6eeertttttttt6u",
	"X66666666666666i",
	"X66666666666666i",
	"Xpaas3dffghjjkpl",
	".XXXXXzxxcvbbnm.",
	"                "
};

wxIcon ibValueForm::GetIcon() const
{
	return wxIcon(s_form_xpm);
}

wxIcon ibValueForm::GetIconGroup()
{
	return wxIcon(s_form_xpm);
}