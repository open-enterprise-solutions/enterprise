#include "constant.h"

/* XPM */
static const char *s_constantGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 10 1",
	"# c #FFFFFF",
	"+ c #000080",
	". c #FFFF00",
	"$ c #00FFFF",
	"% c #040404",
	"@ c #0000FF",
	"O c #C0C0C0",
	"X c #808000",
	"  c None",
	"o c #868686",
	/* pixels */
	"                ",
	"                ",
	"   .XoX         ",
	"  .XoXOXoXoXoXo ",
	" ++++OoXoXOXoXo ",
	" X@@@oooooooooX ",
	" XO########OoXo ",
	" XO$o$o$o$#OooX ",
	" XO########OoXo ",
	" XO+++++###Ooo%%",
	"oXXXXXXXXXXXo%%%",
	"%%%%%%%%%%%%%%%%",
	"%OOOoOOOoOOO%O%%",
	"%OOO%%%%%OOO%%% ",
	"%OOOoOOOoOOO%%  ",
	"%%%%%%%%%%%%%   "
};

/* XPM */
static const char *s_constant_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"@ c Black",
	"O c #FFFFFF",
	"+ c #808080",
	". c #FFFF00",
	"X c #C0C0C0",
	"o c #808000",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"  .X.X.X.X.X    ",
	"  X.X.X.X.X.o   ",
	"  .OOOOOOOOX+@  ",
	"  X.X.X.X.X.o@  ",
	"  .X.X.X.X.X+@  ",
	"  o+o+o+o+o+o@  ",
	"   @@@@@@@@@@@  ",
	"                ",
	"                ",
	"                ",
	"                "
};

wxIcon CMetaConstantObject::GetIcon()
{
	return wxIcon(s_constant_xpm);
}

wxIcon CMetaConstantObject::GetIconGroup()
{
	return wxIcon(s_constantGroup_xpm);
}