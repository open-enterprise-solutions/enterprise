////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "metadata/metadata.h"
#include "metadata/metaObjects/objects/baseObject.h"
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
				m_aObjectValues.insert_or_assign(
					attribute->GetMetaID(),
					attribute->CreateValue()
				);
			}
			else {
				m_aObjectValues.insert_or_assign(
					attribute->GetMetaID(), CValue()
				);
			}
		}

		// table is collection values 
		for (auto table : m_metaObject->GetObjectTables()) {
			m_aObjectValues[table->GetMetaID()] = new CTabularSectionDataObjectRef(this, table);
		}
	}
	else if (CReferenceDataObject::ReadData(createData)) {
		m_foundedRef = true; m_newObject = false;
	}

	if (createData) {
		m_initializedRef = true;
	}
}

CReferenceDataObject::CReferenceDataObject(IMetadata* metaData, const meta_identifier_t& id, const Guid& objGuid) : CValue(eValueTypes::TYPE_VALUE, true), IObjectValueInfo(objGuid, !objGuid.isValid()),
m_metaObject(wxStaticCast(metaData->GetMetaObject(id), IMetaObjectRecordDataRef)), m_methods(new CMethods()), m_initializedRef(false), m_reference_impl(NULL), m_foundedRef(false)
{
	m_reference_impl = new reference_t(
		m_metaObject->GetMetaID(),
		m_objGuid
	);
}

CReferenceDataObject::CReferenceDataObject(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid) : CValue(eValueTypes::TYPE_VALUE, true), IObjectValueInfo(objGuid, !objGuid.isValid()),
m_metaObject(metaObject), m_methods(new CMethods()), m_initializedRef(false), m_reference_impl(NULL), m_foundedRef(false)
{
	m_reference_impl = new reference_t(
		m_metaObject->GetMetaID(),
		m_objGuid
	);
}

CReferenceDataObject::~CReferenceDataObject()
{
	wxDELETE(m_methods);
	wxDELETE(m_reference_impl);
}

CReferenceDataObject* CReferenceDataObject::Create(IMetadata* metaData, const meta_identifier_t& id, const Guid& objGuid)
{
	CReferenceDataObject* refData = new CReferenceDataObject(
		metaData,
		id,
		objGuid
	);

	refData->PrepareRef();
	return refData;
}

CReferenceDataObject* CReferenceDataObject::Create(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid)
{
	CReferenceDataObject* refData = new CReferenceDataObject(
		metaObject,
		objGuid
	);

	refData->PrepareRef();
	return refData;
}

CReferenceDataObject* CReferenceDataObject::Create(IMetadata* metaData, void* ptr)
{
	reference_t* reference = static_cast<reference_t*>(ptr);

	if (reference != NULL) {

		return new CReferenceDataObject(
			metaData,
			reference->m_id,
			reference->m_guid
		);
	}

	return NULL;
}

CReferenceDataObject* CReferenceDataObject::CreateFromPtr(IMetadata* metaData, void* ptr)
{
	reference_t* reference = static_cast<reference_t*>(ptr);

	if (reference != NULL) {

		CReferenceDataObject* refData = new CReferenceDataObject(
			metaData,
			reference->m_id,
			reference->m_guid
		);

		if (refData != NULL) {
			refData->PrepareRef(false);
		}

		return refData;
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

	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		return foundedIt->second;
	}

	return CValue();
}

IMetaObjectRecordData* CReferenceDataObject::GetMetaObject() const
{
	return m_metaObject;
}

void CReferenceDataObject::ShowValue()
{
	IRecordDataObject* objValue = NULL;

	if (m_metaObject != NULL && m_objGuid.isValid()) {
		objValue = m_metaObject->CreateObjectRefValue(m_objGuid);
	}
	else {
		objValue = m_metaObject->CreateObjectRefValue();
	}

	if (objValue != NULL) {
		objValue->ShowFormValue();
	}
}

IRecordDataObjectRef* CReferenceDataObject::GetObject() const
{
	if (m_newObject) {
		return m_metaObject->CreateObjectRefValue();
	}

	return m_metaObject->CreateObjectRefValue(m_objGuid);
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