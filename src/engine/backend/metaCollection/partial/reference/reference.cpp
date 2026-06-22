////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : reference object
////////////////////////////////////////////////////////////////////////////

#include "reference.h"
#include "backend/metaData.h"
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/tabularSection/tabularSection.h"

#include <vector>
#include <algorithm>
#include <utility>

// Re-entrancy guard for the eager reference read. A self / cyclic reference (a document
// field pointing at the same record, or A -> B -> A) makes ReadData recurse: it reads a
// reference field, whose eager PrepareRef calls ReadData again, forever -> stack overflow.
// While a ref identity (type + guid) is already being read up the stack, the nested re-read
// is skipped (the field keeps its key, loads lazily on demand). A self-reference is a LEGAL
// config, so this must terminate rather than crash.
//
// A thread-local stack of identities (each worker reads on its own call stack): push on
// entry, pop on exit (RAII — correct across the FB exceptions ReadData can throw). It costs
// no allocation once the vector's capacity settles, and a linear scan over a depth that is
// tiny in practice — negligible next to the DB query the read itself issues.
namespace {
	thread_local std::vector<std::pair<ibMetaID, ibGuid>> g_refReadStack;

	struct ibRefReadGuard {
		bool m_cycle = false;
		ibRefReadGuard(const ibMetaID& metaId, const ibGuid& guid) {
			const std::pair<ibMetaID, ibGuid> key{ metaId, guid };
			m_cycle = std::find(g_refReadStack.begin(), g_refReadStack.end(), key) != g_refReadStack.end();
			g_refReadStack.push_back(key);
		}
		~ibRefReadGuard() { g_refReadStack.pop_back(); }
		bool Cycle() const { return m_cycle; }   // true == this identity is already being read up the stack
	};
}


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
				new ibValueTabularSectionDataObjectRef(this, object));
		}
	}
	else {
		// Break self / cyclic references: read this identity only if it is not already
		// being read up the stack (otherwise leave it lazy — no infinite recursion).
		ibRefReadGuard guard(m_metaObject->GetMetaID(), m_objGuid);
		if (!guard.Cycle() && ibValueReferenceDataObject::ReadData(createData)) {
			m_foundedRef = true; m_newObject = false;
		}
	}

	if (createData) {
		m_initializedRef = true;
	}
	// Name surface is lazy (FillMembers built on first GetPMethods).
}

ibValueReferenceDataObject::ibValueReferenceDataObject(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid) : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), ibValueDataObject(objGuid, !objGuid.isValid()),
m_metaObject(metaObject), m_initializedRef(false), m_reference_impl(nullptr), m_foundedRef(false)
{
	m_members.Bind(this, &ibValueReferenceDataObject::FillMembers);
	m_reference_impl = new ibReference(m_metaObject->GetMetaID(), m_objGuid);
	//gs_references.emplace_back(this);
}

// Identity key = the DB reference key (metaID + guid), aligning runtime grouping / join / dedup
// over a reference with the database. See the declaration in reference.h.
wxString ibValueReferenceDataObject::GetHashKey() const
{
	return m_metaObject != nullptr
		? wxString::Format(wxT("%i:%s"), m_metaObject->GetMetaID(), wxString(GetGuid()))
		: wxString(GetGuid());
}

ibValueReferenceDataObject::~ibValueReferenceDataObject()
{
	wxDELETE(m_reference_impl);
	//gs_references.erase(
	//	std::remove_if(gs_references.begin(), gs_references.end(),
	//		[this](ibValueReferenceDataObject* ref) { return ref == this;}), gs_references.end()
	//);
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibMetaData* metaData, const ibMetaID& id, const ibGuid& objGuid)
{
	const ibValueMetaObjectRecordDataRef* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(id);
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

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid)
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

ibValueReferenceDataObject* ibValueReferenceDataObject::CreateRaw(const ibValueMetaObjectRecordDataRef* metaObject, const ibGuid& objGuid)
{
	// Construct without PrepareRef (deferred to first use): the value-ctor
	// registry path can't prepare eagerly without recursing. See header note.
	return new ibValueReferenceDataObject(metaObject, objGuid);
}

ibValueReferenceDataObject* ibValueReferenceDataObject::Create(const ibMetaData* metaData, void* ptr)
{
	ibReference* reference = static_cast<ibReference*>(ptr);
	if (reference != nullptr) {
		const ibValueMetaObjectRecordDataRef* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(reference->m_id);
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

ibValueReferenceDataObject* ibValueReferenceDataObject::CreateFromPtr(const ibMetaData* metaData, void* ptr)
{
	ibReference* reference = static_cast<ibReference*>(ptr);
	if (reference != nullptr) {
		const ibValueMetaObjectRecordDataRef* metaObject = metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataRef>(reference->m_id);
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

void ibValueReferenceDataObject::FillMembers(ibMemberTable& helper) const
{
	ibValueMetaObjectRecordDataMutableRef* metaObject = nullptr;
	if (m_metaObject->ConvertToValue(metaObject)) {

		helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
		helper.AppendFunc(wxT("GetObject"), wxT("GetObject()"));
		helper.AppendFunc(wxT("GetGuid"), wxT("GetGuid()"));

		wxString objectName;

		//fill custom attributes
		for (const auto object : metaObject->GetGenericAttributeArrayObject()) {
			if (object->IsDeleted())
				continue;
			if (!object->GetObjectNameAsString(objectName))
				continue;
			helper.AppendProp(
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
			helper.AppendProp(
				objectName,
				true,
				false,
				object->GetMetaID(),
				eTable
			);
		}
	}
	else {
		helper.AppendFunc(wxT("IsEmpty"), wxT("IsEmpty()"));
		helper.AppendFunc(wxT("GetMetadata"), wxT("GetMetadata()"));
	}
}

bool ibValueReferenceDataObject::SetPropVal(const long lPropNum, const ibValue& value)
{
	return false;
}

bool ibValueReferenceDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const long lPropAlias = m_members.GetPropAlias(lPropNum);
	ibValueReferenceDataObject::PrepareRef();
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (!m_metaObject->IsDataReference(id)) {
		if (lPropAlias == eTable && !GetValueByMetaID(id, pvarPropVal)) {
			m_listObjectValue.insert_or_assign(id,
				new ibValueTabularSectionDataObjectRef(this, m_metaObject->FindTableObjectByFilter(id), !m_newObject)
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
		pvarRetValue = new ibValueGuid(m_objGuid);
		return true;
	}

	return false;
}