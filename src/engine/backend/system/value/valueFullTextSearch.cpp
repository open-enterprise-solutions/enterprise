////////////////////////////////////////////////////////////////////////////
//	Description : the base's full-text search, as a script reaches it
////////////////////////////////////////////////////////////////////////////

#include "valueFullTextSearch.h"

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/compiler/enumUnit.h"            // ConvertToEnumValue<> is declared in value.h and DEFINED here
#include "backend/metadataConfiguration.h"        // activeMetaData - whose right the mode is written under
#include "backend/settings/settingsStorage.h"
#include "backend/system/systemManagerEnum.h"     // FullTextSearchMode, handed back by GetFullTextSearchMode
#include "core/serialize/dataBuilder.h"

namespace {

// THE ROW OF THE MODE - one per base, no user: a well-known object key, as the base's locale has one.
const ibGuid& ModeObjectKey()
{
	static const ibGuid key(ibT("66756c6c-7465-7874-0000-000000000001"));
	return key;
}

const ibString kModeField = ibT("mode");

// The base's storage, or a refusal that says why there is none: a mode read from nowhere would be a guess, and a
// mode written to nowhere would be a switch that does nothing.
ibSettingsStorage& Storage()
{
	ibSettingsStorage* const storage = ibApplicationInstance::GetSettingsStorage();
	if (storage == nullptr)
		ibBackendCoreException::Error(_("FullTextSearch: no base is open, so there is no full-text search mode to read or set"));
	return *storage;
}

bool IsMode(std::int32_t number)
{
	return number == ibFullTextSearchMode_Enable || number == ibFullTextSearchMode_Disable;
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
	helper.AppendProc(ibT("UpdateIndex"), 2, ibT("UpdateIndex(merge : boolean = false, portion : boolean = false)"));
	helper.AppendProc(ibT("ClearIndex"), 0, ibT("ClearIndex()"));
	helper.AppendFunc(ibT("UpdateDate"), 0, ibT("UpdateDate() : date"));
}

ibValueFullTextSearchManager::ibValueFullTextSearchManager() : ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true) {}

ibFullTextSearchMode ibValueFullTextSearchManager::GetMode()
{
	ibDataNode row;
	if (!Storage().Restore(ibSettingsKey(ibSettingsCategory::Search, ModeObjectKey()), row))
		return ibFullTextSearchMode_Disable;   // nobody switched it on: the base has no search

	std::int32_t number = 0;
	row.GetValue(kModeField, number);
	if (!IsMode(number))
		ibBackendCoreException::Error(_("FullTextSearch: the base keeps a full-text search mode this build does not know (%d)"), number);
	return static_cast<ibFullTextSearchMode>(number);
}

void ibValueFullTextSearchManager::SetMode(ibFullTextSearchMode mode)
{
	if (!IsMode(mode))
		ibBackendCoreException::Error(_("FullTextSearch.SetFullTextSearchMode: the mode is a FullTextSearchMode value - FullTextSearchMode.Enable or FullTextSearchMode.Disable"));

	// THE BASE'S SWITCH, so the right is the base's: Data administration. A process with no configuration (a tool,
	// a test) has no roles to ask, which is open access.
	const ibMetaDataConfigurationBase* const metaData = activeMetaData;
	if (metaData != nullptr && !metaData->AccessRight_DataAdministration())
		ibBackendCoreException::Error(_("FullTextSearch.SetFullTextSearchMode: changing the full-text search mode needs the Data administration right"));

	ibDataNode row;
	row.SetValue(kModeField, static_cast<std::int32_t>(mode));
	if (!Storage().Save(ibSettingsKey(ibSettingsCategory::Search, ModeObjectKey()), row))
		ibBackendCoreException::Error(_("FullTextSearch.SetFullTextSearchMode: the mode was not saved in the base (the technology journal says why)"));
}

// ⚠ NO INDEX YET (valueFullTextSearch.h): nothing is indexed, so nothing is up to date.
bool ibValueFullTextSearchManager::IsIndexTrue()
{
	return false;
}

void ibValueFullTextSearchManager::UpdateIndex(bool /*merge*/, bool /*portion*/)
{
}

void ibValueFullTextSearchManager::ClearIndex()
{
}

// The moment the index was last brought up to date - never, while there is none.
ibDateTime ibValueFullTextSearchManager::GetUpdateDate()
{
	return ibDateTime();
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
		UpdateIndex(FlagArgument(paParams, lSizeArray, 0, ibT("UpdateIndex"), ibT("merge")),
			FlagArgument(paParams, lSizeArray, 1, ibT("UpdateIndex"), ibT("portion")));
		return true;
	case enClearIndex:
		ClearIndex();
		return true;
	}
	return false;
}

bool ibValueFullTextSearchManager::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** /*paParams*/, const long /*lSizeArray*/)
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
	}
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

// SYSTEM, not a value type: a script does not create a full-text search, the base has one. The global context vends
// it as FullTextSearch (globalContextManager.cpp).
SYSTEM_TYPE_REGISTER(ibValueFullTextSearchManager, "FullTextSearchManager", system_to_clsid("MG_FTSM"));
