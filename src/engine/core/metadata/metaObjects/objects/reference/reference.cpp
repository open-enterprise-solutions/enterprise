////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "core/metadata/metadata.h"
#include "core/metadata/metaObjects/objects/object.h"
#include "core/metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include "utils/stringUtils.h"

wxIMPLEMENT_DYNAMIC_CLASS(CReferenceDataObject, CValue);

//**********************************************************************************************
//*                                     reference                                              *        
//**********************************************************************************************

void CReferenceDataObject::PrepareRef(bool createData)
{
	wxASSERT(m_metaObject != NULL);

	if (m_initializedRef)
		return;

	if (CReferenceDataObject::IsEmpty()) {
		//attrbutes can refValue 
		for (auto attribute : m_metaObject->GetGenericAttributes()) {
			if (!m_metaObject->IsDataReference(attribute->GetMetaID())) {
				m_objectValues.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
			}
		}

		// table is collection values 
		for (auto table : m_metaObject->GetObjectTables()) {
			m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObjectRef(this, table));
		}
	}
	else if (CReferenceDataObject::ReadData(createData)) {
		m_foundedRef = true; m_newObject = false;
	}

	if (createData) {
		m_initializedRef = true;
	}
}

CReferenceDataObject::CReferenceDataObject(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid) : CValue(eValueTypes::TYPE_VALUE, true), IObjectValueInfo(objGuid, !objGuid.isValid()),
m_metaObject(metaObject), m_methodHelper(new CMethodHelper()), m_initializedRef(false), m_reference_impl(NULL), m_foundedRef(false)
{
	m_reference_impl = new reference_t(
		m_metaObject->GetMetaID(), m_objGuid
	);
}

CReferenceDataObject::~CReferenceDataObject()
{
	wxDELETE(m_methodHelper);
	wxDELETE(m_reference_impl);
}

CReferenceDataObject* CReferenceDataObject::Create(IMetadata* metaData, const meta_identifier_t& id, const Guid& objGuid)
{
	IMetaObjectRecordDataRef* metaObject = NULL;
	if (metaData->GetMetaObject(metaObject, id)) {
		CReferenceDataObject* refData = new CReferenceDataObject(
			metaObject, objGuid
		);
		if (refData != NULL)
			refData->PrepareRef(true);
		return refData;
	}
	return NULL;
}

CReferenceDataObject* CReferenceDataObject::Create(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid)
{
	CReferenceDataObject* refData = new CReferenceDataObject(
		metaObject, objGuid
	);
	if (refData != NULL)
		refData->PrepareRef(true);
	return refData;
}

CReferenceDataObject* CReferenceDataObject::Create(IMetadata* metaData, void* ptr)
{
	reference_t* reference = static_cast<reference_t*>(ptr);
	if (reference != NULL) {
		IMetaObjectRecordDataRef* metaObject = NULL;
		if (metaData->GetMetaObject(metaObject, reference->m_id)) {
			return new CReferenceDataObject(
				metaObject, reference->m_guid
			);
		}
	}
	return NULL;
}

CReferenceDataObject* CReferenceDataObject::CreateFromPtr(IMetadata* metaData, void* ptr)
{
	reference_t* reference = static_cast<reference_t*>(ptr);
	if (reference != NULL) {
		IMetaObjectRecordDataRef* metaObject = NULL;
		if (metaData->GetMetaObject(metaObject, reference->m_id)) {
			CReferenceDataObject* refData = new CReferenceDataObject(
				metaObject, reference->m_guid
			);
			if (refData != NULL)
				refData->PrepareRef(false);
			return refData;
		}
	}
	return NULL;
}

bool CReferenceDataObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& varMetaVal)
{
	return false;
}

bool CReferenceDataObject::GetValueByMetaID(const meta_identifier_t& id, CValue& pvarMetaVal) const
{
	if (m_metaObject->IsDataReference(id)) {
		if (!CReferenceDataObject::IsEmpty()) {
			pvarMetaVal = CReferenceDataObject::Create(m_metaObject, m_objGuid);
			return true;
		}
		pvarMetaVal = CReferenceDataObject::Create(m_metaObject);
		return true;
	}
	auto foundedIt = m_objectValues.find(id);
	wxASSERT(foundedIt != m_objectValues.end());
	if (foundedIt != m_objectValues.end()) {
		pvarMetaVal = foundedIt->second;
		return true;
	}
	return false;
}

