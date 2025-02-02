#include "metaData.h"
#include "backend/objCtor.h"
#include "backend/appData.h"

CValue* IMetaData::CreateObjectRef(const class_identifier_t& clsid, CValue** paParams, const long lSizeArray)
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);

	if (it != m_factoryCtors.end()) {
		IAbstractTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		CValue* newObject = typeCtor->CreateObject();
		wxASSERT(newObject);

		bool succes = true;
		if (lSizeArray > 0)
			succes = newObject->Init(paParams, lSizeArray);
		else
			succes = newObject->Init();

		if (!succes) {
			wxDELETE(newObject);
			CBackendException::Error("Error initializing object '%s'", typeCtor->GetClassName());
		}
		newObject->PrepareNames();
		return newObject;
	}

	return CValue::CreateObjectRef(clsid, paParams, lSizeArray);
}

void IMetaData::RegisterCtor(IMetaValueTypeCtor* typeCtor)
{
	wxASSERT(typeCtor->GetClassType() > 0);

	if (typeCtor != nullptr) {
		if (IMetaData::IsRegisterCtor(typeCtor->GetClassType())) {
			CBackendException::Error("Object '%s' is exist", typeCtor->GetClassName());
		}
#ifdef DEBUG
		wxLogDebug("* Register class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		typeCtor->CallEvent(eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register);
		m_factoryCtors.push_back(typeCtor);
	}
}

void IMetaData::UnRegisterCtor(IMetaValueTypeCtor*& typeCtor)
{
	auto it = std::find(m_factoryCtors.begin(), m_factoryCtors.end(), typeCtor);
	if (it != m_factoryCtors.end()) {
		wxASSERT(typeCtor);
		typeCtor->CallEvent(eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister);
#ifdef DEBUG
		wxLogDebug("* Unregister class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		m_factoryCtors.erase(it);
		wxDELETE(typeCtor);
	}
	else {
		CBackendException::Error("Object '%s' is not exist", typeCtor->GetClassName());
	}
}

void IMetaData::UnRegisterCtor(const wxString& className)
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [className](IMetaValueTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName()); }
	);
	if (it != m_factoryCtors.end()) {
		IMetaValueTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		typeCtor->CallEvent(eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister);
#ifdef DEBUG
		wxLogDebug("* Unregister class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		m_factoryCtors.erase(it);
		wxDELETE(typeCtor);
	}
	else {
		CBackendException::Error("Object '%s' is not exist", className);
	}
}

bool IMetaData::IsRegisterCtor(const wxString& className) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [className](IMetaValueTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName());
		}
	);
	if (it != m_factoryCtors.end()) return true;
	return CValue::IsRegisterCtor(className);
}

bool IMetaData::IsRegisterCtor(const wxString& className, eCtorObjectType objectType) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [className, objectType](IMetaValueTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName())
			&& (objectType == typeCtor->GetObjectTypeCtor());
		});

	if (it != m_factoryCtors.end()) return true;
	return CValue::IsRegisterCtor(className, objectType);
}

bool IMetaData::IsRegisterCtor(const wxString& className, eCtorObjectType objectType, eCtorMetaType refType) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [className, objectType, refType](IMetaValueTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName())
			&& (eCtorObjectType::eCtorObjectType_object_meta_value == typeCtor->GetObjectTypeCtor()
				&& refType == typeCtor->GetMetaTypeCtor());
		}
	);
	if (it != m_factoryCtors.end()) return true;
	return CValue::IsRegisterCtor(className, objectType);
}

bool IMetaData::IsRegisterCtor(const class_identifier_t& clsid) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IMetaValueTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	if (it != m_factoryCtors.end()) return true;
	return CValue::IsRegisterCtor(clsid);
}

class_identifier_t IMetaData::GetIDObjectFromString(const wxString& clsName) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsName](IAbstractTypeCtor* typeCtor) {
		return stringUtils::CompareString(clsName, typeCtor->GetClassName());
		}
	);
	if (it != m_factoryCtors.end()) {
		IAbstractTypeCtor* typeCtor = *it;
		wxASSERT(typeCtor);
		return typeCtor->GetClassType();
	}
	return CValue::GetIDObjectFromString(clsName);
}

wxString IMetaData::GetNameObjectFromID(const class_identifier_t& clsid, bool upper) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	if (it != m_factoryCtors.end()) {
		IAbstractTypeCtor* typeCtor = *it;
		wxASSERT(typeCtor);
		return upper ? typeCtor->GetClassName().Upper() : typeCtor->GetClassName();
	}
	return CValue::GetNameObjectFromID(clsid, upper);
}

