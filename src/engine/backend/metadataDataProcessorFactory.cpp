#include "metadataDataProcessor.h"

#include "backend/objCtor.h"
#include "backend/metadataConfiguration.h"

ibValue* ibMetaDataDataProcessor::CreateObjectRef(const ibClassID& clsid, ibValue** paParams, const long lSizeArray) const
{
	ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(clsid);

	if (typeCtor != nullptr) {
		ibValue* newObject = typeCtor->CreateObject();
		wxASSERT(newObject);

		bool succes = true;
		if (lSizeArray > 0)
			succes = newObject->Init(paParams, lSizeArray);
		else
			succes = newObject->Init();

		if (!succes) {
			wxDELETE(newObject);
			ibBackendCoreException::Error(_("Error initializing object '%s'"), typeCtor->GetClassName());
		}
		// Name surface builds lazily on first GetPMethods() — no eager populate.
		return newObject;
	}

	return activeMetaData->CreateObjectRef(clsid, paParams, lSizeArray);
}

bool ibMetaDataDataProcessor::IsRegisterCtor(const wxString& className) const
{
	if (!ibMetaData::IsRegisterCtor(className))
		return activeMetaData->IsRegisterCtor(className);
	return true;
}

bool ibMetaDataDataProcessor::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType) const
{
	if (!ibMetaData::IsRegisterCtor(className, objectType))
		return activeMetaData->IsRegisterCtor(className);
	return true;
}

bool ibMetaDataDataProcessor::IsRegisterCtor(const wxString& className, ibCtorObjectType objectType, ibCtorObjectMetaType refType) const
{
	if (!ibMetaData::IsRegisterCtor(className, objectType, refType))
		return activeMetaData->IsRegisterCtor(className, objectType, refType);
	return true;
}

bool ibMetaDataDataProcessor::IsRegisterCtor(const ibClassID& clsid) const
{
	if (!ibMetaData::IsRegisterCtor(clsid))
		return activeMetaData->IsRegisterCtor(clsid);
	return true;
}

ibClassID ibMetaDataDataProcessor::GetIDObjectFromString(const wxString& className) const
{
	if (const ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(className))
		return typeCtor->GetClassType();

	return activeMetaData->GetIDObjectFromString(className);
}

wxString ibMetaDataDataProcessor::GetNameObjectFromID(const ibClassID& clsid, bool upper) const
{
	if (const ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(clsid))
		return upper ? typeCtor->GetClassName().Upper() : typeCtor->GetClassName();

	return activeMetaData->GetNameObjectFromID(clsid, upper);
}

ibCtorMetaValueType* ibMetaDataDataProcessor::GetTypeCtor(const ibClassID& clsid) const
{
	if (ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(clsid))   // hot — O(1)
		return typeCtor;
	return activeMetaData->GetTypeCtor(clsid);
}

ibCtorMetaValueType* ibMetaDataDataProcessor::GetTypeCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const
{
	// (metaValue, refType) key — metadata-specific, kept linear.
	ibCtorMetaValueType* result = nullptr;
	m_factoryCtors.ForEach([&](ibCtorMetaValueType* typeCtor) {
		if (result == nullptr && refType == typeCtor->GetMetaTypeCtor() && metaValue == typeCtor->GetMetaObject())
			result = typeCtor;
	});
	if (result != nullptr) return result;
	return activeMetaData->GetTypeCtor(metaValue, refType);
}

ibCtorAbstractType* ibMetaDataDataProcessor::GetAvailableCtor(const wxString& className) const
{
	if (ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(className))
		return typeCtor;
	return activeMetaData->GetAvailableCtor(className);
}

ibCtorAbstractType* ibMetaDataDataProcessor::GetAvailableCtor(const ibClassID& clsid) const
{
	if (ibCtorMetaValueType* typeCtor = m_factoryCtors.Find(clsid))   // hot — O(1)
		return typeCtor;
	return activeMetaData->GetAvailableCtor(clsid);
}

std::vector<ibCtorMetaValueType*> ibMetaDataDataProcessor::GetListCtorsByType() const
{
	return activeMetaData->GetListCtorsByType();
}

bool ibMetaDataDataProcessor::GetOwner(ibMetaData*& metaData) const
{
	metaData = activeMetaData;
	return true;
}

std::vector<ibCtorMetaValueType*> ibMetaDataDataProcessor::GetListCtorsByType(const ibClassID& clsid, ibCtorObjectMetaType refType) const
{
	return activeMetaData->GetListCtorsByType(clsid, refType);
}

std::vector<ibCtorMetaValueType*> ibMetaDataDataProcessor::GetListCtorsByType(ibCtorObjectMetaType refType) const
{
	return activeMetaData->GetListCtorsByType(refType);
}