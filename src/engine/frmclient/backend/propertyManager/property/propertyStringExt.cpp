#include "propertyString.h"
#include "core/serialize/dataBuilder.h"   // ibDataValue — node value (String, the stored form)


////////////////////////////////////////////////////////////////////////

// ⚠ HELD AS THE RAW FORM, unchanged — its node (ibBackendProperty::ReadNodeValue). A configuration written before this
// reads as it always did, and one written now is still read by everything that speaks the format.
ibTranslateString ibPropertyTString::GetValueAsTranslate() const
{
	return ibTranslateString(GetNodeValue().AsString());
}

void ibPropertyTString::SetValue(const ibTranslateString& translate)
{
	ibProperty::ReadNodeValue(ibDataValue::String(translate.GetRawText()));
}

bool ibPropertyTString::ReadNodeValue(const ibDataValue& value)
{
	ibProperty::ReadNodeValue(ibDataValue::String(value.AsString()));
	return true;
}
