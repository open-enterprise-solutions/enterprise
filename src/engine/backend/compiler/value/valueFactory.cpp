////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common factory module 
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "backend/backend_exception.h"

static std::vector<IAbstractTypeCtor*>* s_factoryCtors = nullptr;

//*******************************************************************************
//*                      Support dynamic object                                 *
//*******************************************************************************

CValue* CValue::CreateObjectRef(const class_identifier_t& clsid, CValue** paParams, const long lSizeArray)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	if (it != s_factoryCtors->end()) {
		IAbstractTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		CValue* created_value = typeCtor->CreateObject();
		wxASSERT(created_value);
		if (typeCtor->GetObjectTypeCtor() != eCtorObjectType::eCtorObjectType_object_system) {
			bool succes = true;
			if (lSizeArray > 0)
				succes = created_value->Init(paParams, lSizeArray);
			else
				succes = created_value->Init();
			if (!succes) {
				wxDELETE(created_value);
				CBackendException::Error("Error initializing object '%s'", typeCtor->GetClassName());
			}
			created_value->PrepareNames();
		}
		return created_value;
	}
	else {
		CBackendException::Error("Error creating object '%llu'", clsid);
	}
	return nullptr;
}

void CValue::RegisterCtor(IAbstractTypeCtor* typeCtor)
{
	if (s_factoryCtors == nullptr) {
		s_factoryCtors = new std::vector<IAbstractTypeCtor*>;
	}
	if (typeCtor != nullptr) {
		if (CValue::IsRegisterCtor(typeCtor->GetClassType())) {
			CBackendException::Error("Object '%s' is exist"), typeCtor->GetClassName();
		}
#ifdef DEBUG
		wxLogDebug("* Register class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		typeCtor->CallEvent(eCtorObjectTypeEvent::eCtorObjectTypeEvent_Register);
		s_factoryCtors->push_back(typeCtor);
	}
}

void CValue::UnRegisterCtor(IAbstractTypeCtor*& typeCtor)
{
	auto it = std::find(s_factoryCtors->begin(), s_factoryCtors->end(), typeCtor);
	if (it != s_factoryCtors->end()) {
		wxASSERT(typeCtor);
		typeCtor->CallEvent(eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister);

#ifdef DEBUG
		wxLogDebug("* Unregister class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		s_factoryCtors->erase(it);
		wxDELETE(typeCtor);
	}
	else {
		CBackendException::Error("Object '%s' is not exist", typeCtor->GetClassName());
	}
	if (s_factoryCtors->size() == 0) {
		wxDELETE(s_factoryCtors);
	}
}

void CValue::UnRegisterCtor(const wxString& className)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [className](IAbstractTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName());
		}
	);
	if (it != s_factoryCtors->end()) {
		IAbstractTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		typeCtor->CallEvent(eCtorObjectTypeEvent::eCtorObjectTypeEvent_UnRegister);

#ifdef DEBUG
		wxLogDebug("* Unregister class '%s' with clsid '%s:%llu' ", typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		s_factoryCtors->erase(it);
		wxDELETE(typeCtor);
	}
	else {
		CBackendException::Error("Object '%s' is not exist", className);
	}

	if (s_factoryCtors->size() == 0) {
		wxDELETE(s_factoryCtors);
	}
}

bool CValue::IsRegisterCtor(const wxString& className)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [className](IAbstractTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName());
		}
	);
	return it != s_factoryCtors->end();
}

bool CValue::IsRegisterCtor(const wxString& className, eCtorObjectType objectType)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [className, objectType](IAbstractTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName())
			&& (objectType == typeCtor->GetObjectTypeCtor());
		}
	);
	return it != s_factoryCtors->end();
}

bool CValue::IsRegisterCtor(const class_identifier_t& clsid)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	return it != s_factoryCtors->end();
}

class_identifier_t CValue::GetTypeIDByRef(const wxClassInfo* classInfo)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(),
		[classInfo](IAbstractTypeCtor* typeCtor) {
			return classInfo == typeCtor->GetClassInfo();
		}
	);
	if (it != s_factoryCtors->end()) {
		IAbstractTypeCtor* typeCtor = *it;
		wxASSERT(typeCtor);
		return typeCtor ? typeCtor->GetClassType() : 0;
	}
	return 0;
}

class_identifier_t CValue::GetTypeIDByRef(const CValue* objectRef)
{
	if (objectRef->m_typeClass != eValueTypes::TYPE_VALUE &&
		objectRef->m_typeClass != eValueTypes::TYPE_ENUM) {
		return objectRef->GetClassType();
	}
	wxClassInfo* classInfo = objectRef->GetClassInfo();
	wxASSERT(classInfo);
	if (classInfo != nullptr)
		return GetTypeIDByRef(classInfo);
	return 0;
}

