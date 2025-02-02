#include "document.h"
/* XPM */
static const char *s_document_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 5 1",
	"O c Black",
	"X c #FFFFFF",
	". c #808080",
	"o c #C0C0C0",
	"  c None",
	/* pixels */
	"                ",
	".........       ",
	".XXXXXXX..      ",
	".XXXXXXX.o.     ",
	".XXXXXXX...O    ",
	".XOOOOOXXXoO    ",
	".XXXXXXXXXoO    ",
	".XOOOOOOOXoO    ",
	".XXXXXXXXXoO    ",
	".XOOOOOOOXoO    ",
	".XXXXXXXXXoO    ",
	".XOOOOOOOXoO    ",
	".XXXXXXXXXoO    ",
	".ooooooooooO    ",
	".OOOOOOOOOOO    ",
	"                "
};

/* XPM */
static const char *s_documentGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"o c #FFFFFF",
	"O c #FFFF00",
	"@ c #040404",
	"+ c #C0C0C0",
	"X c #808000",
	"  c None",
	". c #868686",
	/* pixels */
	"                ",
	"    ..X         ",
	"   .oooX        ",
	"  .oO+O+XXXXXXX ",
	"  .o+O+O+oooooX@",
	"  .oO+O+O+O+O+X@",
	" ...........+OX@",
	" .ooooooooo..+X@",
	" .o+O+O+O+O+..X@",
	"  .o+O+O+O+O+.X@",
	"   .o+O+O+O+O.X@",
	"   XXXXXXXXXXXX@",
	"    @@@@@@@@@@@@",
	"                ",
	"                ",
	"                "
};

wxIcon CMetaObjectDocument::GetIcon() const
{
	return wxIcon(s_document_xpm);
}

wxIcon CMetaObjectDocument::GetIconGroup()
{
	return wxIcon(s_documentGroup_xpm);
}
