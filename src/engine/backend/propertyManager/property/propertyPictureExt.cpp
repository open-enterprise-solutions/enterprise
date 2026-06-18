#include "propertyPicture.h"
#include "backend/propertyManager/property/variant/variantPicture.h"
#include "backend/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)

////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyExternalPicture::CreateVariantData(const ibExternalPictureDescription& id) const
{
	return new ibVariantDataExternalPicture(id);
}

////////////////////////////////////////////////////////////////////////

wxBitmap ibPropertyExternalPicture::GetValueAsBitmap() const
{
	return get_cell_variant<ibVariantDataExternalPicture>()->GetPictureBitmap();
}

ibExternalPictureDescription& ibPropertyExternalPicture::GetValueAsPictureDesc() const
{
	return get_cell_variant<ibVariantDataExternalPicture>()->GetExternalPictureDesc();
}

void ibPropertyExternalPicture::SetValue(const ibExternalPictureDescription& val)
{
	m_propValue = CreateVariantData(val);
}

////////////////////////////////////////////////////////////////////////

bool ibPropertyExternalPicture::IsEmptyProperty() const {
	return get_cell_variant<ibVariantDataExternalPicture>()->IsEmptyPicture();
}

#include "backend/system/value/valuePicture.h"

// get property for grid
wxObject* (*ibPropertyExternalPicture::ms_propertyExtPicture)(
	const wxString&,
	const wxString&,
	const wxVariant&) = nullptr;

//base property for "external picture"
bool ibPropertyExternalPicture::SetDataValue(const ibValue& varPropVal)
{
	return false;
}

bool ibPropertyExternalPicture::GetDataValue(ibValue& pvarPropVal) const
{
	pvarPropVal = ibValue::CreateObjectValue<ibValuePicture>(GetValueAsPictureDesc());
	return true;
}

bool ibPropertyExternalPicture::ReadNodeValue(const ibDataValue& value)
{
	return ibExternalPictureDescriptionMemory::ReadNode(value, GetValueAsPictureDesc());
}

bool ibPropertyExternalPicture::WriteNodeValue(ibDataValue& value) const
{
	return ibExternalPictureDescriptionMemory::WriteNode(value, GetValueAsPictureDesc());
}