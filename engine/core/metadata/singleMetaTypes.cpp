#include "metadata/objects/reference/reference.h"
#include "metadata/objects/list/objectList.h"

#include "singleMetaTypes.h"

//reference class 
wxClassInfo *CMetaTypeRefObjectValueSingle::GetClassInfo() const
{
	return CLASSINFO(CValueReference);
}

CValue *CMetaTypeRefObjectValueSingle::CreateObject() const
{
	return new CValueReference(m_metaValue);
}

//list class 
wxClassInfo *CMetaTypeListObjectValueSingle::GetClassInfo() const
{
	return CLASSINFO(CDataObjectList);
}

CValue *CMetaTypeListObjectValueSingle::CreateObject() const
{
	return new CDataObjectList(m_metaValue, 0);
}

//object class
wxClassInfo *CMetaTypeObjectValueSingle::GetClassInfo() const
{
	IDataObjectValue *dataObject = m_metaValue->CreateObjectValue();
	wxASSERT(dataObject); wxClassInfo *classInfo = dataObject->GetClassInfo();
	delete dataObject; return classInfo;
}

CValue *CMetaTypeObjectValueSingle::CreateObject() const
{
	return m_metaValue->CreateObjectValue();
}
