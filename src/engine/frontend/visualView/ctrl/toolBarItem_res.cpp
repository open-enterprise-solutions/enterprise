#include "toolBar.h"

/* XPM */
static char* s_tool_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"22 22 85 1",
	"  c #6984C9",
	". c #778ACD",
	"X c magenta",
	"o c #CC9900",
	"O c #FFE300",
	"+ c #FFE800",
	"@ c #FFE900",
	"# c #FFEC00",
	"$ c #FFED00",
	"% c #FFEE00",
	"& c #FFF200",
	"* c #FFF600",
	"= c #FFFD00",
	"- c yellow",
	"; c #ACA899",
	": c #8995D4",
	"> c #9494CD",
	", c #9695D0",
	"< c #9797D5",
	"1 c #939EDD",
	"2 c #9D9FD5",
	"3 c #9998DA",
	"4 c #9B9AE0",
	"5 c #9D9DE6",
	"6 c #9E9FEC",
	"7 c #9EA9E6",
	"8 c #A2A9DF",
	"9 c #A0A1F2",
	"0 c #A2A2F7",
	"q c #A3A3FC",
	"w c #A5A5FF",
	"e c #B3B5E5",
	"r c #B4B5FA",
	"t c #B6B8F0",
	"y c #C4C4F3",
	"u c #C8C8F7",
	"i c #CBCBF8",
	"p c #D4D3EF",
	"a c #D1D1F7",
	"s c #D8D8FF",
	"d c #DFDFFE",
	"f c #E1DDCE",
	"g c #E1DECD",
	"h c #E1DECE",
	"j c #E3DFCF",
	"k c #E3E0CF",
	"l c #E3E0D0",
	"z c #E5E1D1",
	"x c #E5E1D2",
	"c c #E5E2D1",
	"v c #E5E2D2",
	"b c #E6E3D3",
	"n c #E7E3D3",
	"m c #E6E4D3",
	"M c #E8E5D4",
	"N c #E8E5D5",
	"B c #E9E5D4",
	"V c #E9E5D5",
	"C c #E8E6D4",
	"Z c #E9E7D6",
	"A c #EAE6D6",
	"S c #EAE7D6",
	"D c #EBE8D7",
	"F c #ECE8D7",
	"G c #EBE8D8",
	"H c #ECE9D8",
	"J c #E2E2FE",
	"K c #E3E3FF",
	"L c #E4E4FF",
	"P c #E5E5FF",
	"I c #E6E6FF",
	"U c #E7E7FF",
	"Y c #E9E9FE",
	"T c #E9E9FF",
	"R c #EAEAFF",
	"E c #EBEBFF",
	"W c #EDEDFF",
	"Q c #EFEFFF",
	"! c #F0F0FF",
	"~ c #F1F1FF",
	"^ c #F3F3FF",
	"/ c #F6F6FF",
	"( c #F7F7FF",
	") c #FCFCFF",
	"_ c None",
	/* pixels */
	"______________________",
	"______________________",
	"______________________",
	"______________________",
	"______________________",
	";;;;;;;;;;;;;;;;;;;;;;",
	"HHHHHHffffffffffHHHHFH",
	"HHHHHfHHooHHHHHHfHHHHH",
	"HHAAFfFo--oDDFDDfAAFFF",
	"ZAAZZfZo-oooooZDfZAAZZ",
	"ZZnZZfZoo*%$wwoZfZmZZZ",
	"zzZvvfmoo%%@wprvfZzbvv",
	"zvvvvfvoo$@Oq)uvfvvvvv",
	"vjjjkfkkoooo0((tfffkkk",
	"ffffffffffff9^!Q7fffff",
	";;;;;;;;;;;;6QWEE1;;;;",
	"____________5EEIKJ:___",
	"____________4IIK8. ___",
	"____________3d>ae_____",
	"____________<>_2s2____",
	"____________>__2iy2___",
	"________________222___"
};

wxIcon CValueToolBarItem::GetIcon() const
{
	return wxIcon(s_tool_xpm);
}

wxIcon CValueToolBarItem::GetIconGroup()
{
	return wxIcon(s_tool_xpm);
}

/* XPM */
static char* s_toolSeparator_xpm[] = {
"22 22 34 1",
" 	c None",
"!	c black",
"#	c #ACA899",
"$	c #ECE9D8",
"%	c #666699",
"&	c white",
"'	c #003399",
"(	c #FAFDFF",
")	c #F4FBFF",
"*	c #EBE8D7",
"+	c #0083D7",
",	c #1E92DF",
"-	c #F2FAFF",
".	c #EBF7FF",
"0	c #E2F4FF",
"1	c #0A87DA",
"2	c #2F9AE3",
"3	c #E6F6FF",
"4	c #DFF3FF",
"5	c #D6EFFF",
"6	c #168EDD",
"7	c #2394E1",
"8	c #329BE5",
"9	c #42A2E8",
":	c #DBF1FF",
";	c #CBEBFF",
"<	c #C8C8C8",
"=	c #D2EDFF",
">	c #C9EAFF",
"?	c #C1E7FF",
"@	c #48A5E9",
"A	c #E1DECE",
"B	c #E3E0D0",
"C	c #E1DDCE",
"                      ",
"                      ",
"                      ",
"                      ",
"                      ",
"                      ",
"######################",
"$$$$$$$$$$$#$$$$$$$$$$",
"$$%%%%$$$$&#$$$''''$$$",
"$$%&()%*$$&#$$'+&&,'$$",
"$$%(-.0%$$&#$$'1&&2'$$",
"$$%-345%$$&#$$'6789'$$",
"$$%3:5;%$$&#$$'<<<<'$$",
"$$%:=>?%$$&#$$'<@<<'$$",
"$$%%%%%%$$&#$$A''''B$$",
"CACCCAACCA&CCCCCACCACC",
"######################",
"                      ",
"                      ",
"                      ",
"                      ",
"                      " };

wxIcon CValueToolBarSeparator::GetIcon() const
{
	return wxIcon(s_toolSeparator_xpm);
}

wxIcon CValueToolBarSeparator::GetIconGroup()
{
	return wxIcon(s_toolSeparator_xpm);
}