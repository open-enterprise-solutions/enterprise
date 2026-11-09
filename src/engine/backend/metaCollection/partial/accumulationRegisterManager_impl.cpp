////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : accumlation manager
////////////////////////////////////////////////////////////////////////////

#include "accumulationRegister.h"
#include "accumulationRegisterManager.h"

#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueTable.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"        // L2-1 — structured IR for the live aggregates
#include "backend/databaseLayer/databaseMaterializeBuilder.h"  // L2-2 — RenderMaterializedRead (the materialised readings)
#include "backend/query/dataQueryBuilder.h"                     // L3 door — From(balance/turnover queryable).Select()
#include "backend/query/dbTableProvider.h"                      // ibDbTableProvider::GetValueAttribute — the DB value-assembly
#include "backend/metaCollection/partial/registerQueryLowering.h"   // ibRegFieldsOf / ibRegValueField / ibRegCompositeIR
#include "backend/appData.h"
#include "backend/session/session.h"
#include "core/stringTokenizer.h"                                  // the comma lists Balance is cut to

#include <algorithm>


// ⭐⭐ THE COLUMNS COME FROM THE SOURCE — `ibRegSelectionToTable` (registerQueryLowering.h).
//
// Both readings here used to list their own: the dimensions, then a column per resource per figure,
// with the caption built on the spot (`GetSynonym() + " " + _("Balance")`). That was the THIRD
// spelling of names the view and the reading already agreed on, and the drift it produced is on the
// record — `Resource1_Turnover` against `Resource1Turnover`, two names for one number, each
// compiling. The surface now carries the caption too, so asking it for its columns loses nothing and
// gains the one thing this file had that the accounting register's copy did not.
//
// The runtime entry stays thin, exactly like the slice: build the call-scoped queryable (filters in
// the ctor) and read it through L3's From().Select().
namespace {

// The names a comma list states, each found among `declared` and given back in its declared spelling; a name that
// is not there raises, naming it — a misspelt resource would otherwise read as a balance of nothing.
template <typename TField>
std::vector<ibString> NamedAmong(const ibString& list, const std::vector<TField*>& declared, const ibString& refusal)
{
	std::vector<ibString> names;
	ibStringTokenizer tokens(list, ibT(","));
	while (tokens.HasMoreTokens()) {
		const ibString name = tokens.GetNextToken().Trim(true).Trim(false);
		if (name.IsEmpty())
			continue;
		const auto found = std::find_if(declared.begin(), declared.end(),
			[&name](const TField* field) { return field != nullptr && stringUtils::IsSameAs(name, field->GetName()); });
		if (found == declared.end())
			ibBackendCoreException::Error(refusal, name);
		names.push_back((*found)->GetName());
	}
	return names;
}

template <typename TField>
std::vector<ibString> AllOf(const std::vector<TField*>& declared)
{
	std::vector<ibString> names;
	for (const TField* field : declared)
		if (field != nullptr)
			names.push_back(field->GetName());
	return names;
}

ibString CommaList(const std::vector<ibString>& names)
{
	ibString list;
	for (const ibString& name : names)
		list += (list.IsEmpty() ? ibString() : ibString(ibT(","))) + name;
	return list;
}

} // namespace

ibValue ibValueManagerDataObjectAccumulationRegister::Balance(const ibValue& cPeriod, const ibValue& cFilter,
                                                              const ibValue& cDimensions, const ibValue& cResources)
{
	ibRequireOpenBase();

	// What the reading is cut to is read FIRST: a name that is not the register's refuses before the base is asked.
	// Left out (an empty value) is all of them; stated — even as "" — is exactly those.
	const auto dimensions = m_metaObject->GetDimensionArrayObject();
	const auto resources  = m_metaObject->GetResourceArrayObject();
	const bool cut = cDimensions.GetType() != ibValueTypes::TYPE_EMPTY || cResources.GetType() != ibValueTypes::TYPE_EMPTY;
	const std::vector<ibString> keptDimensions = cDimensions.GetType() != ibValueTypes::TYPE_EMPTY
		? NamedAmong(cDimensions.GetString(), dimensions, _("Balance: '%s' is not a dimension of the register"))
		: AllOf(dimensions);
	const std::vector<ibString> keptResources = cResources.GetType() != ibValueTypes::TYPE_EMPTY
		? NamedAmong(cResources.GetString(), resources, _("Balance: '%s' is not a resource of the register"))
		: AllOf(resources);

	// The Structure a script passes becomes the condition here — the same converter the query door uses.
	ibBalanceQueryable balance(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryResult selection = ibDataQueryBuilder().From(&balance).Execute(ibReadPageRequest{});
	ibValue result = ibRegSelectionToTable(selection, &balance);

	ibValueModelTable* table = nullptr;
	if (!result.ConvertToValue(table) || table == nullptr)
		return result;

	// A resource's column is named after the resource (#409). The virtual table's `<Resource>Balance` names one
	// figure among several in a query; the value table holds nothing but the balance.
	if (ibValueModel::ibValueModelColumnCollection* const columns = table->GetColumnCollection()) {
		for (const ibValueMetaObjectResource* resource : resources) {
			if (resource == nullptr)
				continue;
			if (auto* const column = columns->GetColumnByName(resource->GetName() + ibRegFigure::Balance))
				column->SetColumnName(resource->GetName());
		}
	}

	if (cut)
		table->GroupBy(CommaList(keptDimensions), CommaList(keptResources));
	return result;
}


ibValue ibValueManagerDataObjectAccumulationRegister::Turnovers(const ibValue& cBeginOfPeriod, const ibValue& cEndOfPeriod,
                                                                const ibValue& cFilter, const ibValue& cPeriodicity)
{
	ibRequireOpenBase();

	// The periodicity is read the way the QUERY side reads it — one function, so a word written in a
	// script and the same word written in a query mean the same thing.
	const ibRegFold fold = ibReadRegisterFold(cPeriodicity);

	ibTurnoverQueryable turnover(m_metaObject, cBeginOfPeriod, cEndOfPeriod, ibRegFilterPredicate(m_metaObject, cFilter), fold);
	ibDataQueryResult selection = ibDataQueryBuilder().From(&turnover).Execute(ibReadPageRequest{});
	// ⚠ AND THE PERIODICITY IS WHY THE LIST COULD NOT STAY HAND-WRITTEN. Asked for months, this
	// reading publishes a `Period` column that the old list did not know about at all — it enumerated
	// dimensions and figures and nothing else. The shape knows, because the shape is what produced it.
	return ibRegSelectionToTable(selection, &turnover);
}

