#include "accountingRegister.h"

/* The register's square, teal, with an account's T: debit on the left, credit on the right */
static const ibString s_accountingRegister_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='1' width='14' height='14' rx='2.5' fill='#1098ad'/><path d='M3 4.2 H13 M8 4.2 V12.6' fill='none' stroke='#ffffff' stroke-width='1.5' stroke-linecap='butt' stroke-linejoin='round'/><path d='M3.8 8.6 H6.6 M5.2 7.2 V10' fill='none' stroke='#ffffff' stroke-width='1.2' stroke-linecap='butt' stroke-linejoin='round'/><path d='M9.4 8.6 H12.2' fill='none' stroke='#ffffff' stroke-width='1.2' stroke-linecap='butt' stroke-linejoin='round'/></svg>");

ibPicture ibValueMetaObjectAccountingRegister::GetIcon() const
{
	return GetIconGroup();
}

ibPicture ibValueMetaObjectAccountingRegister::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_accountingRegister_svg);
	return picture;
}
