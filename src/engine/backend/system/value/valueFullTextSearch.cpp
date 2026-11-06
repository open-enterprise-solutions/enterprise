////////////////////////////////////////////////////////////////////////////
//	Description : the base's full-text search, as a script reaches it
////////////////////////////////////////////////////////////////////////////

#include "valueFullTextSearch.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"            // ConvertToEnumValue<> is declared in value.h and DEFINED here
#include "backend/metadataConfiguration.h"        // activeMetaData - whose rights the verbs are asked under
#include "backend/metaCollection/partial/commonObject.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/system/systemManagerEnum.h"     // FullTextSearchMode, handed back by GetFullTextSearchMode

namespace {

// THE BASE'S INDEX, so the right is the base's: Data administration. A process with no configuration (a tool, a test)
// has no roles to ask, which is open access.
void RequireDataAdministration(const ibString& method)
{
	const ibMetaDataConfigurationBase* const metaData = activeMetaData;
	if (metaData != nullptr && !metaData->AccessRight_DataAdministration())
		ibBackendCoreException::Error(_("FullTextSearch.%s: the full-text index is the base's, and changing it needs the Data administration right"), method);
}

// An optional Boolean argument: left out - the default; anything but a Boolean - refused with its name, since a
// number or a string standing for a flag is a call written for something else.
bool FlagArgument(ibValue** paParams, const long lSizeArray, long index, const ibString& method, const ibString& name)
{
	if (index >= lSizeArray || paParams[index]->GetType() == ibValueTypes::TYPE_EMPTY)
		return false;
	if (paParams[index]->GetType() != ibValueTypes::TYPE_BOOLEAN)
		ibBackendCoreException::Error(_("FullTextSearch.%s: %s is a Boolean"), method, name);
	return paParams[index]->GetBoolean();
}

} // namespace

// Order MUST match ibValueFullTextSearchManager::Func - the number is the index into the table.
void ibValueFullTextSearchManager_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendFunc(ibT("GetFullTextSearchMode"), 0, ibT("GetFullTextSearchMode() : FullTextSearchMode"));
	helper.AppendProc(ibT("SetFullTextSearchMode"), 1, ibT("SetFullTextSearchMode(mode : FullTextSearchMode)"));
	helper.AppendFunc(ibT("IndexTrue"), 0, ibT("IndexTrue() : boolean"));
	// Two parameters declared, ONE taken: a second argument is a call written for the old shape (merge, portion) and
	// is refused with that said, not read as something else (CallAsProc).
	helper.AppendProc(ibT("UpdateIndex"), 2, ibT("UpdateIndex(portion : boolean = false)"));
	helper.AppendProc(ibT("ClearIndex"), 0, ibT("ClearIndex()"));
	helper.AppendFunc(ibT("UpdateDate"), 0, ibT("UpdateDate() : date"));
	helper.AppendFunc(ibT("CreateList"), 2, ibT("CreateList(searchString : string = \"\", portionSize : number = 20) : FullTextSearchList"));
}

ibValueFullTextSearchManager::ibValueFullTextSearchManager() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibFullTextSearchMode ibValueFullTextSearchManager::GetMode()
{
	return ibFullTextIndex::GetMode();
}

void ibValueFullTextSearchManager::SetMode(ibFullTextSearchMode mode)
{
	RequireDataAdministration(ibT("SetFullTextSearchMode"));
	ibFullTextIndex::SetMode(mode);
}

bool ibValueFullTextSearchManager::IsIndexTrue()
{
	return ibFullTextIndex::IsIndexTrue(activeMetaData);
}

ibFullTextIndex::ibProgress ibValueFullTextSearchManager::UpdateIndex(bool portion)
{
	RequireDataAdministration(ibT("UpdateIndex"));
	return ibFullTextIndex::Update(activeMetaData, portion);
}

ibFullTextIndex::ibProgress ibValueFullTextSearchManager::GetProgress()
{
	return ibFullTextIndex::GetProgress(activeMetaData);
}

void ibValueFullTextSearchManager::ClearIndex()
{
	RequireDataAdministration(ibT("ClearIndex"));
	ibFullTextIndex::Clear();
}

