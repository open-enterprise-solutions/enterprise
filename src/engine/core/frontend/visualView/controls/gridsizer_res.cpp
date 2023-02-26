#include "sizers.h"

/* XPM */
static char* s_gridSizer_xpm[] = {
"21 21 4 1",
" 	c None",
".	c #0000FF",
"+	c #4949FF",
"@	c #9B9BFF",
"                     ",
"  .................  ",
" .+@@@@@@+.+@@@@@@+. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .+@@@@@@+.+@@@@@@+. ",
" ................... ",
" .+@@@@@@+.+@@@@@@+. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .@      @.@      @. ",
" .+@@@@@@+.+@@@@@@+. ",
"  .................  ",
"                     " };

wxIcon CValueGridSizer::GetIcon()
{
	return wxIcon(s_gridSizer_xpm);
}

wxIcon CValueGridSizer::GetIconGroup()
{
	return wxIcon(s_gridSizer_xpm);
}