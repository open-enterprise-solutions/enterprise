#include "propertyPicture.h"
#include "backend/propertyManager/property/variant/variantPicture.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)


////////////////////////////////////////////////////////////////////////

ibVariant ibPropertyPicture::CreateVariantData(ibPropertyObject* property, const ibPictureDescription& id)
{
	return ibVariant(new ibVariantPicture(property, id));
}

////////////////////////////////////////////////////////////////////////

ibPicture ibPropertyPicture::GetValueAsPicture() const
{
	return get_cell_variant<ibVariantPicture>()->GetPicture();
}

ibPictureDescription& ibPropertyPicture::GetValueAsPictureDesc() const
{
	return get_cell_variant<ibVariantPicture>()->GetPictureDesc();
}

void ibPropertyPicture::SetValue(const ibPictureDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

////////////////////////////////////////////////////////////////////////

bool ibPropertyPicture::IsEmptyProperty() const {
	return get_cell_variant<ibVariantPicture>()->IsEmptyPicture();
}

#include "backend/system/value/valuePicture.h"

//base property for "picture"
bool ibPropertyPicture::SetDataValue(const ibValue& varPropVal)
{
	ibValuePicture* valueType = varPropVal.ConvertToType<ibValuePicture>();
	if (valueType != nullptr) {
		SetValue(valueType->GetPictureDesc());
		return true;
	}
	return false;
}

bool ibPropertyPicture::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValuePicture>(GetValueAsPictureDesc());
	return true;
}

bool ibPropertyPicture::ReadNodeValue(const ibDataValue& value)
{
	return ibBackendPictureDescriptionMemory::ReadNode(value, GetValueAsPictureDesc());
}

bool ibPropertyPicture::WriteNodeValue(ibDataValue& value) const
{
	return ibBackendPictureDescriptionMemory::WriteNode(value, GetValueAsPictureDesc());
}

#include "protocol/protocol.h"                          // ibProtocol::ibSchema::ibProperties — the row's keys
#include "backend/metaData.h"
#include "backend/metaCollection/metaPictureObject.h"   // ibValueMetaObjectPicture — its Guid

bool ibPropertyPicture::GetProtocolValue(ibDataNode& row, ibReader reader)
{
	if (!ibProperty::GetProtocolValue(row, reader))
		return false;

	const ibPropertyObject* const owner = m_owner;   // CONST: the non-const GetMetaData wxFAILs
	const ibMetaData* const metaData = owner != nullptr ? owner->GetMetaData() : nullptr;
	if (metaData == nullptr)
		return true;
	ibMetaData* metaDataOwner = nullptr;
	const ibMetaData* const source = metaData->GetOwner(metaDataOwner) && metaDataOwner != nullptr ? metaDataOwner : metaData;

	auto pictures = std::make_shared<ibDataNode>();
	for (const ibValueMetaObjectPicture* const picture : source->GetAnyArrayObject<ibValueMetaObjectPicture>(g_metaPictureCLSID)) {
		ibDataNode& item = pictures->AddChild(0, static_cast<ibMetaID>(pictures->Children().size() + 1));
		item.SetValue(ibProtocol::ibSchema::ibProperties::Name, picture->GetName());
		const ibGuid guid = picture->GetGuid();
		item.SetValue(ibProtocol::ibSchema::ibProperties::Guid, guid.str());
		item.SetValue(ibProtocol::ibSchema::ibProperties::Id, ibBackendPicture::SendPicture(guid, source));
	}
	row.SetProperty(ibProtocol::ibSchema::ibProperties::Pictures, ibDataValue::Child(pictures));
	return true;
}