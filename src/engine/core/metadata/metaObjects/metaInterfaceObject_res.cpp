#include "metaInterfaceObject.h"

/* XPM */
static const char* s_interfaceGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 8 1",
	"  c None",
	"X c #FFFFFF",
	"o c #808080",
	". c #FFFF00",
	"# c #00FFFF",
	"@ c #FF0000",
	"+ c #0000FF",
	"O c #C0C0C0",
	/* pixels */
	"                ",
	"     .....      ",
	"    ...         ",
	"   ... XXX  X   ",
	"   ... Xo  XOX  ",
	"   .. XXXXX X+  ",
	"        XO XOXO ",
	"  OOOOOO X OXO  ",
	"  O++++O  OXOX  ",
	"  O++++O   O    ",
	"  O++++O   X    ",
	"  O++++O X OX   ",
	"  OOOOOO        ",
	"          @@# # ",
	" XXXXXXXXX #  ##",
	" OOOOOOOOO      "
};

/* XPM */
static const char* s_interface_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 6 1",
	"  c None",
	"X c #FFFFFF",
	". c #808080",
	"+ c #00FFFF",
	"O c #0000FF",
	"o c #C0C0C0",
	/* pixels */
	"                ",
	"                ",
	"          .X    ",
	"        .oXoX   ",
	"         XoX    ",
	"        X XoOo  ",
	"  oooooo XoXoXo ",
	"  oOOOOo oXoXo  ",
	"  oOOOOo XoXo   ",
	"  oOOOOo oXoXo  ",
	"  oOOOOo X      ",
	"  oooooo oXo    ",
	"         XoX    ",
	" XXXXXXXX X + + ",
	" oooooooo ++  ++",
	"                "
};

wxIcon CMetaInterfaceObject::GetIcon()
{
	return wxIcon(s_interface_xpm);
}

wxIcon CMetaInterfaceObject::GetIconGroup()
{
	return wxIcon(s_interfaceGroup_xpm);
}
