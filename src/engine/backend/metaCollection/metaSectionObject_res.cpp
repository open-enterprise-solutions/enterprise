#include "metaSectionObject.h"

/* PNG */
static const wxString s_interface_16_png = wxT("iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAA+klEQVR4nOzasQ7BYBTF8X/FQ3gDm4mFhdVmE29gMnoGo8kbiM1mZWGpyeYNvAWTRBpt02pS7Tm/rd/tdHK/m9ukDcQ1EOcAEOcAEOcAENeMHnT7wyc1dr2cgs9nd0Bc4dFbUyetcPH13EMQcQ4AcQ4AcQ4AcQ4Acf4WiCu8d+fwfKTKeoNRYt0dkPbC9s5PZm3+mocg4jwDyGi5uSXWV/MOVeIOoCTjyZQiHfY78nAHkFHV7ngadwAlyXtni+YOICPvATVTuT2g6NnhDiAj7wE14z0AcQ4AcQ4AcQ4AcQ4AcUH0wP8Ki3EAiAsQ5yuAOAeAOAeAOPkAXgAAAP//GtfCEwAAAAZJREFUAwDEUSRDPA6nNAAAAABJRU5ErkJggg==");

wxIcon ibValueMetaObjectSection::GetIcon() const
{
	return GetIconGroup();
}

wxIcon ibValueMetaObjectSection::GetIconGroup()
{
	static wxIcon icon =
		ibBackendPicture::GetIconFromBase64(s_interface_16_png, wxSize(16, 16));

	return icon;
}
