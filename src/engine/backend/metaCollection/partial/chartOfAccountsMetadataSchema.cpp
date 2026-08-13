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
#include "backend/query/columnLayout.h"                   // ibRowKeyField - the section's owner column
#include "backend/query/schemaSnapshot.h"                 // ibSchemaTable::m_beforeChange - the rule the declaration carries
#include "backend/restructureInfo.h"                      // ibRestructureInfo - where that rule states its reason

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

	const wxString tableName = kindsTable->GetPhysicalTableName();
	const unsigned int ceiling = GetMaxAccountDimensionCount();
	const wxString chartName = GetName();

	out.Shared(kindsTable->GetMetaID(), tableName).m_beforeChange =
		[tableName, ceiling, chartName](ibRestructureInfo* report) -> bool {
			// Rows per account, counted by the DATABASE; the comparison is done HERE. Filtering the
			// aggregate in SQL would mean HAVING over an alias, and if the lowering did not render it the
			// query would throw — straight into the catch below, where a refusal would read as a pass.
			// The rule must not be able to fail quietly, so it asks for the plainest thing there is.
			//
			// A database that has not got this table yet does throw, and that IS a pass: nothing has ever
			// been applied there, so nothing can be stranded.
			int overfull = 0;
			try {
				ibDatabaseQueryBuilder q;
				ibQueryIR ir;
				ir.m_root = ibAggregate(ibScan(tableName),
					{ { ibFunc(wxT("COUNT"), { ibCol(ibRowKeyField()) }), wxT("kindCount") } },
					{ ibCol(ibRowKeyField()) });

				ibQueryResult rs = q.ExecuteIR(ir);
				while (rs.Next()) {
					if (rs.GetResultInt(wxT("kindCount")) > (int)ceiling)
						++overfull;
				}
			}
			catch (const ibBackendException&) {
				return true;
			}

			if (overfull == 0)
				return true;

			if (report != nullptr)
				report->AppendError(wxString::Format(
					_("%s: %i account(s) hold more than %i analytics — remove the extra kinds from them before lowering the limit"),
					chartName, overfull, (int)ceiling));
			return false;
		};
}
