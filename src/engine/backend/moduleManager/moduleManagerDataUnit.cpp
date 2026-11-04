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
#include "metaObjectCollection.h"
#include "backend/metaData.h"

#include "backend/query/queryable.h"                // ibBackendQueryableHolder
#include "backend/query/tempTableQueryable.h"       // ibTempTableQueryable — Data.From wrapper
#include "backend/system/value/valueTable.h"        // ibValueModelTable — the From argument
#include "backend/system/value/valueQueryable.h"    // ibValueQueryable — the vended leaf
#include "backend/backend_exception.h"              // ibBackendCoreException — From argument validation

#include <iterator>   // std::size — the table of kinds

namespace {

// The source an object vends, or nullptr: the kind member exists, and this object of it is no data source.
const ibBackendQueryable* QueryableOf(const ibValueMetaObject* object)
{
	const ibBackendQueryableHolder* const holder = dynamic_cast<const ibBackendQueryableHolder*>(object);
	return holder != nullptr ? holder->GetQueryable() : nullptr;
}

} // namespace

// Data.Catalogs, … — the data source of each object of one kind that vends one (metaObjectCollection.h): the SAME
// ibBackendQueryable the text query language reads through. Lazy by contract: vending a source reads NOTHING.
class ibValueQueryableCollection : public ibValueMetaObjectCollection {
public:

	ibValueQueryableCollection() : ibValueMetaObjectCollection(nullptr) {}
	ibValueQueryableCollection(const ibClassID& clsid, const ibString& kindName, ibMetaData* metaData)
		: ibValueMetaObjectCollection(metaData), m_kindName(kindName) {
		if (m_metaData == nullptr)
			return;
		for (const auto object : m_metaData->GetAnyArrayObject(clsid))
			if (QueryableOf(object) != nullptr)
				m_listValue.push_back(object);
	}

protected:

	// The source is named by its script path ("Catalogs.Goods"), for a watch and a diagnostic.
	virtual ibValue ElementValue(ibValueMetaObject* const& object) const override {
		const ibBackendQueryable* const queryable = object != nullptr ? QueryableOf(object) : nullptr;
		if (queryable == nullptr)
			return ibValue();
		return new ibValueQueryable(queryable, m_kindName + ibT(".") + object->GetName());
	}

private:

	ibString m_kindName;   // the member it was read through — "Catalogs"
};

namespace {

// THE QUERYABLE KINDS, one per member, in the members' order — mirrors the query language's source coverage
// (queryableFactory): data-reference records, registers, constants. A member's number is its position here, so
// FillMembers and GetPropVal read one table.
struct ibQueryableKind {
	const ibChar* m_name;
	ibClassID     m_clsid;
};

constexpr ibQueryableKind s_dataKinds[] = {
	{ ibT("Constants"),                   g_metaConstantCLSID },
	{ ibT("Catalogs"),                    g_metaCatalogCLSID },
	{ ibT("Documents"),                   g_metaDocumentCLSID },
	{ ibT("Enumerations"),                g_metaEnumerationCLSID },
	{ ibT("InformationRegisters"),        g_metaInformationRegisterCLSID },
	{ ibT("AccumulationRegisters"),       g_metaAccumulationRegisterCLSID },
	{ ibT("ChartsOfCharacteristicTypes"), g_metaChartOfCharacteristicTypesCLSID },
	{ ibT("ChartsOfAccounts"),            g_metaChartOfAccountsCLSID },
	{ ibT("AccountingRegisters"),         g_metaAccountingRegisterCLSID },
	{ ibT("ChartsOfCalculationTypes"),    g_metaChartOfCalculationTypesCLSID },
	{ ibT("CalculationRegisters"),        g_metaCalculationRegisterCLSID },
	{ ibT("Sequences"),                   g_metaSequenceCLSID },
};

} // namespace

ibValueModuleManager::ibValueDataUnit::ibValueDataUnit(ibMetaData* metaData) :
	ibValueDynamicMembers(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData)
{
	m_members.Bind(this, &ibValueDataUnit::FillMembers);
}

ibValueModuleManager::ibValueDataUnit::~ibValueDataUnit()
{
}

void ibValueModuleManager::ibValueDataUnit::FillMembers(ibMemberTable& helper) const
{
	// Data.From(valueTable) — an in-memory value table as a Queryable source.
	// ⚠ THE COUNT IS DECLARED, NOT ONLY THE SIGNATURE. The text says `From(valueTable)` and the
	// implementation reads paParams[0] — but the compiler counts what was REGISTERED, and zero was
	// registered: `Data.From(table)` came back "too many parameters passed to 'From'", a verb
	// documented, implemented and unreachable (2026-09-04; the third of this exact shape in one
	// session, after Query.Execute and QueryResult.Select).
	helper.AppendFunc("From", 1, "From(valueTable)");

	// clsid narrowed into the (long) tag slot on purpose — GetPropVal keys by ordinal and re-supplies
	// the clsid, so only the tag identity matters; static_cast makes the std::uint64_t->long explicit (no C4305/C4309).
	for (const ibQueryableKind& kind : s_dataKinds)
		helper.AppendProp(kind.m_name, true, false, static_cast<long>(kind.m_clsid));
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
			std::make_shared<ibTempTableQueryable>(*paParams[0], m_metaData), ibT("From"));
		return true;
	}
	}
	return false;
}

bool ibValueModuleManager::ibValueDataUnit::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	if (lPropNum < 0 || lPropNum >= (long)std::size(s_dataKinds))
		return false;
	pvarPropVal = new ibValueQueryableCollection(s_dataKinds[lPropNum].m_clsid, s_dataKinds[lPropNum].m_name, m_metaData);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueQueryableCollection, "QueryableCollection", system_to_clsid("MG_DTCOL"));
