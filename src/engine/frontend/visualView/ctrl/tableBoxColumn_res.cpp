#include "tableBox.h"

/* XPM */
static const char* s_dataviewlist_column_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"17 17 7 1",
	". c None",
	"+ c #FFFFFF",
	"O c #FF3300",
	"  c #3E9ADE",
	"@ c #B8B8B8",
	"X c #858585",
	"o c #2C2C2C",
	/* pixels */
	"              ...",
	" XXoXXoOOOOOOO...",
	" XoXooXOOOOOOO...",
	" X+++++O+++++O...",
	" X+++++O+++++O...",
	" X+++++O+++++O...",
	" X@@@@@OOOOOOO...",
	" X+++++O+++++O...",
	" X+++++O+++++O...",
	" X+++++O+++++O...",
	" X@@@@@OOOOOOO...",
	" X+++++O+++++O...",
	" X+++++O+++++O...",
	" X+++++O+++++O...",
	" X@@@@@OOOOOOO...",
	" X+++++O+++++O...",
	" X+++++O+++++O..."
};

wxIcon ibValueModelTableBoxColumn::GetIcon() const
{
	return wxIcon(s_dataviewlist_column_xpm);
}

wxIcon ibValueModelTableBoxColumn::GetIconGroup()
{
	return wxIcon(s_dataviewlist_column_xpm);
}