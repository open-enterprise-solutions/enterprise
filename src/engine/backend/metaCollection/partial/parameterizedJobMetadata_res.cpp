#include "parameterizedJob.h"

/* The same alarm clock the predefined kind wears, a list of parameters beside it: one subsystem, one visual. */
static const ibString s_parameterizedJob_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><path d='M1.6 4.2 L3.6 2.4' fill='none' stroke='#e03131' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><path d='M12.4 4.2 L10.4 2.4' fill='none' stroke='#e03131' stroke-width='1.8' stroke-linecap='round' stroke-linejoin='round'/><circle cx='7' cy='9' r='5.3' fill='#e03131'/><path d='M7 6.2 V9 H9' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='round' stroke-linejoin='round'/><rect x='9.5' y='9.5' width='6' height='6' rx='1.2' fill='#1c6fd1' stroke='#ffffff' stroke-width='1'/><path d='M11.3 11.8 H13.7 M11.3 13.4 H13.7' fill='none' stroke='#ffffff' stroke-width='1' stroke-linecap='butt' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectParameterizedJob::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectParameterizedJob::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_parameterizedJob_svg);

	return picture;
}
