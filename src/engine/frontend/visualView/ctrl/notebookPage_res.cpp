#include "notebook.h"

/* XPM */
static const char* s_page_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 13 1",
	"% c None",
	". c #FFFFFF",
	"$ c #ECFFFF",
	"o c #808080",
	"+ c #F9FFFF",
	"= c #848284",
	"@ c #F5FFFF",
	"# c #EBEBEB",
	"& c #EDFFFF",
	"O c #0066CC",
	"* c #E3FFFF",
	"X c #DDE2EC",
	"  c #333366",
	/* pixels */
	"                ",
	" .... XXXXXXXXX ",
	" .... Xoooooooo ",
	" .  . Xo....... ",
	" .... Xoooooooo ",
	" .... XXXXXXXXX ",
	" .... Xoooooooo ",
	" OOOO Xo....... ",
	" OOOO Xoooooooo ",
	" ++++ XXXXXXXXX ",
	" @@@@ XX######X ",
	" @  @ X#$%$$$%# ",
	" $$$& X#%$%$%$# ",
	" $$$$ X######## ",
	" **** XX======X ",
	"                "
};

wxIcon ibValueNotebookPage::GetIcon() const
{
	return wxIcon(s_page_xpm);
}

wxIcon ibValueNotebookPage::GetIconGroup()
{
	return wxIcon(s_page_xpm);
}