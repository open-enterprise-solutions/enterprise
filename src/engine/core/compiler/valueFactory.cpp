////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : common factory module 
////////////////////////////////////////////////////////////////////////////

#include "value.h"
#include "functions.h"

static std::vector<IObjectValueAbstract*>* s_factoryObjects = NULL;

//*******************************************************************************
//*                       Guard factory objects                                 *
//*******************************************************************************

inline void AllocateFactoryObjects()
{
	s_factoryObjects = new std::vector<IObjectValueAbstract*>;
}

inline void DestroyFactoryObjects()
{
	wxDELETE(s_factoryObjects);
}

//*******************************************************************************
//*                      Support dynamic object                                 *
//*******************************************************************************

#include "appData.h"
#include "utils/stringUtils.h"

CValue* CValue::CreateObjectRef(const wxString& className, CValue** aParams)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error(_("Error creating object '%s'"), className.wc_str());

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);

	CValue* newObject = singleObject->CreateObject();
	wxASSERT(newObject);

	if (singleObject->GetObjectType() == eObjectType::eObjectType_object) {
		if (!appData->DesignerMode()) {
			if (aParams) {
				if (!newObject->Init(aParams)) {
					wxDELETE(newObject);
					CTranslateError::Error(_("Error initializing object '%s'"), className.wc_str());
				}
			}
			else {
				if (!newObject->Init()) {
					wxDELETE(newObject);
					CTranslateError::Error(_("Error initializing object '%s'"), className.wc_str());
				}
			}
		}
	}

	return newObject;
}

void CValue::RegisterObject(const wxString& className, IObjectValueAbstract* singleObject)
{
	if (!s_factoryObjects) {
		AllocateFactoryObjects();
	}

	if (singleObject != NULL) {

		if (CValue::IsRegisterObject(className)) {
			CTranslateError::Error(_("Object '%s' is exist"), className.wc_str());
		}

		if (CValue::IsRegisterObject(singleObject->GetClassType())) {
			CTranslateError::Error(_("Object '%s' is exist"), className.wc_str());
		}

		//first initialization
		if (singleObject->GetObjectType() == eObjectType::eObjectType_object) {
			CValue* newObject = singleObject->CreateObject();
			newObject->PrepareNames();
			delete newObject;
		}

		s_factoryObjects->push_back(singleObject);
	}
}

void CValue::UnRegisterObject(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded == s_factoryObjects->end()) {
		CTranslateError::Error(_("Object '%s' is not exist"), className.wc_str());
	}

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);

	s_factoryObjects->erase(itFounded);

	if (s_factoryObjects->size() == 0) {
		DestroyFactoryObjects();
	}

	delete singleObject;
}

bool CValue::IsRegisterObject(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	return itFounded != s_factoryObjects->end();
}

bool CValue::IsRegisterObject(const wxString& className, eObjectType objectType)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className, objectType](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName())
			&& (objectType == singleObject->GetObjectType());
		});

	return itFounded != s_factoryObjects->end();
}

bool CValue::IsRegisterObject(const CLASS_ID& clsid)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	return itFounded != s_factoryObjects->end();
}

CLASS_ID CValue::GetTypeIDByRef(const wxClassInfo* classInfo)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [classInfo](IObjectValueAbstract* singleObject) {
		return classInfo == singleObject->GetClassInfo();
		});

	if (itFounded == s_factoryObjects->end())
		return 0;

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return singleObject->GetClassType();
}

CLASS_ID CValue::GetTypeIDByRef(const ITypeValue* objectRef)
{
	if (objectRef->m_typeClass != eValueTypes::TYPE_VALUE &&
		objectRef->m_typeClass != eValueTypes::TYPE_ENUM &&
		objectRef->m_typeClass != eValueTypes::TYPE_OLE)
	{
		return objectRef->GetClassType();
	}

	wxClassInfo* classInfo = objectRef->GetClassInfo();
	wxASSERT(classInfo);
	return GetTypeIDByRef(classInfo);
}

CLASS_ID CValue::GetIDObjectFromString(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error(_("Object '%s' is not exist"), className.wc_str());

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return singleObject->GetClassType();
}

bool CValue::CompareObjectName(const wxString& className, eValueTypes valueType)
{
	return StringUtils::CompareString(className, GetNameObjectFromVT(valueType));
}

wxString CValue::GetNameObjectFromID(const CLASS_ID& clsid, bool upper)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error(_("Object with id '%llu' is not exist"), clsid);

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return upper ? singleObject->GetClassName().Upper() : singleObject->GetClassName();
}

wxString CValue::GetNameObjectFromVT(eValueTypes valueType, bool upper)
{
	if (valueType > eValueTypes::TYPE_REFFER)
		return wxEmptyString;

	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [valueType](IObjectValueAbstract* singleObject) {
		ISimpleObjectValueSingle* simpleSingleObject = dynamic_cast<ISimpleObjectValueSingle*>(singleObject);
		if (simpleSingleObject) {
			return valueType == simpleSingleObject->GetValueType();
		}
		return false;
		});

	if (itFounded == s_factoryObjects->end())
		return wxEmptyString;

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return upper ? singleObject->GetClassName().Upper() : singleObject->GetClassName();
}

eValueTypes CValue::GetVTByID(const CLASS_ID& clsid)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetClassType();
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
		});

	if (itFounded == s_factoryObjects->end())
		return 0;

	IObjectValueAbstract* singleObject = *itFounded;
	wxASSERT(singleObject);
	return singleObject->GetClassType();
}

IObjectValueAbstract* CValue::GetAvailableObject(const CLASS_ID& clsid)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [clsid](IObjectValueAbstract* singleObject) {
		return clsid == singleObject->GetClassType();
		});

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error(_("Object id '%llu' is not exist"), clsid);

	return *itFounded;
}

IObjectValueAbstract* CValue::GetAvailableObject(const wxString& className)
{
	auto itFounded = std::find_if(s_factoryObjects->begin(), s_factoryObjects->end(), [className](IObjectValueAbstract* singleObject) {
		return StringUtils::CompareString(className, singleObject->GetClassName());
		});

	if (itFounded == s_factoryObjects->end())
		CTranslateError::Error(_("Object '%s' is not exist"), className.wc_str());

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