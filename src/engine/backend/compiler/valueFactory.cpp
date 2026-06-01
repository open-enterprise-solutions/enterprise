////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common factory module 
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "backend/backend_exception.h"

static std::vector<ibCtorAbstractType*>* s_factoryCtors = nullptr;
static std::atomic<unsigned int> s_factoryCtorCountChanges = 0;

//*******************************************************************************
//*                      Support dynamic object                                 *
//*******************************************************************************

ibValue* ibValue::CreateObjectRef(const ibClassID& clsid, ibValue** paParams, const long lSizeArray)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(clsid);

	if (typeCtor != nullptr) {
		ibValue* created_value = typeCtor->CreateObject();
		wxASSERT(created_value);
		if (typeCtor->GetObjectTypeCtor() != ibCtorObjectType::ibCtorObjectType_object_system) {
			bool succes = true;
			if (lSizeArray > 0)
				succes = created_value->Init(paParams, lSizeArray);
			else
				succes = created_value->Init();
			if (!succes) {
				wxDELETE(created_value);
				ibBackendCoreException::Error(_("Error initializing object '%s'"), typeCtor->GetClassName());
			}
			created_value->PrepareNames();
		}
		return created_value;
	}
	else {
		ibBackendCoreException::Error(_("Error creating object '%llu'"), clsid);
	}

	return nullptr;
}

void ibValue::RegisterCtor(ibCtorAbstractType* typeCtor)
{
	if (s_factoryCtors == nullptr) s_factoryCtors = new std::vector<ibCtorAbstractType*>;

	if (typeCtor != nullptr) {

		if (ibValue::IsRegisterCtor(typeCtor->GetClassType())) {
			ibBackendCoreException::Error(_("Object '%s' is exist"), typeCtor->GetClassName());
		}
		else if (ibValue::IsRegisterCtor(typeCtor->GetClassName())) {
			ibBackendCoreException::Error(_("Object '%s' is exist"), typeCtor->GetClassName());
		}

#ifdef DEBUG
		if (wxTheApp != NULL)
			wxLogDebug(wxT("* Register class '%s' with clsid '%s:%llu' "), typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif

		s_factoryCtorCountChanges++;

		typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_Register);
		s_factoryCtors->emplace_back(typeCtor);
	}
}

void ibValue::UnRegisterCtor(ibCtorAbstractType*& typeCtor)
{
	if (typeCtor != nullptr && IsRegisterCtor(typeCtor->GetClassType())) {

		typeCtor->CallEvent(ibCtorObjectTypeEvent::ibCtorObjectTypeEvent_UnRegister);

#ifdef DEBUG
		if (wxTheApp != NULL)
			wxLogDebug(wxT("* Unregister class '%s' with clsid '%s:%llu' "), typeCtor->GetClassName(), clsid_to_string(typeCtor->GetClassType()), typeCtor->GetClassType());
#endif
		s_factoryCtors->erase(
			std::remove(s_factoryCtors->begin(), s_factoryCtors->end(), typeCtor)
		);

		wxDELETE(typeCtor);
		s_factoryCtorCountChanges++;
	}
	else if (typeCtor != nullptr) {
		ibBackendCoreException::Error(_("Object '%s' is not register"), typeCtor->GetClassName());
	}

	if (s_factoryCtors->size() == 0) wxDELETE(s_factoryCtors);
}

void ibValue::UnRegisterCtor(const wxString& className)
{
	ibCtorAbstractType* typeCtor = GetAvailableCtor(className);

	if (typeCtor == nullptr) {
		ibBackendCoreException::Error(_("Object '%s' is not exist"), className);
		return;
	}

	UnRegisterCtor(typeCtor);
}

bool ibValue::IsRegisterCtor(const wxString& className)
{
	if (s_factoryCtors == nullptr || className.IsEmpty())
		return false;
	for (auto& typeCtor : *s_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName()))
			return true;
	return false;
}

bool ibValue::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType)
{
	if (s_factoryCtors == nullptr)
		return false;
	for (auto& typeCtor : *s_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName()) && (objectType == typeCtor->GetObjectTypeCtor()))
			return true;
	return false;
}

bool ibValue::IsRegisterCtor(const ibClassID& clsid)
{
	if (s_factoryCtors == nullptr)
		return false;
	for (auto& typeCtor : *s_factoryCtors)
		if (clsid == typeCtor->GetClassType())
			return true;
	return false;
}

ibClassID ibValue::GetTypeIDByRef(const std::type_info& typeInfo)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(typeInfo);
	wxASSERT(typeCtor);
	return typeCtor != nullptr ?
		typeCtor->GetClassType() : 0;
}

ibClassID ibValue::GetTypeIDByRef(const ibValue* objectRef)
{
	// All "object-reference" tags resolve through the C++ type-id
	// (typeid(*objectRef), matched against the registry's type_info key —
	// the replacement for the old wxDECLARE_DYNAMIC_CLASS/wxClassInfo path).
	// Any tag NOT in this set falls through to objectRef->GetClassType() —
	// which for primitive types returns the right id, but for unrecognized
	// reference-shaped tags causes infinite recursion through
	// GetClassType ↔ GetTypeIDByRef. Add new TYPE_* here when adding
	// to ibValueTypes enum.
	if (objectRef->m_typeClass != ibValueTypes::TYPE_VALUE &&
		objectRef->m_typeClass != ibValueTypes::TYPE_OLE &&
		objectRef->m_typeClass != ibValueTypes::TYPE_ENUM &&
		objectRef->m_typeClass != ibValueTypes::TYPE_FUNCTION &&
		objectRef->m_typeClass != ibValueTypes::TYPE_ITERATOR) {
		return objectRef->GetClassType();
	}
	return GetTypeIDByRef(typeid(*objectRef));
}

