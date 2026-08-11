#include "objectSelector.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/appData.h"

ibValueSelectorDataObject::ibValueSelectorDataObject() : ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true)
{
}

ibValueSelectorDataObject::~ibValueSelectorDataObject()
{
}

bool ibValueSelectorDataObject::Next()
{
	// Universal cursor drive: designer mode never iterates; otherwise advance one row
	// past the anchor through the shared keyset step.
	if (appData->DesignerMode())
		return false;
	return FetchNext();
}

#include "backend/objCtor.h"

ibClassID ibValueSelectorDataObject::GetClassType() const
{
	const ibCtorMetaValueType* clsFactory =
		GetMetaObject()->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Selection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ibValueSelectorDataObject::GetClassName() const
{
	const ibCtorMetaValueType* clsFactory =
		GetMetaObject()->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Selection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ibValueSelectorDataObject::GetString() const
{
	const ibCtorMetaValueType* clsFactory =
		GetMetaObject()->GetTypeCtor(ibCtorObjectMetaType::ibCtorObjectMetaType_Selection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////

ibValueSelectorRecordDataObject::ibValueSelectorRecordDataObject(const ibValueMetaObjectRecordDataMutableRef* metaObject) :
	ibValueSelectorDataObject(),
	ibValueDataObject(ibGuid(), false),
	m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueSelectorRecordDataObject::FillMembers);
	Reset();
}

ibValueRecordDataObjectRef* ibValueSelectorRecordDataObject::GetObject(const ibGuid& guid) const
{
	if (appData->DesignerMode()) {
		return m_metaObject->CreateObjectValue();
	}

	if (!guid.isValid()) {
		return nullptr;
	}

	return m_metaObject->CreateObjectValue(guid);
}

//////////////////////////////////////////////////////////////////////////

ibValueSelectorRegisterDataObject::ibValueSelectorRegisterDataObject(const ibValueMetaObjectRegisterData* metaObject) :
	ibValueSelectorDataObject(),
	m_metaObject(metaObject)
{
	m_members.Bind(this, &ibValueSelectorRegisterDataObject::FillMembers);
	Reset();
}

ibValueRecordManagerObject* ibValueSelectorRegisterDataObject::GetRecordManager(const ibRowMetaValues& keyValues) const
{
	if (appData->DesignerMode()) {
		return m_metaObject->CreateRecordManagerObjectValue();
	}

	if (keyValues.empty()) {
		return nullptr;
	}

	return m_metaObject->CreateRecordManagerObjectValue(
		m_metaObject->CreateUniqueKeyPair(keyValues)
	);
}

enum Func {
	enNext,
	enReset,
	enGetObjectRecord
};

void ibValueSelectorRecordDataObject::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Next"), wxT("Next()"));
	helper.AppendFunc(wxT("Reset"), wxT("Reset()"));
	helper.AppendFunc(wxT("GetObject"), wxT("GetObject()"));

	//set object name
	wxString objectName;

	for (const auto object : m_metaObject->GetAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			true,
			false,
			object->GetMetaID()
		);
	}

	for (const auto object : m_metaObject->GetTableArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			true,
			false,
			object->GetMetaID()
		);
	}

	helper.AppendProp(wxT("Ref"), m_metaObject->GetMetaID());
}

bool ibValueSelectorRecordDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enNext:
		pvarRetValue = Next();
		return true;
	case enReset:
		Reset();
		return true;
	case enGetObjectRecord:
		pvarRetValue = GetObject(m_objGuid);
		return true;
	}

	return false;
}

bool ibValueSelectorRecordDataObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueSelectorRecordDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (!m_objGuid.isValid()) {
		if (!appData->DesignerMode()) {
			pvarPropVal = ibValue(ibValueTypes::TYPE_NULL);
			return true;
		}
	}
	if (id != m_metaObject->GetMetaID()) {
		pvarPropVal = m_listObjectValue.at(id);
		return true;
	}
	pvarPropVal = ibValueReferenceDataObject::Create(m_metaObject, m_objGuid);
	return true;
}

void ibValueSelectorRegisterDataObject::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(wxT("Next"), wxT("Next()"));
	helper.AppendFunc(wxT("Reset"), wxT("Reset()"));

	if (m_metaObject->HasRecordManager()) {
		helper.AppendFunc(wxT("GetRecordManager"), wxT("GetRecordManager()"));
	}

	//set object name
	wxString objectName;

	for (const auto object : m_metaObject->GetGenericAttributeArrayObject()) {
		if (object->IsDeleted())
			continue;
		if (!object->GetObjectNameAsString(objectName))
			continue;
		helper.AppendProp(
			objectName,
			true,
			false,
			object->GetMetaID()
		);
	}
}

bool ibValueSelectorRegisterDataObject::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enNext:
		pvarRetValue = Next();
		return true;
	case enReset:
		Reset();
		return true;
	case enGetObjectRecord:
		pvarRetValue = GetRecordManager(m_keyValues);
		return true;
	}

	return false;
}

bool ibValueSelectorRegisterDataObject::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueSelectorRegisterDataObject::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const ibMetaID& id = m_members.GetPropData(lPropNum);
	if (m_keyValues.empty()) {
		if (!appData->DesignerMode()) {
			pvarPropVal = ibValue(ibValueTypes::TYPE_NULL);
			return true;
		}
	}
	pvarPropVal = m_current[id];
	return true;
}
