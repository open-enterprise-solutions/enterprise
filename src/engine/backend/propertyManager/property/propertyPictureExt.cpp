#include "propertyPicture.h"
#include "backend/propertyManager/property/variant/variantPicture.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (Binary, transitional)

////////////////////////////////////////////////////////////////////////

wxVariantData* ibPropertyExternalPicture::CreateVariantData(const ibExternalPictureDescription& id)
{
	return new ibVariantDataExternalPicture(id);
}

////////////////////////////////////////////////////////////////////////

wxBitmap ibPropertyExternalPicture::GetValueAsBitmap() const
{
	return get_cell_variant<ibVariantDataExternalPicture>()->GetPictureBitmap();
}

const ibExternalPictureDescription& ibPropertyExternalPicture::GetValueAsPictureDesc() const
{
	return get_cell_variant<ibVariantDataExternalPicture>()->GetExternalPictureDesc();
}

ibGuid ibPropertyExternalPicture::GetValueAsVersion() const
{
	return get_cell_variant<ibVariantDataExternalPicture>()->GetPictureVersion();
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
	// Read whole, then set: the cell is a new value, with a version of its own — never written in place.
	ibExternalPictureDescription read;
	if (!ibExternalPictureDescriptionMemory::ReadNode(value, read))
		return false;
	SetValue(read);
	return true;
}

bool ibPropertyExternalPicture::WriteNodeValue(ibDataValue& value) const
{
	return ibExternalPictureDescriptionMemory::WriteNode(value, GetValueAsPictureDesc());
}