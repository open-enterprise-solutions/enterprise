#include "widgets.h"

/* XPM */
static char* s_text_ctrl_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"22 22 27 1",
	"  c black",
	". c #3E9ADE",
	"X c magenta",
	"o c #808080",
	"O c #EAFFFF",
	"+ c #EBFFFF",
	"@ c #ECFFFF",
	"# c #EDFFFF",
	"$ c #EEFFFF",
	"% c #EFFFFF",
	"& c azure",
	"* c #F1FFFF",
	"= c #F2FFFF",
	"- c #F3FFFF",
	"; c #F4FFFF",
	": c #F5FFFF",
	"> c #F6FFFF",
	", c #F7FFFF",
	"< c #F8FFFF",
	"1 c #F9FFFF",
	"2 c #FAFFFF",
	"3 c #FBFFFF",
	"4 c #FCFFFF",
	"5 c #FDFFFF",
	"6 c #FEFFFF",
	"7 c gray100",
	"8 c None",
	/* pixels */
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888",
	"......................",
	".77777777777777777777.",
	".777777o7o77777777777.",
	".77  777o777771171117.",
	".7777 71o11711111711-.",
	".11   11o11--11------.",
	".- 1- 1-o------------.",
	".--   -o-o#--#-######.",
	".--###########OOOOOOO.",
	"......................",
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888",
	"8888888888888888888888"
};

wxIcon CValueTextCtrl::GetIcon() const
{
	return wxIcon(s_text_ctrl_xpm);
}

wxIcon CValueTextCtrl::GetIconGroup()
{
	return wxIcon(s_text_ctrl_xpm);
}