#include "objectSelector.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"

ISelectorObject::ISelectorObject() : CValue(eValueTypes::TYPE_VALUE, true),
m_methodHelper(new CMethodHelper())
{
}

ISelectorObject::~ISelectorObject()
{
	wxDELETE(m_methodHelper);
}

#include "backend/objCtor.h"

class_identifier_t ISelectorObject::GetClassType() const
{
	IMetaValueTypeCtor* clsFactory =
		GetMetaObject()->GetTypeCtor(eCtorMetaType::eCtorMetaType_Selection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassType();
}

wxString ISelectorObject::GetClassName() const
{
	IMetaValueTypeCtor* clsFactory =
		GetMetaObject()->GetTypeCtor(eCtorMetaType::eCtorMetaType_Selection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

wxString ISelectorObject::GetString() const
{
	IMetaValueTypeCtor* clsFactory =
		GetMetaObject()->GetTypeCtor(eCtorMetaType::eCtorMetaType_Selection);
	wxASSERT(clsFactory);
	return clsFactory->GetClassName();
}

/////////////////////////////////////////////////////////////////////////

CSelectorDataObject::CSelectorDataObject(IMetaObjectRecordDataMutableRef* metaObject) :
	ISelectorObject(),
	IObjectValueInfo(Guid(), false),
	m_metaObject(metaObject)
{
	Reset();
}

bool CSelectorDataObject::Next()
{
	if (appData->DesignerMode()) {
		return false;
	}

	if (!m_objGuid.isValid()) {
		if (m_currentValues.size() > 0) {
			auto itStart = m_currentValues.begin();
			m_objGuid = *itStart;
			return Read();
		}
	}
	else {
		auto it = std::find(m_currentValues.begin(), m_currentValues.end(), m_objGuid);
		ptrdiff_t pos =
			std::distance(m_currentValues.begin(), it);
		if (pos == m_currentValues.size() - 1) {
			return false;
		}
		std::advance(it, 1);
		m_objGuid = *it;
		return Read();
	}

	return false;
}

IRecordDataObjectRef* CSelectorDataObject::GetObject(const Guid& guid) const
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

CSelectorRegisterObject::CSelectorRegisterObject(IMetaObjectRegisterData* metaObject) :
	ISelectorObject(),
	m_metaObject(metaObject)
{
	Reset();
}

bool CSelectorRegisterObject::Next()
{
	if (appData->DesignerMode()) {
		return false;
	}

	if (m_keyValues.empty()) {
		if (m_currentValues.size() > 0) {
			auto itStart = m_currentValues.begin();
			m_keyValues = *itStart;
			return Read();
		}
	}
	else {
		auto it = std::find(m_currentValues.begin(), m_currentValues.end(), m_keyValues);
		ptrdiff_t pos =
			std::distance(m_currentValues.begin(), it);
		if (pos == m_currentValues.size() - 1) {
			return false;
		}
		std::advance(it, 1);
		m_keyValues = *it;
		return Read();
	}

	return false;
}

IRecordManagerObject* CSelectorRegisterObject::GetRecordManager(const valueArray_t& keyValues) const
{
	if (appData->DesignerMode()) {
		return m_metaObject->CreateRecordManagerObjectValue();
	}

	if (keyValues.empty()) {
		return nullptr;
	}

	return m_metaObject->CreateRecordManagerObjectValue(
		CUniquePairKey(m_metaObject, keyValues)
	);
}

enum Func {
	enNext,
	enReset,
	enGetObjectRecord
};

void CSelectorDataObject::PrepareNames() const
{
	m_methodHelper->ClearHelper();

	m_methodHelper->AppendFunc("next", "next()");
	m_methodHelper->AppendFunc("reset", "reset()");
	m_methodHelper->AppendFunc("getObject", "getObject()");

	for (auto& obj : m_metaObject->GetObjectAttributes()) {
		m_methodHelper->AppendProp(obj->GetName(), true, false, obj->GetMetaID());
	}

	for (auto obj : m_metaObject->GetObjectTables()) {
		m_methodHelper->AppendProp(obj->GetName(), true, false, obj->GetMetaID());
	}

	m_methodHelper->AppendProp(wxT("reference"), m_metaObject->GetMetaID());
}

bool CSelectorDataObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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

bool CSelectorDataObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CSelectorDataObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (!m_objGuid.isValid()) {
		if (!appData->DesignerMode()) {
			pvarPropVal = CValue(eValueTypes::TYPE_NULL);
			return true;
		}
	}
	if (!m_metaObject->IsDataReference(id)) {
		pvarPropVal = m_objectValues.at(id);
		return true;
	}
	pvarPropVal = CReferenceDataObject::Create(m_metaObject, m_objGuid);
	return true;
}

void CSelectorRegisterObject::PrepareNames() const
{
	m_methodHelper->AppendFunc("next", "next()");
	m_methodHelper->AppendFunc("reset", "reset()");

	if (m_metaObject->HasRecordManager()) {
		m_methodHelper->AppendFunc("getRecordManager", "getRecordManager()");
	}

	for (auto& obj : m_metaObject->GetGenericAttributes()) {
		m_methodHelper->AppendProp(
			obj->GetName(),
			true,
			false,
			obj->GetMetaID()
		);
	}
}

bool CSelectorRegisterObject::CallAsFunc(const long lMethodNum, CValue& pvarRetValue, CValue** paParams, const long lSizeArray)
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

bool CSelectorRegisterObject::SetPropVal(const long lPropNum, const CValue& varPropVal)
{
	return false;
}

bool CSelectorRegisterObject::GetPropVal(const long lPropNum, CValue& pvarPropVal)
{
	const meta_identifier_t& id = m_methodHelper->GetPropData(lPropNum);
	if (m_keyValues.empty()) {
		if (!appData->DesignerMode()) {
			pvarPropVal = CValue(eValueTypes::TYPE_NULL);
			return true;
		}
	}
	pvarPropVal = m_objectValues[m_keyValues][id];
	return true;
}