class_identifier_t CValue::GetIDObjectFromString(const wxString& className)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [className](IAbstractTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName());
		}
	);
	if (it != s_factoryCtors->end()) {
		IAbstractTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		return typeCtor->GetClassType();
	}
	CBackendException::Error("Object '%s' is not exist", className);
	return 0;
}

wxString CValue::GetNameObjectFromID(const class_identifier_t& clsid, bool upper)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	if (it != s_factoryCtors->end()) {
		IAbstractTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		return upper ? typeCtor->GetClassName().Upper() :
			typeCtor->GetClassName();
	}
	CBackendException::Error("Object with id '%llu' is not exist", clsid);
	return wxEmptyString;
}

wxString CValue::GetNameObjectFromVT(eValueTypes valueType, bool upper)
{
	if (valueType > eValueTypes::TYPE_REFFER)
		return wxEmptyString;

	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [valueType](IAbstractTypeCtor* typeCtor) {
		IPrimitiveTypeCtor* simpleSingleObject = dynamic_cast<IPrimitiveTypeCtor*>(typeCtor);
		if (simpleSingleObject != nullptr)
			return valueType == simpleSingleObject->GetValueType();
		return false;
		}
	);
	if (it != s_factoryCtors->end()) {
		IAbstractTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		return upper ? typeCtor->GetClassName().Upper() :
			typeCtor->GetClassName();
	}
	return wxEmptyString;
}

eValueTypes CValue::GetVTByID(const class_identifier_t& clsid)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(),
		[clsid](IAbstractTypeCtor* typeCtor) {
			return clsid == typeCtor->GetClassType();
		}
	);
	if (it != s_factoryCtors->end()) {
		IPrimitiveTypeCtor* typeCtor = dynamic_cast<IPrimitiveTypeCtor*>(*it);
		if (typeCtor == nullptr) return eValueTypes::TYPE_EMPTY;
		return typeCtor->GetValueType();
	}
	return eValueTypes::TYPE_EMPTY;
}

class_identifier_t CValue::GetIDByVT(const eValueTypes& valueType)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [valueType](IAbstractTypeCtor* typeCtor) {
		IPrimitiveTypeCtor* simpleSingleObject = dynamic_cast<IPrimitiveTypeCtor*>(typeCtor);
		if (simpleSingleObject) {
			return valueType == simpleSingleObject->GetValueType();
		}
		return simpleSingleObject ?
			simpleSingleObject->GetValueType() == eValueTypes::TYPE_EMPTY : false;
		}
	);
	if (it != s_factoryCtors->end()) {
		IAbstractTypeCtor* typeCtor(*it);
		wxASSERT(typeCtor);
		return typeCtor->GetClassType();
	}
	return 0;
}

IAbstractTypeCtor* CValue::GetAvailableCtor(const wxString& className)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [className](IAbstractTypeCtor* typeCtor) {
		return stringUtils::CompareString(className, typeCtor->GetClassName());
		}
	);
	if (it != s_factoryCtors->end())
		return *it;
	CBackendException::Error("Object '%s' is not exist", className);
	return nullptr;
}

IAbstractTypeCtor* CValue::GetAvailableCtor(const class_identifier_t& clsid)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		}
	);
	if (it != s_factoryCtors->end())
		return *it;
	CBackendException::Error("Object id '%llu' is not exist", clsid);
	return nullptr;
}

IAbstractTypeCtor* CValue::GetAvailableCtor(const wxClassInfo* classInfo)
{
	auto it = std::find_if(s_factoryCtors->begin(), s_factoryCtors->end(), [classInfo](IAbstractTypeCtor* typeCtor) {
		return classInfo, typeCtor->GetClassInfo();
		}
	);
	if (it != s_factoryCtors->end())
		return *it;
	CBackendException::Error("Object '%s' is not exist", classInfo->GetClassName());
	return nullptr;
}

std::vector<IAbstractTypeCtor*> CValue::GetListCtorsByType(eCtorObjectType objectType)
{
	std::vector<IAbstractTypeCtor*> retVector;
	std::copy_if(s_factoryCtors->begin(), s_factoryCtors->end(),
		std::back_inserter(retVector), [objectType](IAbstractTypeCtor* t) { return objectType == t->GetObjectTypeCtor(); }
	);
	std::sort(retVector.begin(), retVector.end(),
		[](IAbstractTypeCtor* a, IAbstractTypeCtor* b) { return a->GetClassName() > b->GetClassName(); }
	);
	return retVector;
}