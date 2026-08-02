#include "sizer.h"

/* XPM */
static const char* s_staticbox_sizer_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	"  c None",
	"O c #4949FF",
	"X c #B7B79B",
	"o c #0000FF",
	". c #003399",
	"+ c #9B9BFF",
	/* pixels */
	"     .....      ",
	"  XX ..... XXX  ",
	" X            X ",
	"X  oooooooooo  X",
	"X oO++++++++Oo X",
	"X o+        +o X",
	"X o+        +o X",
	"X oO++++++++Oo X",
	"X oooooooooooo X",
	"X oO++++++++Oo X",
	"X o+        +o X",
	"X o+        +o X",
	"X oO++++++++Oo X",
	"X  oooooooooo  X",
	" X            X ",
	"  XXXXXXXXXXXX  "
};

wxIcon ibValueStaticBoxSizer::GetIcon() const
{
	return wxIcon(s_staticbox_sizer_xpm);
}

wxIcon ibValueStaticBoxSizer::GetIconGroup()
{
	return wxIcon(s_staticbox_sizer_xpm);
}