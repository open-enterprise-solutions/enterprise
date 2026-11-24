#include "metaCommandGroupObject.h"
#include "core/graphics/picture.h"   // ibPicture::FromSvg — the drawing, at 16 once
#include "core/picturePredefined.h"    // g_picCommandGroup*CLSID — the platform groups' pictures

static const ibString s_commandGroup_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M3.5 2 H1.5 V14 H3.5' fill='none' stroke='#5a7b95' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><rect x='4.5' y='2.5' width='10.5' height='5' rx='1.5' fill='#1c6fd1'/><path d='M7 5 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/><rect x='4.5' y='8.5' width='10.5' height='5' rx='1.5' fill='#1c6fd1'/><path d='M7 11 H12.5' fill='none' stroke='#ffffff' stroke-width='1.4' stroke-linecap='round' stroke-linejoin='round'/></svg>");

// The platform's own groups — the engine's pictures (core/picturePredefined.h), each by its id.
ibPictureID ibCommandGroupPicture(ibInterfaceCommandSection area)
{
	switch (area) {
		case ibInterfaceCommandSection_Important: return g_picCommandGroupImportantCLSID;
		case ibInterfaceCommandSection_Default:   return g_picCommandGroupNormalCLSID;
		case ibInterfaceCommandSection_Create:    return g_picCommandGroupCreateCLSID;
		case ibInterfaceCommandSection_Report:    return g_picCommandGroupReportsCLSID;
		case ibInterfaceCommandSection_Service:   return g_picCommandGroupServiceCLSID;
		default:                                  return 0;
	}
}

ibPicture ibValueMetaObjectCommandGroup::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectCommandGroup::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_commandGroup_svg);

	return picture;
}
