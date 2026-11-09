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
#include "backend/query/queryRamTable.h"                        // ibQueryRamTable — the folded balance, laid down as a table
#include "core/stringTokenizer.h"                                  // the comma lists Balance is cut to

#include <algorithm>

namespace {

// The fields a comma list states, each found among `declared` whatever the case it was written in; a name that is not
// there raises, naming it — a misspelt resource would otherwise read as a balance of nothing. Left out (an empty
// value), the list states all of them; stated — even as "" — exactly those.
template <typename TField>
std::vector<const TField*> NamedAmong(const ibValue& list, const std::vector<TField*>& declared, const ibString& refusal)
{
	std::vector<const TField*> named;
	if (list.GetType() == ibValueTypes::TYPE_EMPTY) {
		for (const TField* field : declared)
			if (field != nullptr)
				named.push_back(field);
		return named;
	}
	ibStringTokenizer tokens(list.GetString(), ibT(","));
	while (tokens.HasMoreTokens()) {
		const ibString name = tokens.GetNextToken().Trim(true).Trim(false);
		if (name.IsEmpty())
			continue;
		const auto found = std::find_if(declared.begin(), declared.end(),
			[&name](const TField* field) { return field != nullptr && stringUtils::IsSameAs(name, field->GetName()); });
		if (found == declared.end())
			ibBackendCoreException::Error(refusal, name);
		named.push_back(*found);
	}
	return named;
}

// A column of the reading by the name it publishes it under: a dimension by its own, a resource's figure as
// `<Resource>Balance`.
const ibBackendQueryColumn* PublishedAs(const ibBackendQueryable& reading, const ibString& name)
{
	for (const ibBackendQueryColumn* column : reading.GetQueryColumns())
		if (column != nullptr && column->GetName() == name)
			return column;
	return nullptr;
}

} // namespace

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
ibValue ibValueManagerDataObjectAccumulationRegister::Balance(const ibValue& cPeriod, const ibValue& cFilter,
                                                              const ibValue& cDimensions, const ibValue& cResources)
{
	ibRequireOpenBase();

	// What the reading is cut to is read FIRST: a name that is not the register's refuses before the base is asked.
	const std::vector<const ibValueMetaObjectDimension*> dimensions = NamedAmong(cDimensions,
		m_metaObject->GetDimensionArrayObject(), _("Balance: '%s' is not a dimension of the register"));
	const std::vector<const ibValueMetaObjectResource*> resources = NamedAmong(cResources,
		m_metaObject->GetResourceArrayObject(), _("Balance: '%s' is not a resource of the register"));

	// ⭐ FOLDED BY THE DOOR, AS A QUERY FOLDS IT (#409): the dimensions kept are the groups, and each resource kept is
	// summed under its own name. The value table holds nothing but the balance, so `<Resource>Balance` — one figure
	// among several in a query — is not what its column is called here. Nothing is folded or renamed after the read.
	// The Structure a script passes becomes the condition — the same converter the query door uses.
	ibBalanceQueryable balance(m_metaObject, cPeriod, ibRegFilterPredicate(m_metaObject, cFilter));
	ibDataQueryBuilder b;
	b.From(&balance);
	ibQueryRamTable rows;
	std::vector<const ibBackendQueryColumn*> groups;
	for (const ibValueMetaObjectDimension* dimension : dimensions) {
		const ibBackendQueryColumn* const column = PublishedAs(balance, dimension->GetName());
		if (column == nullptr)
			continue;
		b.GroupBy(column);
		groups.push_back(column);
		rows.AddColumn(static_cast<ibMetaID>(groups.size()), column->GetName(), column->GetTypeDesc(), column->GetSynonym());
	}
	std::vector<ibString> sums;
	for (const ibValueMetaObjectResource* resource : resources) {
		const ibBackendQueryColumn* const column = PublishedAs(balance, resource->GetName() + ibRegFigure::Balance);
		if (column == nullptr)
			continue;
		b.Aggregate(ibDataQueryBuilder::AggregateFn::Sum, column, resource->GetName());
		sums.push_back(resource->GetName());
		rows.AddColumn(static_cast<ibMetaID>(groups.size() + sums.size()), resource->GetName(), column->GetTypeDesc(),
			resource->GetSynonym());
	}

	// Column i is the group i, then the sum i — the same two reads a query's result is walked by.
	ibDataQueryResult selection = b.SelectAggregate();
	while (selection.Next()) {
		const long at = rows.AppendRow();
		for (size_t i = 0; i < groups.size(); ++i)
			rows.SetCell(at, static_cast<ibMetaID>(i + 1), selection.GetValue(groups[i]));
		for (size_t i = 0; i < sums.size(); ++i)
			rows.SetCell(at, static_cast<ibMetaID>(groups.size() + i + 1), selection.GetColumn(sums[i]));
	}
	return std::move(rows).ToValueTable();
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

