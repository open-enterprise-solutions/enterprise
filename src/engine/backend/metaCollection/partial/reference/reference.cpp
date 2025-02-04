////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/object.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"
#include "backend/databaseLayer/databaseLayer.h"

wxIMPLEMENT_DYNAMIC_CLASS(CReferenceDataObject, CValue);

//**********************************************************************************************
//*                                     reference                                              *        
//**********************************************************************************************
//static std::vector <CReferenceDataObject*> gs_references;
//**********************************************************************************************

void CReferenceDataObject::PrepareRef(bool createData)
{
	wxASSERT(m_metaObject != nullptr);

	if (m_initializedRef)
		return;

	if (CReferenceDataObject::IsEmpty()) {
		//attrbutes can refValue 
		for (auto& obj : m_metaObject->GetGenericAttributes()) {
			if (obj->IsDeleted()) 
				continue;
			if (!m_metaObject->IsDataReference(obj->GetMetaID())) {
				m_objectValues.insert_or_assign(obj->GetMetaID(), obj->CreateValue());
			}
		}
		// table is collection values 
		for (auto& obj : m_metaObject->GetObjectTables()) {
			if (obj->IsDeleted())
				continue;
			m_objectValues.insert_or_assign(obj->GetMetaID(),
				m_metaObject->GetMetaData()->CreateObjectValue<CTabularSectionDataObjectRef>(this, obj));
		}
	}
	else if (CReferenceDataObject::ReadData(createData)) {
		m_foundedRef = true; m_newObject = false;
	}

	if (createData) {
		m_initializedRef = true;
	}

	PrepareNames();
}

CReferenceDataObject::CReferenceDataObject(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid) : CValue(eValueTypes::TYPE_VALUE, true), IObjectValueInfo(objGuid, !objGuid.isValid()),
m_metaObject(metaObject), m_methodHelper(new CMethodHelper()), m_initializedRef(false), m_reference_impl(nullptr), m_foundedRef(false)
{
	m_reference_impl = new reference_t(m_metaObject->GetMetaID(), m_objGuid);
	//gs_references.emplace_back(this);
}

CReferenceDataObject::~CReferenceDataObject()
{
	wxDELETE(m_reference_impl);
	//gs_references.erase(
	//	std::remove_if(gs_references.begin(), gs_references.end(),
	//		[this](CReferenceDataObject* ref) { return ref == this;}), gs_references.end()
	//);
	wxDELETE(m_methodHelper);
}

CReferenceDataObject* CReferenceDataObject::Create(IMetaData* metaData, const meta_identifier_t& id, const Guid& objGuid)
{
	IMetaObjectRecordDataRef* metaObject = nullptr;
	if (metaData->GetMetaObject(metaObject, id)) {
		//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, objGuid](CReferenceDataObject* ref) {
		//	return metaObject == ref->GetMetaObject() && objGuid == ref->GetGuid(); }
		//);
		//if (it != gs_references.end())
		//	return *it;
		CReferenceDataObject* refData = new CReferenceDataObject(metaObject, objGuid);
		if (refData != nullptr)
			refData->PrepareRef(true);
		return refData;
	}
	return nullptr;
}

CReferenceDataObject* CReferenceDataObject::Create(IMetaObjectRecordDataRef* metaObject, const Guid& objGuid)
{
	//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, objGuid](CReferenceDataObject* ref) {
	//	return metaObject == ref->GetMetaObject() && objGuid == ref->GetGuid(); }
	//);
	//if (it != gs_references.end())
	//	return *it;
	CReferenceDataObject* refData = new CReferenceDataObject(metaObject, objGuid);
	if (refData != nullptr)
		refData->PrepareRef(true);
	return refData;
}

CReferenceDataObject* CReferenceDataObject::Create(IMetaData* metaData, void* ptr)
{
	reference_t* reference = static_cast<reference_t*>(ptr);
	if (reference != nullptr) {
		IMetaObjectRecordDataRef* metaObject = nullptr;
		if (metaData->GetMetaObject(metaObject, reference->m_id)) {
			//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, reference](CReferenceDataObject* ref) {
			//	return metaObject == ref->GetMetaObject() && ref->GetGuid() == reference->m_guid; }
			//);
			//if (it != gs_references.end())
			//	return *it;
			return new CReferenceDataObject(metaObject, reference->m_guid);
		}
	}
	return nullptr;
}

CReferenceDataObject* CReferenceDataObject::CreateFromPtr(IMetaData* metaData, void* ptr)
{
	reference_t* reference = static_cast<reference_t*>(ptr);
	if (reference != nullptr) {
		IMetaObjectRecordDataRef* metaObject = nullptr;
		if (metaData->GetMetaObject(metaObject, reference->m_id)) {
			//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, reference](CReferenceDataObject* ref) {
			//	return metaObject == ref->GetMetaObject() && ref->GetGuid() == reference->m_guid; }
			//);
			//if (it != gs_references.end())
			//	return *it;
			CReferenceDataObject* refData = new CReferenceDataObject(metaObject, reference->m_guid);
			if (refData != nullptr)
				refData->PrepareRef(false);
			return refData;
		}
	}
	return nullptr;
}

