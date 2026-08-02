#include "widgets.h"

/* XPM */
static const char* s_gauge_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 18 1",
	"  c None",
	"# c #EEFFFF",
	"X c #FFFFFF",
	"@ c #12A50C",
	"* c #EAFFFF",
	"o c #F9FFFF",
	"$ c #2EB71E",
	"& c #2EB71F",
	": c #6CE147",
	"> c #6CE148",
	", c #DEFFFF",
	"O c #009900",
	"= c #4DCC33",
	"+ c #F6FFFF",
	"- c #E3FFFF",
	"; c #6BE148",
	"% c #2EB81F",
	". c #333366",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"                ",
	" .............. ",
	".XXXXXXXXXXXXoX.",
	".XOOXOOXOOoOO++.",
	".X@@X@@+@@+@@+#.",
	".o$%+$$+$%#&$#*.",
	".+==#==#==*==--.",
	".#;:#>;*;>-:;,-.",
	".*-*--------,,,.",
	" .............. ",
	"                ",
	"                ",
	"                "
};

wxIcon ibValueGauge::GetIcon() const
{
	return wxIcon(s_gauge_xpm);
}

wxIcon ibValueGauge::GetIconGroup()
{
	return wxIcon(s_gauge_xpm);
}