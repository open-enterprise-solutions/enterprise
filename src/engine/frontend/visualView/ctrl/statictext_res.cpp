#include "widgets.h"

/* XPM */
static const char* s_static_text_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 18 1",
	"  c None",
	"$ c #1E1E1E",
	"& c #262626",
	"X c #1F1F1F",
	". c #181818",
	"- c #272727",
	"@ c #202020",
	"O c #191919",
	"> c #212121",
	"% c #292929",
	", c #222222",
	"o c #1B1B1B",
	": c #232323",
	"# c #1C1C1C",
	"= c #242424",
	"* c #1D1D1D",
	"+ c #161616",
	"; c #252525",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"      .         ",
	"      X         ",
	"      o         ",
	"  O+  @#X   @$  ",
	"    $ @  % X    ",
	"  &*% =  = #    ",
	" -  * ;  : =    ",
	"  .>o @X,   **  ",
	"                ",
	"                ",
	"                ",
	"                ",
	"                "
};

wxIcon ibValueStaticText::GetIcon() const
{
	return wxIcon(s_static_text_xpm);
}

wxIcon ibValueStaticText::GetIconGroup()
{
	return wxIcon(s_static_text_xpm);
}