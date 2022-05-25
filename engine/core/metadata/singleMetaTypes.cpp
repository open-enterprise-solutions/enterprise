#include "metadata/metaObjects/objects/reference/reference.h"
#include "metadata/metaObjects/objects/list/objectList.h"
#include "metadata/metaObjects/objects/constant.h"

#include "singleMetaTypes.h"

wxBitmap IMetaTypeObjectValueSingle::GetClassIcon() const
{
	IMetaObject* metaObject = GetMetaObject();
	wxASSERT(metaObject);
	return metaObject->GetIcon();
}

//reference class 
wxClassInfo* CMetaTypeRefObjectValueSingle::GetClassInfo() const
{
	return CLASSINFO(CReferenceDataObject);
}

CValue* CMetaTypeRefObjectValueSingle::CreateObject() const
{
	return new CReferenceDataObject(m_metaValue);
}

//list class 
wxClassInfo* CMetaTypeListObjectValueSingle::GetClassInfo() const
{
	return CLASSINFO(CListDataObjectRef);
}

CValue* CMetaTypeListObjectValueSingle::CreateObject() const
{
	return new CListDataObjectRef(m_metaValue);
}

wxClassInfo* CMetaTypeListRegisterValueSingle::GetClassInfo() const
{
	return CLASSINFO(CListRegisterObject);
}

CValue* CMetaTypeListRegisterValueSingle::CreateObject() const
{
	return new CListRegisterObject(m_metaValue);
}

//object class
wxClassInfo* CMetaTypeObjectValueSingle::GetClassInfo() const
{
	IRecordDataObject* dataObject = m_metaValue->CreateObjectValue();
	wxASSERT(dataObject); wxClassInfo* classInfo = dataObject->GetClassInfo();
	delete dataObject; return classInfo;
}

CValue* CMetaTypeObjectValueSingle::CreateObject() const
{
	return m_metaValue->CreateObjectValue();
}

//const-object class
wxClassInfo* CMetaTypeConstObjectValueSingle::GetClassInfo() const
{
	return CLASSINFO(CConstantObject);
}

CValue* CMetaTypeConstObjectValueSingle::CreateObject() const
{
	return m_metaValue->CreateObjectValue();
}

//object record key
wxClassInfo* CMetaTypeRecordKeyValueSingle::GetClassInfo() const
{
	return CLASSINFO(CRecordKeyObject);
}

CValue* CMetaTypeRecordKeyValueSingle::CreateObject() const
{
	return new CRecordKeyObject(m_metaValue);
}

//object record manager
wxClassInfo* CMetaTypeRecordManagerValueSingle::GetClassInfo() const
{
	IRecordManagerObject* dataObject = m_metaValue->CreateRecordManagerValue();
	wxASSERT(dataObject); wxClassInfo* classInfo = dataObject->GetClassInfo();
	delete dataObject; return classInfo;
}

CValue* CMetaTypeRecordManagerValueSingle::CreateObject() const
{
	return m_metaValue->CreateRecordManagerValue();
}

//object record set
wxClassInfo* CMetaTypeRecordSetValueSingle::GetClassInfo() const
{
	IRecordSetObject* dataObject = m_metaValue->CreateRecordSetValue();
	wxASSERT(dataObject); wxClassInfo* classInfo = dataObject->GetClassInfo();
	delete dataObject; return classInfo;
}

CValue* CMetaTypeRecordSetValueSingle::CreateObject() const
{
	return m_metaValue->CreateRecordSetValue();
}