#include "widgets.h"

/* XPM */
static const char* s_slider_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 10 1",
	"  c None",
	"@ c #E2DFCF",
	"+ c #D4D1C3",
	"o c #ECE9D8",
	"X c #66CC33",
	"# c #BBB9AD",
	"O c #E3DFD0",
	". c #716F64",
	"$ c #E2E0CF",
	"% c #E2E0D0",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"    ...         ",
	"   .XXX.        ",
	"   .oO+.        ",
	"   .o@+.        ",
	" ##.o@+.####### ",
	"#oo.o@+.ooooooo#",
	" ##.o$+.####### ",
	"   .o@+.        ",
	"   .o%+.        ",
	"   .XXX.        ",
	"    ...         ",
	"                ",
	"                "
};

wxIcon ibValueSlider::GetIcon() const
{
	return wxIcon(s_slider_xpm);
}

wxIcon ibValueSlider::GetIconGroup()
{
	return wxIcon(s_slider_xpm);
}