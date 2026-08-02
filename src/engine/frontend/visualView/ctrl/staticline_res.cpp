#include "widgets.h"

/* XPM */
static const char* s_static_line_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 3 1",
	"  c None",
	"X c #FFFFFF",
	". c #ACA899",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	" .............  ",
	"  XXXXXXXXXXXXX ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                "
};

wxIcon ibValueStaticLine::GetIcon() const
{
	return wxIcon(s_static_line_xpm);
}

wxIcon ibValueStaticLine::GetIconGroup()
{
	return wxIcon(s_static_line_xpm);
}