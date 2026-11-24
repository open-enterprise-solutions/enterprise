#include "metaPictureObject.h"

static const ibString s_picture_svg = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><rect x='1' y='2' width='14' height='12' rx='1.5' fill='#1c6fd1'/><circle cx='5' cy='6' r='1.8' fill='#fcc419'/><path d='M1 12 L5.5 8 L8.5 10.5 L11 8.5 L15 12 V12.5 Q15 14 13.5 14 H2.5 Q1 14 1 12.5 Z' fill='#8ce99a'/></svg>");

ibPicture ibValueMetaObjectPicture::GetIcon() const
{
	//if (!m_propertyPicture->IsEmptyProperty()) {
	//	wxIcon icon;
	//	icon.CopyFromBitmap(m_propertyPicture->GetValueAsBitmap());
	//	return icon;
	//}

	return GetIconGroup();
}

ibPicture ibValueMetaObjectPicture::GetIconGroup()
{
	static const ibPicture picture = ibPicture::FromSvg(s_picture_svg);

	return picture;
}
