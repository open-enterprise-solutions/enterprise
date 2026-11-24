////////////////////////////////////////////////////////////////////////////
//	Description : "Data" global — the queryable-source root (L4-2). The exact
//	              mirror of the "Metadata" unit (moduleManagerMetadataUnit.cpp):
//	              the same kind-namespace member shape, but the leaves are
//	              ibValueQueryable values — the SAME ibBackendQueryable the text
//	              query language reads through (one source registry, one world).
//	              Only the QUERYABLE kinds are exposed (records with a
//	              data-reference, registers, constants); modules / forms /
//	              templates / reports / data tools are not data sources.
//	              After them, Platform: the sources the platform declares in
//	              every application (Data.Platform.Journal.Events).
//	              Lazy by contract: vending a Queryable reads NOTHING.
////////////////////////////////////////////////////////////////////////////

#include "moduleManager.h"
#include "metaObjectCollection.h"
#include "backend/metaData.h"

#include "backend/query/queryable.h"                // ibBackendQueryableHolder
#include "backend/query/tempTableQueryable.h"       // ibTempTableQueryable — Data.From wrapper
#include "backend/query/queryableFactory.h"         // ibQueryableFactory — Data.From by a source's name
#include "backend/system/value/valueTable.h"        // ibValueModelTable — the From argument
#include "backend/system/value/valueQueryable.h"    // ibValueQueryable — the vended leaf
#include "backend/backend_exception.h"              // ibBackendCoreException — From argument validation
#include "backend/metaCollection/metaGroups.h"      // ibMetaGroupOrder — the kinds in the order an application is read

#include <algorithm>  // std::find — a namespace named once; std::stable_sort — the kinds in their order
#include <vector>

namespace {

// The source an object vends, or nullptr: the kind member exists, and this object of it is no data source.
std::shared_ptr<const ibBackendQueryable> QueryableOf(const ibValueMetaObject* object)
{
	const ibBackendQueryableHolder* const holder = dynamic_cast<const ibBackendQueryableHolder*>(object);
	return holder != nullptr ? holder->GetQueryable() : nullptr;
}

// A SOURCE NAMED AS THE QUERY LANGUAGE NAMES IT — "Journal.Events", "AccumulationRegister.Stock.Turnovers" — made
// from these arguments by the descriptor the application's factory finds: the one a query text's FROM is read
// through, so a source a provider adds (ibPlatformSourceRegister) is reachable here with no line of its own.
std::shared_ptr<const ibBackendQueryable> QueryableNamed(const ibMetaData* metaData, const ibString& name,
                                                         ibValue** paParams, long lSizeArray)
{
	ibQueryableFactory* const factory = metaData != nullptr ? metaData->GetSourceFactory() : nullptr;
	ibQueryableSourceDescriptor* const descriptor = factory != nullptr
		? factory->FindDescriptor(name.BeforeFirst(ibT('.')), name.AfterFirst(ibT('.'))) : nullptr;
	const std::shared_ptr<const ibBackendQueryable> source = descriptor != nullptr
		? descriptor->CreateQueryable(paParams, lSizeArray) : nullptr;
	if (source == nullptr)
		ibBackendCoreException::Error(_("Data.From: there is no source '%s' - name it as a query does, e.g. Journal.Events or AccumulationRegister.Stock.Turnovers"), name);
	return source;
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
		const std::shared_ptr<const ibBackendQueryable> queryable = object != nullptr ? QueryableOf(object) : nullptr;
		if (queryable == nullptr)
			return ibValue();
		return new ibValueQueryable(queryable, m_kindName + ibT(".") + object->GetName());
	}

private:

	ibString m_kindName;   // the member it was read through — "Catalogs"
};

