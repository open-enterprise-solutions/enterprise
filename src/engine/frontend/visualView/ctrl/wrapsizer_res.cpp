#include "sizer.h"

/* XPM */
static char* s_wrapSizer_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 3 1",
	"  c None",
	". c #0000FF",
	"X c #9B9BFF",
	/* pixels */
	"                ",
	"  ............  ",
	" .            . ",
	" .XXX XXX XXX . ",
	" .XXX XXX XXX . ",
	" .XXX XXX XXX . ",
	" .            . ",
	" .XXX XXX XXX . ",
	" .XXX XXX XXX . ",
	" .XXX XXX XXX . ",
	" .            . ",
	" .XXX         . ",
	" .XXX         . ",
	" .XXX         . ",
	"  ............  ",
	"                "
};

wxIcon ibValueWrapSizer::GetIcon() const
{
	return wxIcon(s_wrapSizer_xpm);
}

wxIcon ibValueWrapSizer::GetIconGroup()
{
	return wxIcon(s_wrapSizer_xpm);
}
