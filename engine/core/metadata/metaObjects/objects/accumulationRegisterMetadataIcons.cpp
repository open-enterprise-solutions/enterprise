#include "accumulationRegister.h"

/* XPM */
static const char* s_informationRegister_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"o c Black",
	"O c #FFFFFF",
	". c #808080",
	"+ c #00FFFF",
	"X c #0000FF",
	"@ c #C0C0C0",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	"                ",
	"  ............. ",
	"  .XXXXXXXXXXXo ",
	"  ............o ",
	"  .O+O+O+O+O+@o ",
	"  .OooOooOooO@o ",
	"  .O+O+O+O+O+@o ",
	"  .OooOooOooO@o ",
	"  .O+O+O+O+O+@o ",
	"  .OooOooOooO@o ",
	"  .@@@@@@@@@@@o ",
	"  .oooooooooooo ",
	"                ",
	"                "
};

/* XPM */
static const char* s_informationRegisterGroup_xpm[] = {
	/* columns rows colors chars-per-pixel */
	"16 16 7 1",
	"o c Black",
	"O c #FFFFFF",
	". c #808080",
	"+ c #00FFFF",
	"X c #0000FF",
	"@ c #C0C0C0",
	"  c None",
	/* pixels */
	"                ",
	"                ",
	".............   ",
	".XXXXXXXXXXXo   ",
	"................",
	".OO.XXXXXXXXXXXo",
	".Oo............o",
	".OO.O+O+O+O+O+@o",
	".Oo.OooOooOooO@o",
	".OO.O+O+O+O+O+@o",
	".Oo.OooOooOooO@o",
	".@@.O+O+O+O+O+@o",
	".oo.OooOooOooO@o",
	"   .@@@@@@@@@@@o",
	"   .oooooooooooo",
	"                "
};

wxIcon CMetaObjectAccumulationRegister::GetIcon()
{
	return wxIcon(s_informationRegister_xpm);
}

wxIcon CMetaObjectAccumulationRegister::GetIconGroup()
{
	return wxIcon(s_informationRegisterGroup_xpm);
}
