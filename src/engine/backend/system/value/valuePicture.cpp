////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value picture 
////////////////////////////////////////////////////////////////////////////

#include "valuePicture.h"

#include <vector>

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

ibValuePictureLib::ibValuePictureLib()
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE)
{
	m_members.Bind(this, &ibValuePictureLib::FillMembers);
}

void ibValuePictureLib::FillMembers(ibMemberTable& helper) const
{
	const std::vector<ibBackendPictureEntry> pictures = ibBackendPicture::GetArrayPicture();
	for (size_t i = 0; i < pictures.size(); ++i)
		helper.AppendProp(pictures[i].m_name, true, false, static_cast<long>(i));
}

bool ibValuePictureLib::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const std::vector<ibBackendPictureEntry> pictures = ibBackendPicture::GetArrayPicture();
	if (lPropNum < 0 || static_cast<size_t>(lPropNum) >= pictures.size())
		return false;
	pvarPropVal = new ibValuePicture(ibPictureDescription(pictures[lPropNum].m_id));
	return true;
}

ibValue* ibPictureLibrary()
{
	static ibValuePictureLib library;
	// A frame that names PictureLib holds this object and lets go when the frame ends.
	// The let-go would delete it. One hold keeps the process library alive.
	static struct Hold {
		Hold() { library.IncrRef(); }
	} hold;
	return &library;
}
