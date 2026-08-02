#include "tableBox.h"

/* XPM */
static const char* s_dataviewlist_ctrl_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 5 1",
	"o c #FFFFFF",
	"  c #3E9ADE",
	"O c #B8B8B8",
	". c #858585",
	"X c #2C2C2C",
	/* pixels */
	"                ",
	" ..X.X...X..X.X ",
	" .X.X..XX..X.X. ",
	" .ooooOoooOoooo ",
	" .OOOOOOOOOOOOO ",
	" .ooooOoooOoooo ",
	" .ooooOoooOoooo ",
	" .OOOOOOOOOOOOO ",
	" .ooooOoooOoooo ",
	" .ooooOoooOoooo ",
	" .OOOOOOOOOOOOO ",
	" .ooooOoooOoooo ",
	" .ooooOoooOoooo ",
	" .OOOOOOOOOOOOO ",
	" .ooooOoooOoooo ",
	"                "
};

wxIcon ibValueModelTableBox::GetIcon() const
{
	return wxIcon(s_dataviewlist_ctrl_xpm);
}

wxIcon ibValueModelTableBox::GetIconGroup()
{
	return wxIcon(s_dataviewlist_ctrl_xpm);
}