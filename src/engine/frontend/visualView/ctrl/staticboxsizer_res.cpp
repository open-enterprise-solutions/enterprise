#include "sizers.h"

/* XPM */
static char* s_staticbox_sizer_xpm[] = {
"21 21 7 1",
" 	c None",
"!	c black",
"#	c #003399",
"$	c #B7B79B",
"%	c #0000FF",
"&	c #4949FF",
"'	c #9B9BFF",
"                     ",
"      #####          ",
"   $$ ##### $$$$$$$  ",
"  $                $ ",
" $  %%%%%%%%%%%%%%  $",
" $ %&''''''''''''&% $",
" $ %'            '% $",
" $ %'            '% $",
" $ %'            '% $",
" $ %&''''''''''''&% $",
" $ %%%%%%%%%%%%%%%% $",
" $ %&''''''''''''&% $",
" $ %'            '% $",
" $ %'            '% $",
" $ %'            '% $",
" $ %&''''''''''''&% $",
" $  %%%%%%%%%%%%%%  $",
"  $                $ ",
"   $$$$$$$$$$$$$$$$  ",
"                     ",
"                     "
};

wxIcon CValueStaticBoxSizer::GetIcon() const
{
	return wxIcon(s_staticbox_sizer_xpm);
}

wxIcon CValueStaticBoxSizer::GetIconGroup()
{
	return wxIcon(s_staticbox_sizer_xpm);
}