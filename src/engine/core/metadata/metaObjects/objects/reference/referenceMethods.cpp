////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference - methods
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "compiler/methods.h"
#include "metadata/metaObjects/objects/object.h"
#include "metadata/metaObjects/table/metaTableObject.h"

enum
{
	enIsEmpty = 0,
	enGetMetadata,
	enGetObject,
	enGetGuid
};

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

CMethods* CReferenceDataObject::GetPMethods() const
{
	CReferenceDataObject::PrepareNames();
	return m_methods;
};

void CReferenceDataObject::PrepareNames() const
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		SEng aMethods[] =
		{
			{"isEmpty", "IsEmpty()"},
			{"getMetadata", "getMetadata()"},
			{"getObject", "getObject()"},
			{"getGuid", "getGuid()"},
		};

		int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
		m_methods->PrepareMethods(aMethods, nCountM);

		std::vector<SEng> aAttributes;

		//fill custom attributes 
		for (auto attributes : metaObject->GetGenericAttributes()) {
			SEng attribute;
			attribute.sName = attributes->GetName();
			attribute.iName = attributes->GetMetaID();
			attribute.sSynonym = wxT("attribute");
			aAttributes.push_back(attribute);
		}

		//fill part tables attributes 
		for (auto table : metaObject->GetObjectTables()) {
			SEng attribute;
			attribute.sName = table->GetName();
			attribute.iName = table->GetMetaID();
			attribute.sSynonym = wxT("table");
			aAttributes.push_back(attribute);
		}

		m_methods->PrepareAttributes(aAttributes.data(), aAttributes.size());
	}
	else {
		SEng aMethods[] =
		{
			{"isEmpty", "IsEmpty()"},
			{"getMetadata", "getMetadata()"},
		};

		int nCountM = sizeof(aMethods) / sizeof(aMethods[0]);
		m_methods->PrepareMethods(aMethods, nCountM);
	}
}

CValue CReferenceDataObject::Method(methodArg_t& aParams)
{
	switch (aParams.GetIndex())
	{
	case enIsEmpty: 
		return IsEmptyRef();
	case enGetMetadata:
		return GetMetadata();
	case enGetObject: 
		return GetObject();
	case enGetGuid: 
		return new CValueGuid(m_objGuid);
	}

	return CValue();
}