// Data.Platform, Data.Platform.Journal — the sources the PLATFORM declares in every application
// (ibPlatformSourceRegister), named as a query names them: a namespace, then a source in it. `Data.Platform.Journal.Events`
// is `FROM Journal.Events`, all of it; `Data.From("Journal.Events", Begin, End)` reads it with its arguments. ONE CLASS,
// TWO LEVELS: with no namespace it holds the namespaces (`Journal`), with one the names of its sources (`Events`). It
// holds NAMES and makes a source when one is read, through the road Data.From takes (QueryableNamed): reads nothing.
// Count and Get(index | name), as the metaobject collections beside it.
class ibValuePlatformSourceCollection : public ibValueCollection<ibString, &ibValueMetaObjectCollection_BindNames> {
public:

	ibValuePlatformSourceCollection() {}
	ibValuePlatformSourceCollection(const ibMetaData* metaData, const ibString& ns)
		: ibValueCollection(ibValueTypes::TYPE_VALUE, true), m_metaData(metaData), m_namespace(ns) {
		const ibQueryableFactory* const factory = m_metaData != nullptr ? m_metaData->GetSourceFactory() : nullptr;
		if (factory == nullptr)
			return;
		for (const ibQueryableSourceDescriptor* source : factory->GetDescriptors(ibQueryableSourceKind::Platform)) {
			if (!m_namespace.IsEmpty() && !stringUtils::IsSameAs(source->GetNamespace(), m_namespace))
				continue;
			const ibString name = m_namespace.IsEmpty() ? source->GetNamespace() : source->GetName();
			if (std::find(m_listValue.begin(), m_listValue.end(), name) == m_listValue.end())
				m_listValue.push_back(name);
		}
	}

	// The names, read as a metaobject collection reads its objects' (metaObjectCollection.cpp): case does not matter.
	virtual long FindProp(const ibString& strPropName) const override {
		for (size_t at = 0; at < m_listValue.size(); ++at)
			if (stringUtils::IsSameAs(m_listValue[at], strPropName))
				return (long)at;
		return ibNOT_FOUND;
	}
	virtual long GetNProps() const override { return (long)m_listValue.size(); }
	virtual const ibString& GetPropName(const long lPropNum) const override {
		static const ibString s_absent;
		return lPropNum >= 0 && lPropNum < GetNProps() ? m_listValue[lPropNum] : s_absent;
	}
	virtual bool GetPropVal(const long lPropNum, ibValue& pvarPropVal) override {
		if (lPropNum < 0 || lPropNum >= GetNProps())
			return false;
		pvarPropVal = ElementValue(m_listValue[lPropNum]);
		return true;
	}
	virtual bool IsPropReadable(const long lPropNum) const override { return lPropNum >= 0 && lPropNum < GetNProps(); }
	virtual bool IsPropWritable(const long /*lPropNum*/) const override { return false; }

	// [name] as the dot reads; [position] as every collection does.
	virtual bool GetAt(const ibValue& varKeyValue, ibValue& pvarValue) override {
		if (varKeyValue.GetType() != ibValueTypes::TYPE_STRING)
			return ibValueCollection::GetAt(varKeyValue, pvarValue);
		const long at = FindProp(varKeyValue.GetString());
		if (at == ibNOT_FOUND)
			ibBackendCoreException::Error(_("Key '%s' not found!"), varKeyValue.GetString());
		pvarValue = ElementValue(m_listValue[at]);
		return true;
	}

protected:

	virtual ibValue ElementValue(const ibString& name) const override {
		// No name is the editor's sample of an element (For Each): what it is is known only once it is one.
		if (name.IsEmpty())
			return ibValue();
		if (m_namespace.IsEmpty())
			return new ibValuePlatformSourceCollection(m_metaData, name);
		const ibString source = m_namespace + ibT(".") + name;
		return new ibValueQueryable(QueryableNamed(m_metaData, source, nullptr, 0), source);
	}

private:

	const ibMetaData* m_metaData = nullptr;
	ibString m_namespace;   // empty: the namespaces themselves
};

