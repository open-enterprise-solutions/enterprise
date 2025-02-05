#include "widgets.h"

/* XPM */
static char* s_static_text_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"22 22 3 1",
	"  c black",
	". c magenta",
	"X c None",
	/* pixels */
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXX XXXXXXXXXXXX",
	"XXXXXXXXX XXXXXXXXXXXX",
	"XXXXXXXXX XXXXXXXXXXXX",
	"XXXXX  XX   XXX  XXXXX",
	"XXXXXXX X XX X XXXXXXX",
	"XXXXX   X XX X XXXXXXX",
	"XXXX XX X XX X XXXXXXX",
	"XXXXX   X   XXX  XXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXXXXXXXX"
};

wxIcon CValueStaticText::GetIcon() const
{
	return wxIcon(s_static_text_xpm);
}

wxIcon CValueStaticText::GetIconGroup()
{
	return wxIcon(s_static_text_xpm);
}