////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of accounts - the STRUCTURE it declares (ContributeTables)
//
//	Everything about this metaobject's physical shape lives here and nowhere else: the tables it
//	declares into a schema snapshot, and the rules those tables carry into the restructuring. The
//	metadata file next door keeps the lifecycle events and the properties; a reader looking for
//	"what does this become in the database" has one file to open.
////////////////////////////////////////////////////////////////////////////

#include "chartOfAccounts.h"
#include "backend/metaData.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // the L2 door - the "who already holds too many" count
#include "backend/metadataSerialization.h"                // ibDeserializeValue - a declared kind, read with this configuration
#include "backend/query/columnLayout.h"                   // ibOwnerRefField - the section's owner column
#include "backend/query/schemaSnapshot.h"                 // ibSchemaTable::m_beforeChange - the rule the declaration carries
#include "backend/restructureInfo.h"                      // ibRestructureInfo - where that rule states its reason
#include "backend/system/value/composition/valueComposerField.h"   // ibMaterializeCompositionValue - a declared kind, made live

// THE DECLARATION CARRIES ITS OWN RULE. The base contributes this chart's table and its analytics-kinds
// section; what the base cannot know is that the section's ROW COUNT PER ACCOUNT is bounded by a number
// declared on the chart. So the rule is attached here, while the change tree is being formed, and the
// differ asks it before it touches that table (schemaSnapshot.cpp).
//
// LOWERING THE CEILING IS NOT A SETTING, IT IS A DELETION: every account already holding more kinds than
// the new number would have to lose some, and nothing about a property edit says which. Refused before
// any DDL happens, with its own reason in the ledger — which greys the Apply button.
void ibValueMetaObjectChartOfAccounts::ContributeTables(ibSchemaSnapshot& out) const
{
	ibValueMetaObjectRecordDataHierarchyMutableRef::ContributeTables(out);

	const ibValueMetaObjectAccountDimensionKindsTable* kindsTable = m_propertyAccountDimensionKindsTable->GetMetaObject();
	if (kindsTable == nullptr || kindsTable->IsDeleted())
		return;

	const ibString tableName = kindsTable->GetPhysicalTableName();
	const unsigned int ceiling = GetMaxAccountDimensionCount();
	const ibString chartName = GetName();

	out.Shared(kindsTable->GetMetaID(), tableName).m_beforeChange +=
		[tableName, ceiling, chartName](const ibSchemaTable*, ibRestructureInfo* report) -> bool {
			// Rows per account, counted by the DATABASE; the comparison is done HERE. Filtering the
			// aggregate in SQL would mean HAVING over an alias, which not every dialect renders — and a
			// rule that cannot run must not be the thing deciding whether a change is allowed. So it asks
			// for the plainest thing there is, and a failure to get even that now stops the apply.
			//
			// ⭐⭐ "COULD NOT CHECK" IS NOT "CHECKED AND FINE".
			//
			// This used to wrap the read in catch (ibBackendException) { return true; } — every failure,
			// whatever it was, answered PASS. The one legitimate failure is a table that does not exist
			// yet (a base nothing was ever applied to, where no row can be stranded), so THAT is what is
			// asked, plainly and in advance. Everything else — a broken lowering, a lost connection, a
			// dialect that cannot render the aggregate — is now what it actually is: the rule could not
			// run, so the apply stops instead of granting permission it never established.
			ibDatabaseQueryBuilder q;
			if (!q.TableExists(tableName))
				return true;   // nothing has ever been applied here; there is nothing to strand

			int overfull = 0;
			{
				ibQueryIR ir;
				ir.m_root = ibAggregate(ibScan(tableName),
					{ { ibFunc(ibT("COUNT"), { ibCol(ibOwnerRefField()) }), ibT("kindCount") } },
					{ ibCol(ibOwnerRefField()) });

				ibQueryResult rs = q.ExecuteIR(ir);
				while (rs.Next()) {
					if (rs.GetResultInt(ibT("kindCount")) > (int)ceiling)
						++overfull;
				}
			}

			if (overfull == 0)
				return true;

			if (report != nullptr)
				report->AppendError(ibString::Format(
					_("%s: %i account(s) hold more than %i analytics - remove the extra kinds from them before lowering the limit"),
					chartName, overfull, (int)ceiling));
			return false;
		};

	// ⭐ A DECLARED ACCOUNT'S ANALYTICS, UNFOLDED into its own kind columns — column N takes the kind of row N, the rule
	// an account's save follows (chartOfAccountsObject.cpp), so a declared account reads exactly like one a person
	// entered: a posting finds its kinds in the columns, a form in the section. The base seeded the account and its
	// lines; the columns are this chart's copy of those lines, which no schema can know. Only the declared rows'
	// columns: the ones past them are the data's, as the lines past them are.
	const ibValueMetaObjectAttributeBase* kindAttr = kindsTable->GetAccountDimensionKind();
	if (kindAttr == nullptr)
		return;

	ibSchemaTable& accounts = out.Shared(GetMetaID(), GetPhysicalTableName());
	for (const ibPredefinedItemDescription& item : m_predefinedDesc.m_items) {
		const auto declared = item.m_tables.find(kindsTable->GetMetaID());
		if (declared == item.m_tables.end())
			continue;
		for (ibSchemaSeedRow& row : accounts.m_seed) {
			if (row.m_id != item.m_guid)
				continue;
			for (unsigned int idx = 0; idx < GetAccountDimensionKindColumnCount() && idx < declared->second.size(); idx++) {
				const ibValueMetaObjectAttributePredefined* column = GetAccountDimensionKindColumn(idx);
				if (column == nullptr || column->IsDeleted())
					continue;
				// Empty as the column's type is empty, where the row names no kind.
				ibValue kind = column->CreateValue();
				const auto cell = declared->second[idx].find(kindAttr->GetMetaID());
				if (cell != declared->second[idx].end())
					kind = ibMaterializeCompositionValue(ibDeserializeValue(cell->second.m_value, m_metaData), m_metaData);
				row.Set(column->GetQueryColumn(), kind);
			}
		}
	}

	// ⭐ …AND WHAT A DECLARED ACCOUNT'S ANALYTICS MUST BE, on top of what every item must have (the base's check of the
	// accounts table): as many rows as the chart takes, each naming a kind — a predefined item the bound chart of
	// characteristic types still declares. A kind removed from that chart, or the chart unbound since, refuses the apply
	// before a statement, naming the account.
	accounts.m_beforeChange += [this, kindsTable, kindAttr, ceiling](const ibSchemaTable*, ibRestructureInfo* report) -> bool {
		bool passed = true;
		const auto refuse = [report, &passed](const ibString& why) {
			if (report != nullptr)
				report->AppendError(why);
			passed = false;
		};
		const ibMetaDescription& bound = m_propertyChartOfCharacteristicTypes->GetValueAsMetaDesc();
		const auto declaredKind = [this, &bound](const ibValue& value) {
			ibValueCompositionPredefined* kind = nullptr;
			if (!value.ConvertToValue(kind) || kind == nullptr)
				return false;
			for (unsigned int idx = 0; idx < bound.GetTypeCount(); idx++)
				if (bound.GetByIdx(idx) == kind->GetMetaId()) {
					const auto* chart = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataHierarchyMutableRef>(kind->GetMetaId());
					return chart != nullptr && chart->HasPredefinedValue(kind->GetGuid());
				}
			return false;
		};
		for (const ibPredefinedItemDescription& item : m_predefinedDesc.m_items) {
			const auto declared = item.m_tables.find(kindsTable->GetMetaID());
			if (declared == item.m_tables.end())
				continue;
			if (declared->second.size() > ceiling)
				refuse(ibString::Format(_("%s.%s: %i analytics are declared, the chart takes %i"),
					GetName(), item.m_name, static_cast<int>(declared->second.size()), static_cast<int>(ceiling)));
			for (std::size_t idx = 0; idx < declared->second.size(); idx++) {
				const auto cell = declared->second[idx].find(kindAttr->GetMetaID());
				if (cell == declared->second[idx].end() || !declaredKind(ibDeserializeValue(cell->second.m_value, m_metaData)))
					refuse(ibString::Format(_("%s.%s: analytics line %i names no kind its chart of characteristic types declares"),
						GetName(), item.m_name, static_cast<int>(idx) + 1));
			}
		}
		return passed;
	};
}
