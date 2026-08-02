#include "sizer.h"

/* XPM */
static const char* s_boxSizer_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 3 1",
	"  c None",
	". c #0000FF",
	"X c #9B9BFF",
	/* pixels */
	"                ",
	"  ............  ",
	" .            . ",
	" .            . ",
	" .            . ",
	" .............. ",
	" .XXXXXXXXXXXX. ",
	" .            . ",
	" .            . ",
	" .XXXXXXXXXXXX. ",
	" .............. ",
	" .            . ",
	" .            . ",
	" .            . ",
	"  ............  ",
	"                "
};

wxIcon ibValueBoxSizer::GetIcon() const
{
	return wxIcon(s_boxSizer_xpm);
}

wxIcon ibValueBoxSizer::GetIconGroup()
{
	return wxIcon(s_boxSizer_xpm);
}