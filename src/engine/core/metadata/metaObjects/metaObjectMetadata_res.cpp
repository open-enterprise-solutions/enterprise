#include "metaObjectMetadata.h"

/* XPM */
static const char *s_metadata_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 8 1",
	". c Black",
	"o c #FFFFFF",
	"O c #808080",
	"@ c #000080",
	"# c #00FFFF",
	"+ c #800000",
	"X c #0000FF",
	"  c None",
	/* pixels */
	"  ..............",
	"  .XXXXXXXXXXXX.",
	"  .XXXXXXXXXXXX.",
	"......ooooooooo.",
	".oooo..OOOo+++o.",
	".o@......oooooo.",
	".oo.oooo..o@@oo.",
	".o@.o@......ooo.",
	".oo.oo.#o#o..@o.",
	".o@.o@.o@@#...o.",
	".oo.oo.#o#o#o.o.",
	"....o@.o@@@@#.o.",
	"   .oo.#o#o#o...",
	"   ....o@@@@#.  ",
	"      .#o#o#o.  ",
	"      ........  "
};

wxIcon CMetaObject::GetIcon()
{
	return wxIcon(s_metadata_xpm);
}

wxIcon CMetaObject::GetIconGroup()
{
	return wxIcon(s_metadata_xpm);
}