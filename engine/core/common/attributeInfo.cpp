#include "metadata/metaObjects/metaObject.h"

#include "attributeInfo.h"
#include "metadata/metadata.h"
#include "metadata/singleMetaTypes.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierNumber, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierDate, CValue);
wxIMPLEMENT_DYNAMIC_CLASS(CValueQualifierString, CValue);

CLASS_ID IAttributeInfo::GetClassTypeObject() const
{
	IMetadata *metaData = GetMetadata();
	wxASSERT(metaData);
	if (m_typeDescription.GetTypeObject() >= defaultMetaID) {
		IMetaObjectValue *metaObject = wxDynamicCast(
			metaData->GetMetaObject(m_typeDescription.GetTypeObject()), IMetaObjectValue
		);
		wxASSERT(metaObject);
		IMetaTypeObjectValueSingle *clsFactory =
			metaObject->GetTypeObject(eMetaObjectType::enReference);
		wxASSERT(clsFactory);
		return clsFactory->GetTypeID();
	}

	return CValue::GetIDByVT((const eValueTypes)m_typeDescription.GetTypeObject());
}

CValueTypeDescription *IAttributeInfo::GetValueTypeDescription() const
{
	CLASS_ID clsid = GetClassTypeObject();
	return new CValueTypeDescription(clsid,
		GetNumberQualifier(),
		GetDateQualifier(),
		GetStringQualifier()
	);
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_REGISTER(CValueQualifierNumber, "qualifierNumber", TEXT2CLSID("VL_QLFN"));
VALUE_REGISTER(CValueQualifierDate, "qualifierDate", TEXT2CLSID("VL_QLFD"));
VALUE_REGISTER(CValueQualifierString, "qualifierString", TEXT2CLSID("VL_QLFS"));