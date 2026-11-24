#include "metaModuleObject.h"

static const ibString s_module_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3.5 1 H10 L14 5 V13.5 Q14 15 12.5 15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#5a7b95'/><path d='M10 1 V3.8 Q10 5 11.2 5 H14 Z' fill='#a8bac8'/><path d='M4.5 7.5 H8' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><path d='M6.5 10 H11.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><path d='M4.5 12.5 H9' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectModuleBase::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectModuleBase::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_module_svg);

	return picture;
}

ibPicture ibValueMetaObjectSharedModule::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectSharedModule::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_module_svg);

	return picture;
}

ibPicture ibValueMetaObjectManagerModule::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectManagerModule::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_module_svg);

	return picture;
}
