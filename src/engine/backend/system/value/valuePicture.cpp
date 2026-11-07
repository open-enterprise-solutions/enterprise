////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : value picture 
////////////////////////////////////////////////////////////////////////////

#include "valuePicture.h"

#include <algorithm>
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

VALUE_TYPE_REGISTER(ibValuePicture, "StoragePicture", g_valuePictureCLSID);

#include "backend/metaData.h"
#include "backend/metaCollection/metaPictureObject.h"   // ibValueMetaObjectPicture — the configuration's own

ibValuePictureLib::ibValuePictureLib(const ibMetaData* metaData)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_metaData(metaData)
{
	m_members.Bind(this, &ibValuePictureLib::FillMembers);
}

void ibValuePictureLib::FillMembers(ibMemberTable& helper) const
{
	m_pictures.clear();

	const auto append = [&](const ibString& name, const ibPictureDescription& picture) {
		helper.AppendProp(name, true, false, static_cast<long>(m_pictures.size()));
		m_pictures.push_back(picture);
	};

	// The configuration's pictures first: an external processor's or report's are its configuration's, as the picker reads them.
	std::vector<ibString> names;
	if (m_metaData != nullptr) {
		ibMetaData* owner = nullptr;
		const ibMetaData* const source = m_metaData->GetOwner(owner) && owner != nullptr ? owner : m_metaData;
		for (const ibValueMetaObjectPicture* const picture : source->GetAnyArrayObject<ibValueMetaObjectPicture>(g_metaPictureCLSID)) {
			append(picture->GetName(), ibPictureDescription(picture->GetGuid()));
			names.push_back(picture->GetName());
		}
	}

	// Then the engine's. A name the configuration already gave is the configuration's picture.
	for (const ibPictureEntry& entry : ibBackendPicture::GetArrayPicture()) {
		const bool taken = std::any_of(names.begin(), names.end(),
			[&entry](const ibString& name) { return stringUtils::IsSameAs(name, entry.m_name); });
		if (!taken)
			append(entry.m_name, ibPictureDescription(entry.m_id));
	}
}

bool ibValuePictureLib::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long index = m_members.GetPropData(lPropNum);
	if (index < 0 || static_cast<size_t>(index) >= m_pictures.size())
		return false;
	pvarPropVal = ibValue::CreateObjectValue<ibValuePicture>(m_pictures[index]);
	return true;
}

// SYSTEM, not a value type: a script cannot make one, the module manager vends it. It still needs the entry — the
// compiler asks every context value for its type (PrepareModuleData), and a type not in the registry asserts there.
SYSTEM_TYPE_REGISTER(ibValuePictureLib, "PictureLib");
