////////////////////////////////////////////////////////////////////////////
//	Description : "Data" global — the queryable-source root (L4-2). The exact
//	              mirror of the "Metadata" unit (moduleManagerMetadataUnit.cpp):
//	              the same kind-namespace member shape, but the leaves are
//	              ibValueQueryable values — the SAME ibBackendQueryable the text
//	              query language reads through (one source registry, one world).
//	              Only the QUERYABLE kinds are exposed (records with a
//	              data-reference, registers, constants); modules / forms /
//	              templates / reports / data processors are not data sources.
//	              Lazy by contract: vending a Queryable reads NOTHING.
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "backend/metaData.h"

#include "backend/query/queryable.h"                // ibBackendQueryableHolder
#include "backend/query/tempTableQueryable.h"       // ibTempTableQueryable — Data.From wrapper
#include "backend/system/value/valueMap.h"          // ibValueStructure
#include "backend/system/value/valueTable.h"        // ibValueModelTable — the From argument
#include "backend/system/value/valueQueryable.h"    // ibValueQueryable — the vended leaf
#include "backend/backend_exception.h"              // ibBackendCoreException — From argument validation

ibValueModuleManager::ibValueDataUnit::ibValueDataUnit(ibMetaData* metaData) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData)
{
	m_members.Bind(this, &ibValueDataUnit::FillMembers);
}

ibValueModuleManager::ibValueDataUnit::~ibValueDataUnit()
{
}

namespace {

enum
{
	enDataConstants = 0,
	enDataCatalogs,
	enDataDocuments,
	enDataEnumerations,
	enDataInformationRegisters,
	enDataAccumulationRegisters,
	enDataChartsOfCharacteristicTypes,
	enDataChartsOfAccounts,
	enDataAccountingRegisters,
};

// One kind namespace: Name -> Queryable for every metaobject of the kind that
// vends a queryable (ibBackendQueryableHolder). The source name mirrors the
// canonical script path ("Catalogs.Номенклатура") for watch / diagnostics.
ibValueStructure* BuildKindNamespace(ibMetaData* metaData, const ibClassID& clsid,
                                     const wxString& kindName)
{
	ibValueStructure* names = new ibValueStructure();
	if (metaData == nullptr)
		return names;
	for (const auto object : metaData->GetAnyArrayObject(clsid)) {
		const ibBackendQueryableHolder* holder =
			dynamic_cast<const ibBackendQueryableHolder*>(object);
		const ibBackendQueryable* queryable =
			holder != nullptr ? holder->GetQueryable() : nullptr;
		if (queryable == nullptr)
			continue;   // the kind member exists but this object vends no source
		names->Insert(object->GetName(),
			new ibValueQueryable(queryable, kindName + wxT(".") + object->GetName()));
	}
	return names;
}

} // namespace

void ibValueModuleManager::ibValueDataUnit::FillMembers(ibMemberTable& helper) const
{
	// Data.From(valueTable) — an in-memory value table as a Queryable source.
	helper.AppendFunc("From", "From(valueTable)");

	// The QUERYABLE kinds only — mirrors the query language's source coverage
	// (queryableFactory): data-reference records, registers, constants.
	// clsid narrowed into the (long) tag slot on purpose — GetPropVal keys by ordinal and re-supplies
	// the clsid, so only the tag identity matters; static_cast makes the u64->long explicit (no C4305/C4309).
	helper.AppendProp("Constants", true, false, static_cast<long>(g_metaConstantCLSID));
	helper.AppendProp("Catalogs", true, false, static_cast<long>(g_metaCatalogCLSID));
	helper.AppendProp("Documents", true, false, static_cast<long>(g_metaDocumentCLSID));
	helper.AppendProp("Enumerations", true, false, static_cast<long>(g_metaEnumerationCLSID));
	helper.AppendProp("InformationRegisters", true, false, static_cast<long>(g_metaInformationRegisterCLSID));
	helper.AppendProp("AccumulationRegisters", true, false, static_cast<long>(g_metaAccumulationRegisterCLSID));
	helper.AppendProp("ChartsOfCharacteristicTypes", true, false, static_cast<long>(g_metaChartOfCharacteristicTypesCLSID));
	helper.AppendProp("ChartsOfAccounts", true, false, static_cast<long>(g_metaChartOfAccountsCLSID));
	helper.AppendProp("AccountingRegisters", true, false, static_cast<long>(g_metaAccountingRegisterCLSID));
}

bool ibValueModuleManager::ibValueDataUnit::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueModuleManager::ibValueDataUnit::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue,
                                                       ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case 0: {   // From(valueTable)
		if (lSizeArray < 1 || paParams == nullptr || paParams[0] == nullptr)
			ibBackendCoreException::Error(_("Data.From: a value table argument is required"));
		ibValueModelTable* rows = nullptr;
		if (!paParams[0]->ConvertToValue(rows) || rows == nullptr)
			ibBackendCoreException::Error(_("Data.From: the argument must be a value table"));
		// The wrapper copies the table reference (the held ibValue keeps it alive);
		// the Queryable OWNS the wrapper and shares it across chain links.
		pvarRetValue = new ibValueQueryable(
			std::make_shared<ibTempTableQueryable>(*paParams[0]), wxT("From"));
		return true;
	}
	}
	return false;
}

bool ibValueModuleManager::ibValueDataUnit::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	switch (lPropNum)
	{
	case enDataConstants:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaConstantCLSID, wxT("Constants"));
		return true;
	case enDataCatalogs:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaCatalogCLSID, wxT("Catalogs"));
		return true;
	case enDataDocuments:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaDocumentCLSID, wxT("Documents"));
		return true;
	case enDataEnumerations:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaEnumerationCLSID, wxT("Enumerations"));
		return true;
	case enDataInformationRegisters:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaInformationRegisterCLSID, wxT("InformationRegisters"));
		return true;
	case enDataAccumulationRegisters:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaAccumulationRegisterCLSID, wxT("AccumulationRegisters"));
		return true;
	case enDataChartsOfCharacteristicTypes:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaChartOfCharacteristicTypesCLSID, wxT("ChartsOfCharacteristicTypes"));
		return true;
	case enDataChartsOfAccounts:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaChartOfAccountsCLSID, wxT("ChartsOfAccounts"));
		return true;
	case enDataAccountingRegisters:
		pvarPropVal = BuildKindNamespace(m_metaData, g_metaAccountingRegisterCLSID, wxT("AccountingRegisters"));
		return true;
	}
	return false;
}