namespace {

// THE QUERYABLE KINDS, one per member — mirrors the query language's source coverage (queryableFactory): data-reference
// records, registers, constants. Which kinds, not their order: the members stand as an application is read (DataKinds).
struct ibQueryableKind {
	const ibChar* m_name;
	ibClassID     m_clsid;
};

constexpr ibQueryableKind s_dataKinds[] = {
	{ ibT("Constants"),                   g_metaConstantCLSID },
	{ ibT("Catalogs"),                    g_metaCatalogCLSID },
	{ ibT("Documents"),                   g_metaDocumentCLSID },
	{ ibT("DocumentJournals"),            g_metaDocumentJournalCLSID },
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

// …in the order an application is read (ibMetaGroupOrder) — the application tree's, the query constructor's. A
// member's number is its position here, so FillMembers and GetPropVal read one list.
const std::vector<const ibQueryableKind*>& DataKinds()
{
	static const std::vector<const ibQueryableKind*> ordered = [] {
		std::vector<const ibQueryableKind*> kinds;
		for (const ibQueryableKind& kind : s_dataKinds)
			kinds.push_back(&kind);
		std::stable_sort(kinds.begin(), kinds.end(), [](const ibQueryableKind* a, const ibQueryableKind* b) {
			return ibMetaGroupOrder(a->m_clsid) < ibMetaGroupOrder(b->m_clsid);
		});
		return kinds;
	}();
	return ordered;
}

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
	// Data.From(sourceName, arguments…) — a source named as a query names it, with its arguments:
	// `Data.From("Journal.Events", Begin, End)`. As many arguments as it is given (ibNOT_FOUND), at least one.
	helper.AppendFunc("From", ibNOT_FOUND, "From(valueTable) / From(sourceName, arguments...)");

	// clsid narrowed into the (long) tag slot on purpose — GetPropVal keys by ordinal and re-supplies
	// the clsid, so only the tag identity matters; static_cast makes the std::uint64_t->long explicit (no C4305/C4309).
	for (const ibQueryableKind* kind : DataKinds())
		helper.AppendProp(kind->m_name, true, false, static_cast<long>(kind->m_clsid));
	// …and after them, what the platform declares (ibValuePlatformSourceCollection) — the member past the kinds' table.
	helper.AppendProp(ibT("Platform"), true, false, ibNOT_FOUND);
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
	case 0: {   // From(valueTable) | From(sourceName, arguments…)
		if (lSizeArray < 1 || paParams == nullptr || paParams[0] == nullptr)
			ibBackendCoreException::Error(_("Data.From: a value table or a source name is required"));
		if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
			const ibString name = paParams[0]->GetString();
			pvarRetValue = new ibValueQueryable(QueryableNamed(m_metaData, name, paParams + 1, lSizeArray - 1), name);
			return true;
		}
		ibValueModelTable* rows = nullptr;
		if (!paParams[0]->ConvertToValue(rows) || rows == nullptr)
			ibBackendCoreException::Error(_("Data.From: the argument must be a value table"));
		// The wrapper copies the table reference (the held ibValue keeps it alive);
		// the Queryable holds the wrapper and shares it across chain links.
		pvarRetValue = new ibValueQueryable(
			std::make_shared<ibTempTableQueryable>(*paParams[0], m_metaData), ibT("From"));
		return true;
	}
	}
	return false;
}

bool ibValueModuleManager::ibValueDataUnit::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	const std::vector<const ibQueryableKind*>& kinds = DataKinds();
	if (lPropNum == (long)kinds.size()) {   // Platform
		pvarPropVal = new ibValuePlatformSourceCollection(m_metaData, ibString());
		return true;
	}
	if (lPropNum < 0 || lPropNum >= (long)kinds.size())
		return false;
	pvarPropVal = new ibValueQueryableCollection(kinds[lPropNum]->m_clsid, kinds[lPropNum]->m_name, m_metaData);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueQueryableCollection, "QueryableCollection", system_to_clsid("MG_DTCOL"));
SYSTEM_TYPE_REGISTER(ibValuePlatformSourceCollection, "PlatformSourceCollection", system_to_clsid("MG_DTPLT"));
