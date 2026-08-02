#include "widgets.h"

/* XPM */
static const char* s_text_ctrl_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 12 1",
	"  c None",
	"& c #EEFFFF",
	"$ c #171717",
	"X c #FFFFFF",
	"o c #808080",
	"* c #EAFFFF",
	". c #3E9ADE",
	"+ c #F9FFFF",
	"# c #F3FFFF",
	"% c #EDFFFF",
	"O c #151515",
	"@ c #161616",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"................",
	".XXXXXXXXXXXXXX.",
	".XXXXXXoXoXXXXX.",
	".XXOOXXXoXXX++X.",
	".XXXX@X+o++X++#.",
	".++O@@++o++####.",
	".#O+#$+#o######.",
	".##$$O#o#o%#%%%.",
	".##&%%%%%%%%***.",
	"................",
	"                ",
	"                ",
	"                "
};

wxIcon ibValueTextCtrl::GetIcon() const
{
	return wxIcon(s_text_ctrl_xpm);
}

wxIcon ibValueTextCtrl::GetIconGroup()
{
	return wxIcon(s_text_ctrl_xpm);
}