#include "sizers.h"

/* XPM */
static char* s_boxSizer_xpm[] = {
"21 21 4 1",
" 	c None",
".	c #0000FF",
"+	c #4949FF",
"@	c #9B9BFF",
"                     ",
"  .................  ",
" .+@@@@@@@@@@@@@@@+. ",
" .@               @. ",
" .@               @. ",
" .@               @. ",
" .+@@@@@@@@@@@@@@@+. ",
" ................... ",
" .+@@@@@@@@@@@@@@@+. ",
" .@               @. ",
" .@               @. ",
" .@               @. ",
" .+@@@@@@@@@@@@@@@+. ",
" ................... ",
" .+@@@@@@@@@@@@@@@+. ",
" .@               @. ",
" .@               @. ",
" .@               @. ",
" .+@@@@@@@@@@@@@@@+. ",
"  .................  ",
"                     " };

wxIcon CValueBoxSizer::GetIcon()
{
	return wxIcon(s_boxSizer_xpm);
}

wxIcon CValueBoxSizer::GetIconGroup()
{
	return wxIcon(s_boxSizer_xpm);
}