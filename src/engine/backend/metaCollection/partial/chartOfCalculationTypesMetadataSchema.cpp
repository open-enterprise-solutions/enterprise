////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of calculation types - the STRUCTURE it declares (ContributeTables)
//
//	What this metaobject becomes in the database and the rules its tables carry into the restructuring,
//	the way chartOfAccountsMetadataSchema.cpp says them for a chart of accounts.
////////////////////////////////////////////////////////////////////////////

#include "chartOfCalculationTypes.h"
#include "backend/metaData.h"
#include "backend/metadataSerialization.h"                         // ibDeserializeValue - a declared type, read with this configuration
#include "backend/query/schemaSnapshot.h"                          // ibSchemaTable::m_beforeChange - the rule the declaration carries
#include "backend/restructureInfo.h"                               // ibRestructureInfo - where that rule states its reason
#include "backend/system/value/composition/valueComposerField.h"   // ibValueCompositionPredefined - a declared type

// ⭐ WHAT A PREDEFINED CALCULATION TYPE'S RELATIONS MUST BE, on top of what every item must have (the base's check of
// this chart's table): every line it declares in Displacing, Base and Leading names a calculation type — a predefined
// item its chart still declares, of a chart the section may name: this chart for Displacing; this chart or one BaseCharts
// lists for Base and Leading. A type removed since, or a chart dropped from BaseCharts, refuses the apply before a
// statement, naming the calculation type, the section and the line.
void ibValueMetaObjectChartOfCalculationTypes::ContributeTables(ibSchemaSnapshot& out) const
{
	ibValueMetaObjectRecordDataHierarchyMutableRef::ContributeTables(out);

	out.Shared(GetMetaID(), GetPhysicalTableName()).m_beforeChange += [this](const ibSchemaTable*, ibRestructureInfo* report) -> bool {
		bool passed = true;
		const auto admits = [this](const ibValueMetaObjectCalculationTypeRelationTable* section, const ibMetaID& chart) {
			if (chart == GetMetaID())
				return true;
			if (section == GetDisplacingTable())
				return false;
			const ibMetaDescription& charts = GetBaseCharts();
			for (unsigned int idx = 0; idx < charts.GetTypeCount(); idx++)
				if (charts.GetByIdx(idx) == chart)
					return true;
			return false;
		};
		const auto declaredType = [this, &admits](const ibValueMetaObjectCalculationTypeRelationTable* section, const ibValue& value) {
			ibValueCompositionPredefined* type = nullptr;
			if (!value.ConvertToValue(type) || type == nullptr || !admits(section, type->GetMetaId()))
				return false;
			const auto* chart = m_metaData->FindAnyObjectByFilter<ibValueMetaObjectRecordDataHierarchyMutableRef>(type->GetMetaId());
			return chart != nullptr && chart->HasPredefinedValue(type->GetGuid());
		};
		for (const ibValueMetaObjectCalculationTypeRelationTable* section : GetRelationTables()) {
			if (section == nullptr || !section->IsEnabled())   // a section never saved is not a table yet
				continue;
			const ibValueMetaObjectAttributePredefined* column = section->GetCalculationType();
			if (column == nullptr || column->GetMetaID() == 0)
				continue;
			for (const ibPredefinedItemDescription& item : m_predefinedDesc.m_items) {
				const auto declared = item.m_tables.find(section->GetMetaID());
				if (declared == item.m_tables.end())
					continue;
				for (std::size_t idx = 0; idx < declared->second.size(); idx++) {
					const auto cell = declared->second[idx].find(column->GetMetaID());
					if (cell != declared->second[idx].end() && declaredType(section, ibDeserializeValue(cell->second.m_value, m_metaData)))
						continue;
					if (report != nullptr)
						report->AppendError(ibString::Format(_("%s.%s: %s line %i names no calculation type the section may name"),
							GetName(), item.m_name, section->GetName(), static_cast<int>(idx) + 1));
					passed = false;
				}
			}
		}
		return passed;
	};
}
