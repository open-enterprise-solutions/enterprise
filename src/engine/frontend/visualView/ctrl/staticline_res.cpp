#include "widgets.h"

/* XPM */
static char* s_static_line_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"22 22 4 1",
	"  c magenta",
	". c #ACA899",
	"X c gray100",
	"o c None",
	/* pixels */
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"o...................oo",
	"ooXXXXXXXXXXXXXXXXXXXo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo",
	"oooooooooooooooooooooo"
};

wxIcon CValueStaticLine::GetIcon() const
{
	return wxIcon(s_static_line_xpm);
}

wxIcon CValueStaticLine::GetIconGroup()
{
	return wxIcon(s_static_line_xpm);
}