ibDateTime ibValueFullTextSearchManager::GetUpdateDate()
{
	return ibFullTextIndex::GetUpdateDate(activeMetaData);
}

bool ibValueFullTextSearchManager::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enSetFullTextSearchMode:
		if (lSizeArray < 1)
			ibBackendCoreException::Error(_("FullTextSearch.SetFullTextSearchMode: the mode is a FullTextSearchMode value - FullTextSearchMode.Enable or FullTextSearchMode.Disable"));
		SetMode(paParams[0]->ConvertToEnumValue<ibFullTextSearchMode>());
		return true;
	case enUpdateIndex:
		if (lSizeArray > 1 && paParams[1]->GetType() != ibValueTypes::TYPE_EMPTY)
			ibBackendCoreException::Error(_("FullTextSearch.UpdateIndex takes one argument, portion: the index is merged by every write, so there is no merge argument. A call written as UpdateIndex(merge, portion) would read its first argument as portion - write UpdateIndex(portion)"));
		UpdateIndex(FlagArgument(paParams, lSizeArray, 0, ibT("UpdateIndex"), ibT("portion")));
		return true;
	case enClearIndex:
		ClearIndex();
		return true;
	}
	return false;
}

bool ibValueFullTextSearchManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enGetFullTextSearchMode:
		pvarRetValue = ibValue::CreateEnumObject<ibValueEnumFullTextSearchMode>(GetMode());
		return true;
	case enIndexTrue:
		pvarRetValue = IsIndexTrue();
		return true;
	case enUpdateDate:
		pvarRetValue = GetUpdateDate();
		return true;
	case enCreateList: {
		const ibString text = lSizeArray > 0 ? paParams[0]->GetString() : ibString();
		int portionSize = ibValueFullTextSearchList::kDefaultPortionSize;
		if (lSizeArray > 1 && paParams[1]->GetType() != ibValueTypes::TYPE_EMPTY) {
			if (paParams[1]->GetType() != ibValueTypes::TYPE_NUMBER || paParams[1]->GetInteger() < 1)
				ibBackendCoreException::Error(_("FullTextSearch.CreateList: portionSize is a number of records, at least 1"));
			portionSize = paParams[1]->GetInteger();
		}
		pvarRetValue = ibValue(new ibValueFullTextSearchList(text, portionSize));
		return true;
	}
	}
	return false;
}

//**********************************************************************
//*                          The found record                          *
//**********************************************************************

void ibValueFullTextSearchItem_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProp(ibT("Value"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Metadata"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("Presentation"), true, false, wxNOT_FOUND);
}

ibValueFullTextSearchItem::ibValueFullTextSearchItem() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueFullTextSearchItem::ibValueFullTextSearchItem(const ibValue& value, const ibValue& metadata, const ibString& presentation)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_value(value), m_metadata(metadata), m_presentation(presentation)
{
}

bool ibValueFullTextSearchItem::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enValue:
		pvarPropVal = m_value;
		return true;
	case enMetadata:
		pvarPropVal = m_metadata;
		return true;
	case enPresentation:
		pvarPropVal = m_presentation;
		return true;
	}
	return false;
}

//**********************************************************************
//*                          The list                                  *
//**********************************************************************

void ibValueFullTextSearchList_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendProc(ibT("FirstPart"), 0, ibT("FirstPart()"));
	helper.AppendProc(ibT("NextPart"), 0, ibT("NextPart()"));
	helper.AppendProc(ibT("PreviousPart"), 0, ibT("PreviousPart()"));
	helper.AppendFunc(ibT("Count"), 0, ibT("Count() : number"));
	helper.AppendFunc(ibT("TotalCount"), 0, ibT("TotalCount() : number"));
	helper.AppendFunc(ibT("StartIndex"), 0, ibT("StartIndex() : number"));
	helper.AppendFunc(ibT("Get"), 1, ibT("Get(index : number) : FullTextSearchItem"));

	helper.AppendProp(ibT("SearchString"), true, false, wxNOT_FOUND);
	helper.AppendProp(ibT("PortionSize"), true, false, wxNOT_FOUND);
}

ibValueFullTextSearchList::ibValueFullTextSearchList() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibValueFullTextSearchList::ibValueFullTextSearchList(const ibString& searchString, int portionSize)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_searchString(searchString), m_portionSize(portionSize)
{
}

