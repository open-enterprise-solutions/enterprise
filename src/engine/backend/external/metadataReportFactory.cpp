#include "metadataReport.h"

#include "backend/appData.h"

#include "backend/objCtor.h"
#include "backend/metadataConfiguration.h"


CValue* CMetaDataReport::CreateObjectRef(const class_identifier_t& clsid, CValue** paParams, const long lSizeArray)
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
	return commonMetaData->CreateObjectRef(clsid, paParams, lSizeArray);
}

bool CMetaDataReport::IsRegisterCtor(const wxString& className) const
{
	if (!IMetaData::IsRegisterCtor(className))
		return commonMetaData->IsRegisterCtor(className);
	return true;
}

bool CMetaDataReport::IsRegisterCtor(const wxString& className, eCtorObjectType objectType) const
{
	if (!IMetaData::IsRegisterCtor(className, objectType))
		return commonMetaData->IsRegisterCtor(className);
	return true;
}

bool CMetaDataReport::IsRegisterCtor(const wxString& className, eCtorObjectType objectType, eCtorMetaType refType) const
{
	if (!IMetaData::IsRegisterCtor(className, objectType, refType))
		return commonMetaData->IsRegisterCtor(className, objectType, refType);
	return true;
}

bool CMetaDataReport::IsRegisterCtor(const class_identifier_t& clsid) const
{
	if (!IMetaData::IsRegisterCtor(clsid))
		return commonMetaData->IsRegisterCtor(clsid);
	return true;
}

class_identifier_t CMetaDataReport::GetIDObjectFromString(const wxString& clsName) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsName](IAbstractTypeCtor* typeCtor) {
		return stringUtils::CompareString(clsName, typeCtor->GetClassName());
		});

	if (it != m_factoryCtors.end()) {
		IAbstractTypeCtor* typeCtor = *it;
		wxASSERT(typeCtor);
		return typeCtor->GetClassType();
	}

	return commonMetaData->GetIDObjectFromString(clsName);
}

wxString CMetaDataReport::GetNameObjectFromID(const class_identifier_t& clsid, bool upper) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IAbstractTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType();
		});

	if (it != m_factoryCtors.end()) {
		IAbstractTypeCtor* typeCtor = *it;
		wxASSERT(typeCtor);
		return upper ? typeCtor->GetClassName().Upper() : typeCtor->GetClassName();
	}

	return commonMetaData->GetNameObjectFromID(clsid, upper);
}

IMetaValueTypeCtor* CMetaDataReport::GetTypeCtor(const class_identifier_t& clsid) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [clsid](IMetaValueTypeCtor* typeCtor) {
		return clsid == typeCtor->GetClassType(); }
	);

	if (it != m_factoryCtors.end()) {
		return *it;
	}

	return commonMetaData->GetTypeCtor(clsid);
}

IMetaValueTypeCtor* CMetaDataReport::GetTypeCtor(const IMetaObject* metaValue, eCtorMetaType refType) const
{
	auto it = std::find_if(m_factoryCtors.begin(), m_factoryCtors.end(), [metaValue, refType](IMetaValueTypeCtor* typeCtor) {
		return refType == typeCtor->GetMetaTypeCtor() &&
			metaValue == typeCtor->GetMetaObject();
		});

	if (it != m_factoryCtors.end()) {
		return *it;
	}

	return commonMetaData->GetTypeCtor(metaValue, refType);
}

IAbstractTypeCtor* CMetaDataReport::GetAvailableCtor(const class_identifier_t& clsid) const
{
	return commonMetaData->GetAvailableCtor(clsid);
}

IAbstractTypeCtor* CMetaDataReport::GetAvailableCtor(const wxString& className) const
{
	return commonMetaData->GetAvailableCtor(className);
}

std::vector<IMetaValueTypeCtor*> CMetaDataReport::GetListCtorsByType() const
{
	return commonMetaData->GetListCtorsByType();
}

std::vector<IMetaValueTypeCtor*> CMetaDataReport::GetListCtorsByType(const class_identifier_t& clsid, eCtorMetaType refType) const
{
	return commonMetaData->GetListCtorsByType(clsid, refType);
}

std::vector<IMetaValueTypeCtor*> CMetaDataReport::GetListCtorsByType(eCtorMetaType refType) const
{
	return commonMetaData->GetListCtorsByType(refType);
}

IMetaObject* CMetaDataReport::GetMetaObject(const meta_identifier_t& id)
{
	IMetaObject* metaObject = IMetaData::GetMetaObject(id);

	if (metaObject) {
		return metaObject;
	}

	return commonMetaData->GetMetaObject(id);
}