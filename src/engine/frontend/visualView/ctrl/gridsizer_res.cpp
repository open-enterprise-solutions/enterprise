#include "sizer.h"

/* XPM */
static const char* s_gridSizer_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 4 1",
	"  c None",
	"o c #4949FF",
	". c #0000FF",
	"X c #9B9BFF",
	/* pixels */
	"                ",
	"  ............  ",
	" .     XX     . ",
	" .     XX     . ",
	" .     XX     . ",
	" .     XX     . ",
	" .     XX     . ",
	" .XXXXXooXXXXX. ",
	" .XXXXXooXXXXX. ",
	" .     XX     . ",
	" .     XX     . ",
	" .     XX     . ",
	" .     XX     . ",
	" .     XX     . ",
	"  ............  ",
	"                "
};

wxIcon ibValueGridSizer::GetIcon() const
{
	return wxIcon(s_gridSizer_xpm);
}

wxIcon ibValueGridSizer::GetIconGroup()
{
	return wxIcon(s_gridSizer_xpm);
}