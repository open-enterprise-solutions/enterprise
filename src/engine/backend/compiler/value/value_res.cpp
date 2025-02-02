#include "value.h"

/* XPM */
static const char* s_value_xpm[] = {
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

wxIcon CValue::GetIcon() const
{
	return wxIcon(s_value_xpm);
}

wxIcon CValue::GetIconGroup()
{
	return wxIcon(s_value_xpm);
}