ibClassID ibValue::GetIDObjectFromString(const wxString& className)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(className);
	if (typeCtor != nullptr)
		return typeCtor->GetClassType();
	ibBackendCoreException::Error(_("Object '%s' is not exist"), className);
	return 0;
}

wxString ibValue::GetNameObjectFromID(const ibClassID& clsid, bool upper)
{
	const ibCtorAbstractType* typeCtor = GetAvailableCtor(clsid);
	if (typeCtor != nullptr) {
		return upper ? typeCtor->GetClassName().Upper() :
			typeCtor->GetClassName();
	}
	ibBackendCoreException::Error(_("Object with id '%llu' is not exist"), clsid);
	return wxEmptyString;
}

wxString ibValue::GetNameObjectFromVT(ibValueTypes valueType, bool upper)
{
	if (valueType > ibValueTypes::TYPE_REFFER)
		return wxEmptyString;
	for (auto& typeCtor : *s_factoryCtors) {
		const ibCtorSingleType* simpleSingleObject = dynamic_cast<ibCtorSingleType*>(typeCtor);
		if (simpleSingleObject != nullptr &&
			valueType == simpleSingleObject->GetValueType()) {
			return upper ? typeCtor->GetClassName().Upper() :
				typeCtor->GetClassName();
		}
	}
	return wxEmptyString;
}

ibValueTypes ibValue::GetVTByID(const ibClassID& clsid)
{
	if (clsid == g_valueUndefinedCLSID)
		return ibValueTypes::TYPE_EMPTY;
	else if (clsid == g_valueBooleanCLSID)
		return ibValueTypes::TYPE_BOOLEAN;
	else if (clsid == g_valueNumberCLSID)
		return ibValueTypes::TYPE_NUMBER;
	else if (clsid == g_valueDateCLSID)
		return ibValueTypes::TYPE_DATE;
	else if (clsid == g_valueStringCLSID)
		return ibValueTypes::TYPE_STRING;
	else if (clsid == g_valueNullCLSID)
		return ibValueTypes::TYPE_NULL;

	const ibCtorAbstractType* typeCtor = GetAvailableCtor(clsid);

	if (typeCtor != nullptr && typeCtor->GetObjectTypeCtor() == ibCtorObjectType_object_enum)
		return ibValueTypes::TYPE_ENUM;
	else if (typeCtor != nullptr)
		return ibValueTypes::TYPE_VALUE;

	return ibValueTypes::TYPE_EMPTY;
}

ibClassID ibValue::GetIDByVT(const ibValueTypes& valueType)
{
	if (valueType == ibValueTypes::TYPE_EMPTY)
		return g_valueUndefinedCLSID;
	else if (valueType == ibValueTypes::TYPE_BOOLEAN)
		return g_valueBooleanCLSID;
	else if (valueType == ibValueTypes::TYPE_NUMBER)
		return g_valueNumberCLSID;
	else if (valueType == ibValueTypes::TYPE_DATE)
		return g_valueDateCLSID;
	else if (valueType == ibValueTypes::TYPE_STRING)
		return g_valueStringCLSID;
	else if (valueType == ibValueTypes::TYPE_NULL)
		return g_valueNullCLSID;

	return 0;
}

ibCtorAbstractType* ibValue::GetAvailableCtor(const wxString& className)
{
	if (s_factoryCtors == nullptr)
		return nullptr;
	for (auto& typeCtor : *s_factoryCtors)
		if (stringUtils::CompareString(className, typeCtor->GetClassName()))
			return typeCtor;
	//ibBackendCoreException::Error("Object '%s' is not exist", className);
	return nullptr;
}

ibCtorAbstractType* ibValue::GetAvailableCtor(const ibClassID& clsid)
{
	if (s_factoryCtors == nullptr)
		return nullptr;
	for (auto& typeCtor : *s_factoryCtors)
		if (clsid == typeCtor->GetClassType())
			return typeCtor;
	//ibBackendCoreException::Error("Object id '%llu' is not exist", clsid);
	return nullptr;
}

ibCtorAbstractType* ibValue::GetAvailableCtor(const std::type_info& typeInfo)
{
	if (s_factoryCtors == nullptr)
		return nullptr;
	for (auto& typeCtor : *s_factoryCtors)
		if (typeInfo == typeCtor->GetTypeInfo())
			return typeCtor;
	//ibBackendCoreException::Error("Object '%s' is not exist", typeInfo.name());
	return nullptr;
}

std::vector<ibCtorAbstractType*> ibValue::GetListCtorsByType(ibCtorObjectType objectType)
{
	std::vector<ibCtorAbstractType*> retVector;
	std::copy_if(s_factoryCtors->begin(), s_factoryCtors->end(),
		std::back_inserter(retVector), [objectType](ibCtorAbstractType* t) { return objectType == t->GetObjectTypeCtor(); }
	);
	std::sort(retVector.begin(), retVector.end(),
		[](ibCtorAbstractType* a, ibCtorAbstractType* b) { return a->GetClassName() > b->GetClassName(); }
	);
	return retVector;
}

//*******************************************************************************

unsigned int ibValue::GetFactoryCountChanges()
{
	return s_factoryCtorCountChanges;
}

//*******************************************************************************