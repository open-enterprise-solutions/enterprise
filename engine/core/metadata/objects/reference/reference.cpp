////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "metadata/metadata.h"
#include "metadata/objects/baseObject.h"
#include "metadata/objects/tabularSection/tabularSection.h"
#include "databaseLayer/databaseLayer.h"
#include "compiler/methods.h"
#include "utils/stringUtils.h"

wxIMPLEMENT_DYNAMIC_CLASS(CValueReference, CValue);

//**********************************************************************************************
//*                                     reference                                              *        
//**********************************************************************************************

void CValueReference::PrepareReference()
{
	wxASSERT(m_metaObject != NULL);
	wxASSERT(m_reference_impl == NULL);
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
	if (IsEmpty()) {
		//attrbutes can refValue 
		for (auto attribute : m_metaObject->GetObjectAttributes()) {
			if (eValueTypes::TYPE_BOOLEAN == attribute->GetTypeObject() ||
				eValueTypes::TYPE_NUMBER == attribute->GetTypeObject() ||
				eValueTypes::TYPE_DATE == attribute->GetTypeObject() ||
				eValueTypes::TYPE_STRING == attribute->GetTypeObject()) {
				m_aObjectValues[attribute->GetMetaID()] = eValueTypes(attribute->GetTypeObject());
			}
			else {
				m_aObjectValues[attribute->GetMetaID()] = new CValueReference(
					m_metaObject->GetMetadata(), attribute->GetTypeObject()
				);
			}
		}
		// table is collection values 
		for (auto table : m_metaObject->GetObjectTables()) {
			m_aObjectValues[table->GetMetaID()] = new CValueTabularRefSection(this, table);
		}
	}
	else {
		m_bNewObject = false;
		if (ReadReferenceInDB()) {
			m_bFoundedRef = true;
		}
	}
}

CValueReference::CValueReference(IMetadata *metaData, meta_identifier_t id, const Guid &objGuid) : CValue(eValueTypes::TYPE_VALUE, true), IObjectValueInfo(objGuid, !objGuid.isValid()),
m_metaObject(wxStaticCast(metaData->GetMetaObject(id), IMetaObjectRefValue)), m_methods(new CMethods()), m_reference_impl(NULL), m_bFoundedRef(false)
{
	PrepareReference();
}

CValueReference::CValueReference(IMetaObjectRefValue *metaObject, const Guid &objGuid) : CValue(eValueTypes::TYPE_VALUE, true), IObjectValueInfo(objGuid, !objGuid.isValid()),
m_metaObject(metaObject), m_methods(new CMethods()), m_reference_impl(NULL), m_bFoundedRef(false)
{
	PrepareReference();
}

CValueReference::~CValueReference()
{
	wxDELETE(m_methods);
	wxDELETE(m_reference_impl);
}

CValue CValueReference::CreateFromPtr(IMetadata *metaData,  void *ptr)
{
	reference_t *reference = static_cast<reference_t *>(ptr);
	if (reference) {
		return new CValueReference(metaData, reference->m_id, reference->m_guid);
	}
	return CValue();
}

void CValueReference::SetBinaryData(int nPosition, PreparedStatement *preparedStatment)
{
	if (m_reference_impl) {
		preparedStatment->SetParamBlob(nPosition, m_reference_impl, sizeof(reference_t));
	}
}

void CValueReference::GetBinaryData(int nPosition, DatabaseResultSet *databaseResultSet)
{
	wxMemoryBuffer memoryBuffer;
	databaseResultSet->GetResultBlob(nPosition, memoryBuffer);

	if (!memoryBuffer.IsEmpty()) {
		reference_t *tempReference = static_cast<reference_t *>(memoryBuffer.GetData());

		m_metaObject = dynamic_cast<IMetaObjectRefValue *>(metadata->GetMetaObject(tempReference->m_id));
		m_objGuid = tempReference->m_guid;

		if (m_metaObject) {
			m_reference_impl = new reference_t(tempReference->m_id, tempReference->m_guid);
		}
	}
}

void CValueReference::SetValueByMetaID(meta_identifier_t id, const CValue &cVal)
{
}

CValue CValueReference::GetValueByMetaID(meta_identifier_t id) const
{
	auto foundedIt = m_aObjectValues.find(id);
	wxASSERT(foundedIt != m_aObjectValues.end());
	if (foundedIt != m_aObjectValues.end()) {
		return foundedIt->second;
	}

	return CValue();
}

IMetaObjectValue *CValueReference::GetMetaObject() const
{
	return m_metaObject;
}

void CValueReference::ShowValue()
{
	IDataObjectValue *objValue = NULL;

	if (m_metaObject && m_objGuid.isValid()) {
		objValue = m_metaObject->CreateObjectRefValue(m_objGuid);
	}
	else {
		objValue = m_metaObject->CreateObjectRefValue();
	}

	if (objValue) {
		objValue->ShowFormValue();
	}
}

CValue CValueReference::IsEmptyRef()
{
	return IsEmpty();
}

CValue CValueReference::GetMetadata()
{
	wxASSERT(m_metaObject);
	return m_metaObject;
}

CValue CValueReference::GetObject()
{
	if (m_bNewObject) {
		return m_metaObject->CreateObjectRefValue();
	}

	return m_metaObject->CreateObjectRefValue(m_objGuid);
}

#include "metadata/singleMetaTypes.h"

CLASS_ID CValueReference::GetTypeID() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enReference);
	wxASSERT(clsFactory);
	return clsFactory->GetTypeID();
}


wxString CValueReference::GetString() const
{
	if (m_bNewObject) {
		return wxEmptyString;
	}

	wxASSERT(m_metaObject);

	if (!m_bFoundedRef) {
		wxString nullRef;
		nullRef << _("not found <") << m_metaObject->GetMetaID() << wxT(":") + m_objGuid.str() << wxT(">");
		return nullRef;
	}

	return m_metaObject->GetDescription(this);
}

wxString CValueReference::GetTypeString() const
{
	IMetaTypeObjectValueSingle *clsFactory =
		m_metaObject->GetTypeObject(eMetaObjectType::enReference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}