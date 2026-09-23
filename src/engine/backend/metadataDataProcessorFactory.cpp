#include "metadataDataProcessor.h"

#include "backend/objCtor.h"
#include "backend/metadataConfiguration.h"

// (No CreateObject of its own: ibMetaData's asks GetTypeCtor below, which looks here first and then at
// the configuration — the whole of what a second copy of the factory used to add.)

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
	if (const ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(className) : nullptr))
		return typeCtor->GetClassType();

	return activeMetaData->GetIDObjectFromString(className);
}

wxString ibMetaDataDataProcessor::GetNameObjectFromID(const ibClassID& clsid, bool upper) const
{
	if (const ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(clsid) : nullptr))
		return upper ? typeCtor->GetClassName().Upper() : typeCtor->GetClassName();

	return activeMetaData->GetNameObjectFromID(clsid, upper);
}

ibCtorMetaValueType* ibMetaDataDataProcessor::GetTypeCtor(const ibClassID& clsid) const
{
	if (ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(clsid) : nullptr))   // hot — O(1)
		return typeCtor;
	return activeMetaData->GetTypeCtor(clsid);
}

ibCtorMetaValueType* ibMetaDataDataProcessor::GetTypeCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const
{
	// (metaValue, refType) key — metadata-specific, kept linear.
	ibCtorMetaValueType* result = nullptr;
	if (m_image) m_image->ForEachCtor([&](ibCtorMetaValueType* typeCtor) {
		if (result == nullptr && refType == typeCtor->GetMetaTypeCtor() && metaValue == typeCtor->GetMetaObject())
			result = typeCtor;
	});
	if (result != nullptr) return result;
	return activeMetaData->GetTypeCtor(metaValue, refType);
}

ibCtorAbstractType* ibMetaDataDataProcessor::GetAvailableCtor(const wxString& className) const
{
	if (ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(className) : nullptr))
		return typeCtor;
	return activeMetaData->GetAvailableCtor(className);
}

ibCtorAbstractType* ibMetaDataDataProcessor::GetAvailableCtor(const ibClassID& clsid) const
{
	if (ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(clsid) : nullptr))   // hot — O(1)
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