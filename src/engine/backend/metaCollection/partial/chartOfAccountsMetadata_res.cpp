#include "chartOfAccounts.h"

/* The chart's book, orange, with an account's T */
static const ibString s_chartOfAccounts_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='2' y='1' width='12' height='14' rx='1.5' fill='#f76707'/><path d='M3.5 1 H5 V15 H3.5 Q2 15 2 13.5 V2.5 Q2 1 3.5 1 Z' fill='#d9480f'/><path d='M6.8 5 H12.2' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='butt' stroke-linejoin='round'/><path d='M9.5 5 V12' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='butt' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectChartOfAccounts::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectChartOfAccounts::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_chartOfAccounts_svg);
	return picture;
}