void ibValueFullTextSearchList::Searched() const
{
	if (!m_searched)
		ibBackendCoreException::Error(_("FullTextSearchList: nothing is searched yet - FirstPart searches \"%s\""), m_searchString);
}

void ibValueFullTextSearchList::FirstPart()
{
	m_hits = ibFullTextIndex::Search(activeMetaData, m_searchString);
	m_searched = true;
	m_start = 0;
}

void ibValueFullTextSearchList::NextPart()
{
	Searched();
	if (m_start + m_portionSize >= static_cast<long>(m_hits.size()))
		ibBackendCoreException::Error(_("FullTextSearchList.NextPart: the part shown is the last of %ld records found"), static_cast<long>(m_hits.size()));
	m_start += m_portionSize;
}

void ibValueFullTextSearchList::PreviousPart()
{
	Searched();
	if (m_start == 0)
		ibBackendCoreException::Error(_("FullTextSearchList.PreviousPart: the part shown is the first"));
	m_start = std::max(0L, m_start - m_portionSize);
}

long ibValueFullTextSearchList::Count() const
{
	Searched();
	return std::min<long>(m_portionSize, static_cast<long>(m_hits.size()) - m_start);
}

long ibValueFullTextSearchList::TotalCount() const
{
	Searched();
	return static_cast<long>(m_hits.size());
}

ibValue ibValueFullTextSearchList::Get(long index) const
{
	Searched();
	if (index < 0 || index >= Count())
		ibBackendCoreException::Error(_("FullTextSearchList.Get: the part holds %ld records, so there is none at %ld"), Count(), index);
	const ibFullTextIndex::ibHit& hit = m_hits[static_cast<std::size_t>(m_start + index)];
	ibValuePtr<ibValueReferenceDataObject> reference(ibValueReferenceDataObject::Create(hit.m_metaObject, hit.m_key));
	const ibString presentation = hit.m_metaObject->GetSynonym() + ibT(": ") + reference->GetString();
	return ibValue(new ibValueFullTextSearchItem(ibValue(reference), ibValue(const_cast<ibValueMetaObjectRecordDataMutableRef*>(hit.m_metaObject)), presentation));
}

bool ibValueFullTextSearchList::CallAsProc(const long lMethodNum, ibValue** /*paParams*/, const long /*lSizeArray*/)
{
	switch (lMethodNum) {
	case enFirstPart:
		FirstPart();
		return true;
	case enNextPart:
		NextPart();
		return true;
	case enPreviousPart:
		PreviousPart();
		return true;
	}
	return false;
}

bool ibValueFullTextSearchList::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum) {
	case enCount:
		pvarRetValue = ibValue(ibNumber(Count()));
		return true;
	case enTotalCount:
		pvarRetValue = ibValue(ibNumber(TotalCount()));
		return true;
	case enStartIndex:
		pvarRetValue = ibValue(ibNumber(StartIndex()));
		return true;
	case enGet:
		if (lSizeArray < 1 || paParams[0]->GetType() != ibValueTypes::TYPE_NUMBER)
			ibBackendCoreException::Error(_("FullTextSearchList.Get: the index is a number"));
		pvarRetValue = Get(paParams[0]->GetInteger());
		return true;
	}
	return false;
}

bool ibValueFullTextSearchList::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum) {
	case enSearchString:
		pvarPropVal = m_searchString;
		return true;
	case enPortionSize:
		pvarPropVal = ibValue(ibNumber(m_portionSize));
		return true;
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// SYSTEM, not value types: a script does not create a full-text search, the base has one, and its lists and items are
// what the search hands out. The global context vends the manager as FullTextSearch (globalContextManager.cpp).
SYSTEM_TYPE_REGISTER(ibValueFullTextSearchManager, "FullTextSearchManager", system_to_clsid("MG_FTSM"));
SYSTEM_TYPE_REGISTER(ibValueFullTextSearchList, "FullTextSearchList", system_to_clsid("VL_FTSL"));
SYSTEM_TYPE_REGISTER(ibValueFullTextSearchItem, "FullTextSearchItem", system_to_clsid("VL_FTSI"));
