#include "tableBox.h"

/* XPM */
static char* s_dataviewlist_column_xpm[] = {
"21 21 7 1",
" 	c None",
".	c #3E9ADE",
"+	c #858585",
"@	c #2C2C2C",
"#	c #FF3300",
"$	c #FFFFFF",
"%	c #B8B8B8",
"..............       ",
".++@++@#######       ",
".+@+@@+#######       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+%%%%%#######       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+%%%%%#######       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+%%%%%#######       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       ",
".+%%%%%#######       ",
".+$$$$$#$$$$$#       ",
".+$$$$$#$$$$$#       " };

wxIcon CValueTableBoxColumn::GetIcon() const
{
	return wxIcon(s_dataviewlist_column_xpm);
}

wxIcon CValueTableBoxColumn::GetIconGroup()
{
	return wxIcon(s_dataviewlist_column_xpm);
}