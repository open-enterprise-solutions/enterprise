#include "propertyPicture.h"
#include "backend/propertyManager/property/variant/variantPicture.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)


////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyPicture::CreateVariantData(ibPropertyObject* property, const ibPictureDescription& id) const
{
	return new ibVariantDataPicture(property, id);
}

////////////////////////////////////////////////////////////////////////

wxBitmap ibPropertyPicture::GetValueAsBitmap() const
{
	return get_cell_variant<ibVariantDataPicture>()->GetPictureBitmap();
}

ibPictureDescription& ibPropertyPicture::GetValueAsPictureDesc() const
{
	return get_cell_variant<ibVariantDataPicture>()->GetPictureDesc();
}

void ibPropertyPicture::SetValue(const ibPictureDescription& val)
{
	m_propValue = CreateVariantData(m_owner, val);
}

////////////////////////////////////////////////////////////////////////

bool ibPropertyPicture::IsEmptyProperty() const {
	return get_cell_variant<ibVariantDataPicture>()->IsEmptyPicture();
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
	return ibPictureDescriptionMemory::ReadNode(value, GetValueAsPictureDesc());
}

bool ibPropertyPicture::WriteNodeValue(ibDataValue& value) const
{
	return ibPictureDescriptionMemory::WriteNode(value, GetValueAsPictureDesc());
}