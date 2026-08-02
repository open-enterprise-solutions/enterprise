#include "widgets.h"

/* XPM */
static const char* s_checkbox_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 20 1",
	"  c None",
	"# c #E8E8E8",
	": c #F7F7F7",
	"+ c #E1E1E1",
	"= c #F0F0F0",
	"o c #DADADA",
	", c #F9F9F9",
	"% c #E3E3E3",
	"- c #F2F2F2",
	"$ c #DCDCDC",
	"< c #EBEBEB",
	"X c #D6D6D6",
	"1 c #FCFCFC",
	"& c #009900",
	"@ c #E6E6E5",
	"> c #E6E6E6",
	"; c #F5F5F5",
	"O c #DFDFDF",
	"* c #EEEEEE",
	". c #333366",
	/* pixels */
	"                ",
	"                ",
	"  ............  ",
	"  .XXXooO+@##.  ",
	"  .XXX$O%%@&#.  ",
	"  .XX$O%%@&&*.  ",
	"  .XO++%@&&&=.  ",
	"  .+&+%@&&&=-.  ",
	"  .+&&@&&&=-;.  ",
	"  .+&&&&&=-;:.  ",
	"  .%>&&&=-;:,.  ",
	"  .@#<&==;:,1.  ",
	"  .#<*=-;:,11.  ",
	"  ............  ",
	"                ",
	"                "
};

wxIcon ibValueCheckbox::GetIcon() const
{
	return wxIcon(s_checkbox_xpm);
}

wxIcon ibValueCheckbox::GetIconGroup()
{
	return wxIcon(s_checkbox_xpm);
}