void CReferenceDataObject::FillFromModel(const valueArray_t& arr)
{
	m_objectValues.clear();

	//attrbutes can refValue 
	for (auto attribute : m_metaObject->GetGenericAttributes()) {

		if (m_metaObject->IsDataReference(attribute->GetMetaID()))
			continue;

		try {
			m_objectValues.insert_or_assign(attribute->GetMetaID(), arr.at(attribute->GetMetaID()));
		}
		catch (std::out_of_range&) {
			m_objectValues.insert_or_assign(attribute->GetMetaID(), attribute->CreateValue());
		}
	}

	// table is collection values 
	for (auto table : m_metaObject->GetObjectTables()) {
		m_objectValues.insert_or_assign(table->GetMetaID(), new CTabularSectionDataObjectRef(this, table, true));
	}

	m_foundedRef = true; m_newObject = false;
}

void CReferenceDataObject::ShowValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		IRecordDataObject* objValue = NULL;
		if (metaObject != NULL && m_objGuid.isValid())
			objValue = metaObject->CreateObjectValue(m_objGuid);
		else
			objValue = metaObject->CreateObjectValue();
		if (objValue != NULL)
			objValue->ShowFormValue();
	}
}

IRecordDataObjectRef* CReferenceDataObject::GetObject() const
{
	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {
		if (m_newObject)
			return metaObject->CreateObjectValue();
		return metaObject->CreateObjectValue(m_objGuid);
	}
	return NULL;
}

#include "core/metadata/singleClass.h"

CLASS_ID CReferenceDataObject::GetTypeClass() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enReference);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeClass();
}

wxString CReferenceDataObject::GetString() const
{
	wxASSERT(m_metaObject);

	if (m_newObject)
		return wxEmptyString;
	else if (!m_foundedRef)
		return wxString::Format("% <%i:%s>", _("not found"), m_metaObject->GetMetaID(), m_objGuid.str());

	return m_metaObject->GetDescription(this);
}

wxString CReferenceDataObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enReference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

//****************************************************************************
//*                              Support methods                             *
//****************************************************************************

enum Func {
	enIsEmpty = 0,
	enGetMetadata,
	enGetObject,
	enGetGuid
};

void CReferenceDataObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	IMetaObjectRecordDataMutableRef* metaObject = NULL;
	if (m_metaObject->ConvertToValue(metaObject)) {

		m_methodHelper->AppendFunc("isEmpty", "IsEmpty()");
		m_methodHelper->AppendFunc("getMetadata", "getMetadata()");
		m_methodHelper->AppendFunc("getObject", "getObject()");
		m_methodHelper->AppendFunc("getGuid", "getGuid()");

		//fill custom attributes 
		for (auto attributes : metaObject->GetGenericAttributes()) {
			if (attributes->IsDeleted())
				continue;
			m_methodHelper->AppendProp(
				attributes->GetName(),
				true,
				false,
				attributes->GetMetaID(),
				eProperty
			);
		}

		//fill custom tables 
		for (auto table : metaObject->GetObjectTables()) {
			if (table->IsDeleted())
				continue;
			m_methodHelper->AppendProp(
				table->GetName(),
				true,
				false,
				table->GetMetaID(),
				eTable
			);
		}
	}
	else {
		m_methodHelper->AppendFunc("isEmpty", "IsEmpty()");
		m_methodHelper->AppendFunc("getMetadata", "getMetadata()");
	}
}

bool CReferenceDataObject::SetPropVal(const long lPropNum, const CValue& value)
{
	return false;
}

bool CReferenceDataObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	CReferenceDataObject::PrepareRef();
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (!m_metaObject->IsDataReference(id)) {
		if (lPropAlias == eTable && GetValueByMetaID(id, pvarPropVal)) {
			CTabularSectionDataObjectRef* tabularSection = NULL;
			if (pvarPropVal.ConvertToValue(tabularSection)) {
				if (tabularSection->IsReadAfter()) {
					if (!tabularSection->LoadData(m_objGuid, false))
						pvarPropVal.Reset();
					return !pvarPropVal.IsEmpty();
				}
				return true;
			}
			pvarPropVal.Reset();
			return false;
		}
		return GetValueByMetaID(id, pvarPropVal);
	}

	if (!CReferenceDataObject::IsEmpty()) {
		pvarPropVal = CReferenceDataObject::Create(m_metaObject, m_objGuid);
		return true;
	}

	pvarPropVal = CReferenceDataObject::Create(m_metaObject);
	return true;
}

bool CReferenceDataObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enIsEmpty:
		pvarRetValue = IsEmptyRef();
		return true;
	case enGetMetadata:
		pvarRetValue = GetMetadata();
		return true;
	case enGetObject:
		pvarRetValue = GetObject();
		return true;
	case enGetGuid:
		pvarRetValue = new CValueGuid(m_objGuid);
		return true;
	}

	return false;
}