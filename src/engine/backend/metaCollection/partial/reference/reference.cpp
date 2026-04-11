////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/databaseLayer/databaseLayer.h"

wxIMPLEMENT_DYNAMIC_CLASS(ibValueReferenceDataObject, ibValue);

//**********************************************************************************************
//*                                     reference                                              *        
//**********************************************************************************************
//static std::vector <ibValueReferenceDataObject*> gs_references;
//**********************************************************************************************

void ibValueReferenceDataObject::PrepareRef(bool createData)
{
	wxASSERT(m_metaObject != nullptr);

	if (m_initializedRef)
		return;

	if (ibValueReferenceDataObject::IsEmpty()) {
		//attrbutes can refValue 
		for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!m_metaObject->IsDataReference(object->GetMetaID())) {
				m_listObjectValue.insert_or_assign(object->GetMetaID(), object->CreateValue());
			}
		}
		// table is collection values 
		for (const auto object : m_metaObject->GetTableArrayObject()) {
			if (object->IsDeleted())
				continue;
			m_listObjectValue.insert_or_assign(object->GetMetaID(),
				ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(this, object));
		}
	}
	else if (ibValueReferenceDataObject::ReadData(createData)) {
		m_foundedRef = true; m_newObject = false;
	}

	if (createData) {
		m_initializedRef = true;
	}

	PrepareNames();
}

ibValueReferenceDataObject::ibValueReferenceDataObject(ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid) : ibValue(ibValueTypes::TYPE_VALUE, true), ibValueDataObject(objGuid, !objGuid.isValid()),
m_metaObject(metaObject), m_methodHelper(new ibValueMethodHelper()), m_initializedRef(false), m_reference_impl(nullptr), m_foundedRef(false)
{
	m_reference_impl = new ibReference(m_metaObject->GetMetaID(), m_objGuid);
	//gs_references.emplace_back(this);
}

ibValueReferenceDataObject::~ibValueReferenceDataObject()
{
	wxDELETE(m_reference_impl);
	//gs_references.erase(
	//	std::remove_if(gs_references.begin(), gs_references.end(),
	//		[this](ibValueReferenceDataObject* ref) { return ref == this;}), gs_references.end()
	//);
	wxDELETE(m_methodHelper);
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(ibMetaData* metaData, const ibMetaID& id, const ibGuid& objGuid)
{
	ibValueMetaObjectRecordDataRef* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(id);
	if (metaObject != nullptr) {
		//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, objGuid](ibValueReferenceDataObject* ref) {
		//	return metaObject == ref->GetMetaObject() && objGuid == ref->GetGuid(); }
		//);
		//if (it != gs_references.end())
		//	return *it;
		ibValueReferenceDataObject* refData = new ibValueReferenceDataObject(metaObject, objGuid);
		if (refData != nullptr)
			refData->PrepareRef(true);
		return refData;
	}
	return nullptr;
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid)
{
	//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, objGuid](ibValueReferenceDataObject* ref) {
	//	return metaObject == ref->GetMetaObject() && objGuid == ref->GetGuid(); }
	//);
	//if (it != gs_references.end())
	//	return *it;
	ibValueReferenceDataObject* refData = new ibValueReferenceDataObject(metaObject, objGuid);
	if (refData != nullptr)
		refData->PrepareRef(true);
	return refData;
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(ibMetaData* metaData, void* ptr)
{
	ibReference* reference = static_cast<ibReference*>(ptr);
	if (reference != nullptr) {
		ibValueMetaObjectRecordDataRef* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(reference->m_id);
		if (metaObject != nullptr) {
			//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, reference](ibValueReferenceDataObject* ref) {
			//	return metaObject == ref->GetMetaObject() && ref->GetGuid() == reference->m_guid; }
			//);
			//if (it != gs_references.end())
			//	return *it;
			return new ibValueReferenceDataObject(metaObject, reference->m_guid);
		}
	}
	return nullptr;
}

ibValueReferenceDataObject* ibValueReferenceDataObject::CreateFromPtr(ibMetaData* metaData, void* ptr)
{
	ibReference* reference = static_cast<ibReference*>(ptr);
	if (reference != nullptr) {
		ibValueMetaObjectRecordDataRef* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(reference->m_id);
		if (metaObject != nullptr) {
			//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, reference](ibValueReferenceDataObject* ref) {
			//	return metaObject == ref->GetMetaObject() && ref->GetGuid() == reference->m_guid; }
			//);
			//if (it != gs_references.end())
			//	return *it;
			ibValueReferenceDataObject* refData = new ibValueReferenceDataObject(metaObject, reference->m_guid);
			if (refData != nullptr)
				refData->PrepareRef(false);
			return refData;
		}
	}
	return nullptr;
}

ibValueReferenceDataObject* ibValueReferenceDataObject::CreateFromResultSet(ibDatabaseResultSet* rs, ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& refGuid)
{
	//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, refGuid](ibValueReferenceDataObject* ref) {
	//	return metaObject == ref->GetMetaObject() && refGuid == ref->GetGuid(); }
	//);
	//if (it != gs_references.end())
	//	return *it;

	ibValueReferenceDataObject* refData = new ibValueReferenceDataObject(metaObject, refGuid);

	//load attributes 
	for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (metaObject->IsDataReference(object->GetMetaID()))
			continue;
		ibValueMetaObjectAttributeBase::GetValueAttribute(
			object,
			refData->m_listObjectValue[object->GetMetaID()],
			rs,
			false
		);
	}

	// table is collection values 
	for (const auto object : metaObject->GetTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		refData->m_listObjectValue.insert_or_assign(
			object->GetMetaID(),
			ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(refData, object, true)
		);
	}

	refData->m_foundedRef = true;
	return refData;
}

bool ibValueReferenceDataObject::SetValueByMetaID(const ibMetaID& id, const ibValue& varMetaVal)
{
	return false;
}

bool ibValueReferenceDataObject::GetValueByMetaID(const ibMetaID& id, ibValue& pvarMetaVal) const
{
	if (m_metaObject->IsDataReference(id)) {
		if (!ibValueReferenceDataObject::IsEmpty()) {
			pvarMetaVal = ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
			return true;
		}
		pvarMetaVal = ibValueReferenceDataObject::Create(m_metaObject);
		return true;
	}
	auto it = m_listObjectValue.find(id);
	//wxASSERT(it != m_listObjectValue.end());
	if (it != m_listObjectValue.end()) {
		pvarMetaVal = it->second;
		return true;
	}
	return false;
}

void ibValueReferenceDataObject::ShowValue()
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		ibValueRecordDataObject* objValue = nullptr;
		if (metaObject != nullptr && m_objGuid.isValid())
			objValue = metaObject->CreateObjectValue(m_objGuid);
		else
			objValue = metaObject->CreateObjectValue();
		if (objValue != nullptr)
			objValue->ShowFormValue();
	}
}

