#include "metadataDataTool.h"

#include "backend/objCtor.h"
#include "backend/metadataApplication.h"

// (No CreateObject of its own: ibMetaData's asks GetTypeCtor below, which looks here first and then at
// the application — the whole of what a second copy of the factory used to add.)

bool ibMetaDataDataTool::IsRegisterCtor(const ibString& className) const
{
	if (!ibMetaData::IsRegisterCtor(className))
		return activeMetaData->IsRegisterCtor(className);
	return true;
}

bool ibMetaDataDataTool::IsRegisterCtor(const ibString& className, ibCtorObjectType objectType) const
{
	if (!ibMetaData::IsRegisterCtor(className, objectType))
		return activeMetaData->IsRegisterCtor(className);
	return true;
}

bool ibMetaDataDataTool::IsRegisterCtor(const ibString& className, ibCtorObjectType objectType, ibCtorObjectMetaType refType) const
{
	if (!ibMetaData::IsRegisterCtor(className, objectType, refType))
		return activeMetaData->IsRegisterCtor(className, objectType, refType);
	return true;
}

bool ibMetaDataDataTool::IsRegisterCtor(const ibClassID& clsid) const
{
	if (!ibMetaData::IsRegisterCtor(clsid))
		return activeMetaData->IsRegisterCtor(clsid);
	return true;
}

ibClassID ibMetaDataDataTool::GetIDObjectFromString(const ibString& className) const
{
	if (const ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(className) : nullptr))
		return typeCtor->GetClassType();

	return activeMetaData->GetIDObjectFromString(className);
}

ibString ibMetaDataDataTool::GetNameObjectFromID(const ibClassID& clsid, bool upper) const
{
	if (const ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(clsid) : nullptr))
		return upper ? typeCtor->GetClassName().Upper() : typeCtor->GetClassName();

	return activeMetaData->GetNameObjectFromID(clsid, upper);
}

ibCtorMetaValueType* ibMetaDataDataTool::GetTypeCtor(const ibClassID& clsid) const
{
	if (ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(clsid) : nullptr))   // hot — O(1)
		return typeCtor;
	return activeMetaData->GetTypeCtor(clsid);
}

ibCtorMetaValueType* ibMetaDataDataTool::GetTypeCtor(const ibValueMetaObject* metaValue, ibCtorObjectMetaType refType) const
{
	// The id the pair spells, in the data tool's own image first (ibMetaImage::FindCtor — one probe; this was a
	// copy of the walk that stood there), then the application's.
	if (ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(metaValue, refType) : nullptr))
		return typeCtor;
	return activeMetaData->GetTypeCtor(metaValue, refType);
}

ibCtorAbstractType* ibMetaDataDataTool::GetAvailableCtor(const ibString& className) const
{
	if (ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(className) : nullptr))
		return typeCtor;
	return activeMetaData->GetAvailableCtor(className);
}

ibCtorAbstractType* ibMetaDataDataTool::GetAvailableCtor(const ibClassID& clsid) const
{
	if (ibCtorMetaValueType* typeCtor = (m_image ? m_image->FindCtor(clsid) : nullptr))   // hot — O(1)
		return typeCtor;
	return activeMetaData->GetAvailableCtor(clsid);
}

std::vector<ibCtorMetaValueType*> ibMetaDataDataTool::GetListCtorsByType() const
{
	return activeMetaData->GetListCtorsByType();
}

bool ibMetaDataDataTool::GetOwner(ibMetaData*& metaData) const
{
	metaData = activeMetaData;
	return true;
}

std::vector<ibCtorMetaValueType*> ibMetaDataDataTool::GetListCtorsByType(const ibClassID& clsid, ibCtorObjectMetaType refType) const
{
	return activeMetaData->GetListCtorsByType(clsid, refType);
}

std::vector<ibCtorMetaValueType*> ibMetaDataDataTool::GetListCtorsByType(ibCtorObjectMetaType refType) const
{
	return activeMetaData->GetListCtorsByType(refType);
}