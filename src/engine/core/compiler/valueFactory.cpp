////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common factory module 
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "translateError.h"

static std::vector<IObjectValueAbstract*>* s_factoryObjects = NULL;

//*******************************************************************************
//*                      Support dynamic object                                 *
//*******************************************************************************

#include "appData.h"
#include "utils/stringUtils.h"

CValue* CValue::CreateObjectRef(const CLASS_ID& clsid, CValue** paParams, const long lSizeArray)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetTypeClass();
		}
	);

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error("Error creating object '%ul'", clsid);

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);

	CValue* newObject = singleObject->CreateObject();
	wxASSERT(newObject);
	if (!appData->DesignerMode()) {
		if (lSizeArray > 0) {
			if (!newObject->Init(paParams, lSizeArray)) {
				wxDELETE(newObject);
				CTranslateError::Error("Error initializing object '%ul'", clsid);
			}
		}
		else {
			if (!newObject->Init()) {
				wxDELETE(newObject);
				CTranslateError::Error("Error initializing object '%ul'", clsid);
			}
		}
	}
	return newObject;
}

void CValue::RegisterObject(const wxString& className, IObjectValueAbstract* singleObject)
{
	if (s_factoryObjects == NULL) {
		s_factoryObjects = new std::vector<IObjectValueAbstract*>;
	}

	if (singleObject != NULL) {

		if (CValue::IsRegisterObject(singleObject->GetTypeClass())) {
			CTranslateError::Error("Object '%s' is exist"), className;
		}

		singleObject->CallEvent(eObjectTypeEvent::eObjectTypeEvent_Register);
		s_factoryObjects->push_back(singleObject);
	}
}

void CValue::UnRegisterObject(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		}
	);

	if (itFounded == s_factoryObjects->end()) {
		CTranslateError::Error("Object '%s' is not exist", className);
	}

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	singleObject->CallEvent(eObjectTypeEvent::eObjectTypeEvent_UnRegister);

	s_factoryObjects->erase(itFounded);

	if (s_factoryObjects->size() == 0) {
		wxDELETE(s_factoryObjects);
	}

	delete singleObject;
}

bool CValue::IsRegisterObject(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		}
	);

	return itFounded != s_factoryObjects->end();
}

bool CValue::IsRegisterObject(const wxString& className, eObjectType objectType)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className, objectType](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName())
		&& (objectType == singleObject->GetObjectType());
		}
	);

	return itFounded != s_factoryObjects->end();
}

bool CValue::IsRegisterObject(const CLASS_ID& clsid)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetTypeClass();
		}
	);

	return itFounded != s_factoryObjects->end();
}

CLASS_ID CValue::GetTypeIDByRef(const wxClassInfo* classInfo)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(),
		[classInfo](IObjectValueAbstract* singleObject) {
			return classInfo == singleObject->GetClassInfo();
		}
	);

	if (itFounded == s_factoryObjects->end())
		return 0;

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return singleObject->GetTypeClass();
}

CLASS_ID CValue::GetTypeIDByRef(const CValue* objectRef)
{
	if (objectRef->m_typeClass != eValueTypes::TYPE_VALUE &&
		objectRef->m_typeClass != eValueTypes::TYPE_ENUM &&
		objectRef->m_typeClass != eValueTypes::TYPE_OLE) {
		return objectRef->GetTypeClass();
	}

	wxClassInfo* classInfo = objectRef->GetClassInfo();
	wxASSERT(classInfo);
	return GetTypeIDByRef(classInfo);
}

CLASS_ID CValue::GetIDObjectFromString(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		}
	);

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error("Object '%s' is not exist", className);

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return singleObject->GetTypeClass();
}

bool CValue::CompareObjectName(const wxString& className, eValueTypes valueType)
{
	return StringUtils::CompareString(className, GetNameObjectFromVT(valueType));
}

wxString CValue::GetNameObjectFromID(const CLASS_ID& clsid, bool upper)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetTypeClass();
		}
	);

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error("Object with id '%llu' is not exist", clsid);

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return upper ? singleObject->GetClassName().Upper() :
		singleObject->GetClassName();
}

wxString CValue::GetNameObjectFromVT(eValueTypes valueType, bool upper)
{
	if (valueType > eValueTypes::TYPE_REFFER)
		return wxEmptyString;
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [valueType](IObjectValueAbstract* singleObject) {
		ISimpleObjectValueSingle* simpleSingleObject = dynamic_cast<ISimpleObjectValueSingle*>(singleObject);
	if (simpleSingleObject != NULL)
		return valueType == simpleSingleObject->GetValueType();
	return false;
		}
	);

	if (itFounded == s_factoryObjects->end())
		return wxEmptyString;

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return upper ? singleObject->GetClassName().Upper() :
		singleObject->GetClassName();
}

eValueTypes CValue::GetVTByID(const CLASS_ID& clsid)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetTypeClass();
		});

	if (itFounded == s_factoryObjects->end())
		return eValueTypes::TYPE_EMPTY;

	ISimpleObjectValueSingle* singleObject =
		dynamic_cast<ISimpleObjectValueSingle*>(*itFounded);

	if (singleObject == NULL)
		return eValueTypes::TYPE_EMPTY;

	return singleObject->GetValueType();
}

CLASS_ID CValue::GetIDByVT(const eValueTypes& valueType)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [valueType](IObjectValueAbstract* singleObject) {
		ISimpleObjectValueSingle* simpleSingleObject = dynamic_cast<ISimpleObjectValueSingle*>(singleObject);
	if (simpleSingleObject) {
		return valueType == simpleSingleObject->GetValueType();
	}
	return simpleSingleObject ?
		simpleSingleObject->GetValueType() == eValueTypes::TYPE_EMPTY : false;
		}
	);

	if (itFounded == s_factoryObjects->end())
		return 0;

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return singleObject->GetTypeClass();
}

IObjectValueAbstract* CValue::GetAvailableObject(const CLASS_ID& clsid)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetTypeClass();
		}
	);

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error("Object id '%llu' is not exist", clsid);

	return *itFounded;
}

IObjectValueAbstract* CValue::GetAvailableObject(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error("Object '%s' is not exist", className);

	return *itFounded;
}

wxArrayString CValue::GetAvailableObjects(eObjectType objectType)
{
	wxArrayString classes;
	for (auto singleObject : *s_factoryObjects) {
		if (objectType == singleObject->GetObjectType()) {
			classes.push_back(singleObject->GetClassName());
		}
	}
	classes.Sort();
	return classes;
}