ibValueRecordDataObjectRef* ibValueReferenceDataObject::GetObject() const
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		if (m_newObject)
			return metaObject->CreateObjectValue();
		return metaObject->CreateObjectValue(m_objGuid);
	}
	return nullptr;
}

#include "backend/objCtor.h"

ibClassID ibValueReferenceDataObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueReferenceDataObject::GetString() const
{
	if (m_newObject)
		return wxEmptyString;
	else if (!m_foundedRef)
		return wxString::Format(wxT("%s <%i:%s>"), _("Not found"), m_metaObject->GetMetaID(), m_objGuid.str());

	wxASSERT(m_metaObject);
	return m_metaObject->GetDataPresentation(this);
}

wxString ibValueReferenceDataObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		m_metaObject->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference);
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

void ibValueReferenceDataObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		m_methodHelper->AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
		m_methodHelper->AppendFunc(wxT("GetObject"), wxT("GetObject()"));
		m_methodHelper->AppendFunc(wxT("GetGuid"), wxT("GetGuid()"));

		wxString objectName;

		//fill custom attributes 
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			m_methodHelper->AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eProperty
			);
		}

		//fill custom tables 
		for (const auto object : metaObject->GetTableArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			m_methodHelper->AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eTable
			);
		}
	}
	else {
		m_methodHelper->AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		m_methodHelper->AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	}
}

bool ibValueReferenceDataObject::SetPropVal(const long lPropNum, const ibValue& value)
{
	return false;
}

bool ibValueReferenceDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_methodHelper->GetPropAlias(lPropNum);
	ibValueReferenceDataObject::PrepareRef();
	const ibMetaID& id = m_methodHelper->GetPropData(lPropNum);
	if (!m_metaObject->IsDataReference(id)) {
		if (lPropAlias == eTable && !GetValueByMetaID(id, pvarPropVal)) {
			m_listObjectValue.insert_or_assign(id,
				ibValue::CreateAndPrepareValueRef<ibValueTabularSectionDataObjectRef>(this, m_metaObject->FindTableObjectByFilter(id), !m_newObject)
			);
		}
		if (lPropAlias == eTable && GetValueByMetaID(id, pvarPropVal)) {
			ibValueTabularSectionDataObjectRef* tabularSection = nullptr;
			if (pvarPropVal.ConvertToValue(tabularSection)) {
				if (tabularSection->IsReadAfter()) {
					if (!tabularSection->LoadData(m_objGuid, true)) {
						pvarPropVal.Reset();
						return false;
					}
				}
				return true;
			}
			pvarPropVal.Reset();
			return false;
		}
		return GetValueByMetaID(id, pvarPropVal);
	}

	if (!ibValueReferenceDataObject::IsEmpty()) {
		pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
		return true;
	}

	pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject);
	return true;
}

bool ibValueReferenceDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enIsEmpty:
		pvarRetValue = IsEmptyRef();
		return true;
	case enGetMetadata:
		pvarRetValue = GetMetaObject();
		return true;
	case enGetObject:
		pvarRetValue = GetObject();
		return true;
	case enGetGuid:
		pvarRetValue = ibValue::CreateAndPrepareValueRef<ibValueGuid>(m_objGuid);
		return true;
	}

	return false;
}