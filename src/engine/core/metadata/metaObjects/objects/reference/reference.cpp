////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "metadata/metadata.h"
#include "metadata/metaObjects/objects/object.h"
#include "metadata/metaObjects/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/methods.h"
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
				m_objectValues.insert_or_assign(
					attribute->GetMetaID(),
					attribute->CreateValue()
				);
			}
			else {
				m_objectValues.insert_or_assign(
					attribute->GetMetaID(), CValue()
				);
			}
		}

		// table is collection values 
		for (auto table : m_metaObject->GetObjectTables()) {
			m_objectValues[table->GetMetaID()] = new CTabularSectionDataObjectRef(this, table);
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
m_metaObject(metaObject), m_methods(new CMethods()), m_initializedRef(false), m_reference_impl(NULL), m_foundedRef(false)
{
	m_reference_impl = new reference_t(
		m_metaObject->GetMetaID(), m_objGuid
	);
}

CReferenceDataObject::~CReferenceDataObject()
{
	wxDELETE(m_methods);
	wxDELETE(m_reference_impl);
}

CReferenceDataObject* CReferenceDataObject::Create(IMetadata* metaData, const meta_identifier_t& id, const Guid& objGuid)
{
	IMetaObjectRecordDataRef* metaObject = NULL; 
	if (metaData->GetMetaObject(metaObject, id)) {
		CReferenceDataObject* refData = new CReferenceDataObject(
			metaObject, objGuid
		);
		refData->PrepareRef();
		return refData;
	}
	return NULL;
}

CReferenceDataObject* CReferenceDataObject::Create(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid)
{
	CReferenceDataObject* refData = new CReferenceDataObject(
		metaObject, objGuid
	);
	refData->PrepareRef();
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
			if (refData != NULL) {
				refData->PrepareRef(false);
			}
			return refData;
		}
	}
	return NULL;
}

void CReferenceDataObject::SetValueByMetaID(const meta_identifier_t& id, const CValue& cVal)
{
}

CValue CReferenceDataObject::GetValueByMetaID(const meta_identifier_t& id) const
{
	if (m_metaObject->IsDataReference(id)) {
		if (!CReferenceDataObject::IsEmpty())
			return CReferenceDataObject::Create(m_metaObject, m_objGuid);
		return CReferenceDataObject::Create(m_metaObject);
	}
	auto foundedIt = m_objectValues.find(id);
	wxASSERT(foundedIt != m_objectValues.end());
	if (foundedIt != m_objectValues.end())
		return foundedIt->second;
	return CValue();
}

IMetaObjectRecordData* CReferenceDataObject::GetMetaObject() const
{
	return m_metaObject;
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
		if (m_newObject) {
			return metaObject->CreateObjectValue();
		}
		return metaObject->CreateObjectValue(m_objGuid);
	}
	return NULL; 
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CReferenceDataObject::GetClassType() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enReference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CReferenceDataObject::GetString() const
{
	if (m_newObject) {
		return wxEmptyString;
	}

	wxASSERT(m_metaObject);

	if (!m_foundedRef) {
		wxString nullRef;
		nullRef << _("not found") + wxT("<") << m_metaObject->GetMetaID() << wxT(":") + m_objGuid.str() << wxT(">");
		return nullRef;
	}

	return m_metaObject->GetDescription(this);
}

wxString CReferenceDataObject::GetTypeString() const
{
	IMetaTypeObjectValueSingle* clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enReference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}