CReferenceDataObject* CReferenceDataObject::CreateFromResultSet(IDatabaseResultSet* rs, IMetaObjectRecordDataRef* metaObject, const Guid& refGuid)
{
	//auto& it = std::find_if(gs_references.begin(), gs_references.end(), [metaObject, refGuid](CReferenceDataObject* ref) {
	//	return metaObject == ref->GetMetaObject() && refGuid == ref->GetGuid(); }
	//);
	//if (it != gs_references.end())
	//	return *it;

	CReferenceDataObject* refData = new CReferenceDataObject(metaObject, refGuid);

	//load attributes 
	for (auto& obj : metaObject->GetGenericAttributes()) {		
		if (obj->IsDeleted())
			continue;	
		if (metaObject->IsDataReference(obj->GetMetaID()))
			continue;
		IMetaObjectAttribute::GetValueAttribute(
			obj,
			refData->m_objectValues[obj->GetMetaID()],
			rs,
			false
		);
	}

	// table is collection values 
	for (auto& obj : metaObject->GetObjectTables()) {
		if (obj->IsDeleted())
			continue;
		refData->m_objectValues.insert_or_assign(obj->GetMetaID(),
			metaObject->GetMetaData()->CreateObjectValue<CTabularSectionDataObjectRef>(refData, obj, true));
	}

	refData->m_foundedRef = true;
	return refData;
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
	auto& it = m_objectValues.find(id);
	wxASSERT(it != m_objectValues.end());
	if (it != m_objectValues.end()) {
		pvarMetaVal = it->second;
		return true;
	}
	return false;
}

void CReferenceDataObject::ShowValue()
{
	IMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		IRecordDataObject* objValue = nullptr;
		if (metaObject != nullptr && m_objGuid.isValid())
			objValue = metaObject->CreateObjectValue(m_objGuid);
		else
			objValue = metaObject->CreateObjectValue();
		if (objValue != nullptr)
			objValue->ShowFormValue();
	}
}

IRecordDataObjectRef* CReferenceDataObject::GetObject() const
{
	IMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {
		if (m_newObject)
			return metaObject->CreateObjectValue();
		return metaObject->CreateObjectValue(m_objGuid);
	}
	return nullptr;
}

#include "backend/objCtor.h"

class_identifier_t CReferenceDataObject::GetClassType() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Reference);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString CReferenceDataObject::GetString() const
{
	if (m_newObject)
		return wxEmptyString;
	else if (!m_foundedRef)
		return wxString::Format("%s <%i:%s>", _("not found"), m_metaObject->GetMetaID(), m_objGuid.str());

	wxASSERT(m_metaObject);
	return m_metaObject->GetDataPresentation(this);
}

wxString CReferenceDataObject::GetClassName() const
{
	IMetaValueTypeCtor* clsFactory =
		m_metaObject->GetTypeCtor(eCtorMetaType::eCtorMetaType_Reference);
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

	IMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		m_methodHelper->AppendFunc("isEmpty", "IsEmpty()");
		m_methodHelper->AppendFunc("getMetadata", "getMetadata()");
		m_methodHelper->AppendFunc("getObject", "getObject()");
		m_methodHelper->AppendFunc("getGuid", "getGuid()");

		//fill custom attributes 
		for (auto& obj : metaObject->GetGenericAttributes()) {
			if (obj->IsDeleted())
				continue;
			m_methodHelper->AppendProp(
				obj->GetName(),
				true,
				false,
				obj->GetMetaID(),
				eProperty
			);
		}

		//fill custom tables 
		for (auto& obj : metaObject->GetObjectTables()) {
			if (obj->IsDeleted())
				continue;
			m_methodHelper->AppendProp(
				obj->GetName(),
				true,
				false,
				obj->GetMetaID(),
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
		if (lPropAlias == eTable && !GetValueByMetaID(id, pvarPropVal)) {
			m_objectValues.insert_or_assign(id,
				GetMetaObjectRef()->GetMetaData()->CreateObjectValueRef<CTabularSectionDataObjectRef>(this, m_metaObject->FindTableById(id), !m_newObject)
			);
		}
		if (lPropAlias == eTable && GetValueByMetaID(id, pvarPropVal)) {
			CTabularSectionDataObjectRef* tabularSection = nullptr;
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
		pvarRetValue = GetMetaObject();
		return true;
	case enGetObject:
		pvarRetValue = GetObject();
		return true;
	case enGetGuid:
		pvarRetValue = CValue::CreateAndConvertObjectValueRef<CValueGuid>(m_objGuid);
		return true;
	}

	return false;
}