meta_identifier_t IMetaData::GetVTByID(const class_identifier_t& clsid) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	if (it != m_factoryCtors.end()) {
		IMetaValueTypeCtor* typeCtor = *it;
		wxASSERT(typeCtor);
		IMetaObject* metaValue = typeCtor->GetMetaObject();
		wxASSERT(metaValue);
		return metaValue->GetMetaID();
	}
	return CValue::GetVTByID(clsid);
}

class_identifier_t IMetaData::GetIDByVT(const meta_identifier_t& valueType, eCtorMetaType refType) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [valueType, refType](IMetaValueTypeCtor* typeCtor) {
		IMetaObject* metaValue = typeCtor->GetMetaObject();
		wxASSERT(metaValue);
		return refType == typeCtor->GetMetaTypeCtor() && valueType == metaValue->GetMetaID();
		}
	);
	if (it != m_factoryCtors.end()) {
		IAbstractTypeCtor* typeCtor = *it;
		wxASSERT(typeCtor);
		return typeCtor->GetClassType();
	}
	return CValue::GetIDByVT((eValueTypes&)valueType);
}

IMetaValueTypeCtor* IMetaData::GetTypeCtor(const class_identifier_t& clsid) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IMetaValueTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	if (it != m_factoryCtors.end()) {
		return *it;
	}
	return nullptr;
}

IMetaValueTypeCtor* IMetaData::GetTypeCtor(const IMetaObject* metaValue, eCtorMetaType refType) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [metaValue, refType](IMetaValueTypeCtor* typeCtor) {
		return refType == typeCtor->GetMetaTypeCtor() &&
			metaValue == typeCtor->GetMetaObject();
		}
	);
	if (it != m_factoryCtors.end()) {
		return *it;
	}
	return nullptr;
}

IAbstractTypeCtor* IMetaData::GetAvailableCtor(const class_identifier_t& clsid) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IMetaValueTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		});

	if (it != m_factoryCtors.end())
		return *it;
	return CValue::GetAvailableCtor(clsid);
}

IAbstractTypeCtor* IMetaData::GetAvailableCtor(const wxString& className) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [className](IMetaValueTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName());
		}
	);

	if (it != m_factoryCtors.end())
		return *it;
	return CValue::GetAvailableCtor(className);
}

std::vector<IMetaValueTypeCtor*> IMetaData::GetListCtorsByType() const
{
	std::vector<IMetaValueTypeCtor*> retVector;
	std::copy(m_factoryCtors.begin(), m_factoryCtors.end(), std::back_inserter(retVector));
	std::sort(retVector.begin(), retVector.end(), [](IMetaValueTypeCtor* a, IMetaValueTypeCtor* b) {
		IMetaObject* ma = a->GetMetaObject(); IMetaObject* mb = b->GetMetaObject();
		return ma->GetName() > mb->GetName() &&
			a->GetMetaTypeCtor() > b->GetMetaTypeCtor();
		}
	);
	return retVector;
}

std::vector<IMetaValueTypeCtor*> IMetaData::GetListCtorsByType(const class_identifier_t& clsid, eCtorMetaType refType) const
{
	std::vector<IMetaValueTypeCtor*> retVector;
	std::copy_if(m_factoryCtors.begin(), m_factoryCtors.end(), std::back_inserter(retVector), [clsid, refType](IMetaValueTypeCtor* t) {
		IMetaObject* const m = t->GetMetaObject();
		return refType == t->GetMetaTypeCtor() &&
			clsid == m->GetClassType(); }
	);
	std::sort(retVector.begin(), retVector.end(), [](IMetaValueTypeCtor* a, IMetaValueTypeCtor* b) {
		IMetaObject* ma = a->GetMetaObject(); IMetaObject* mb = b->GetMetaObject();
		return ma->GetName() > mb->GetName() &&
			a->GetMetaTypeCtor() > b->GetMetaTypeCtor();
		}
	);
	return retVector;
}

std::vector<IMetaValueTypeCtor*> IMetaData::GetListCtorsByType(eCtorMetaType refType) const
{
	std::vector<IMetaValueTypeCtor*> retVector;
	std::copy_if(m_factoryCtors.begin(), m_factoryCtors.end(), std::back_inserter(retVector), [refType](IMetaValueTypeCtor* t) { return refType == t->GetMetaTypeCtor(); });
	std::sort(retVector.begin(), retVector.end(), [](IMetaValueTypeCtor* a, IMetaValueTypeCtor* b) {
		IMetaObject* ma = a->GetMetaObject(); IMetaObject* mb = b->GetMetaObject();
		return ma->GetName() > mb->GetName() &&
			a->GetMetaTypeCtor() > b->GetMetaTypeCtor();
		}
	);
	return retVector;
}