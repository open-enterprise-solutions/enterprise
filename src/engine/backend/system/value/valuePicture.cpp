////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value picture 
////////////////////////////////////////////////////////////////////////////

#include "valuePicture.h"

//////////////////////////////////////////////////////////////////////

ibValuePicture::ibValuePicture(const ibPictureDescription& pictureDesc) :
	ibValue(ibValueTypes::TYPE_VALUE), m_pictureDesc(pictureDesc)
{
}

bool ibValuePicture::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	// Get from FS
	return ibBackendPicture::LoadFromFile(
		paParams[0]->GetString(), m_pictureDesc);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValuePicture, "StoragePicture", value_to_clsid("SY_PICTR"));
