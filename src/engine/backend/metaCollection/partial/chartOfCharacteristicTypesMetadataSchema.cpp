////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : chart of characteristic types - the STRUCTURE it declares (ContributeTables)
//
//	What this metaobject becomes in the database and the rules its tables carry into the restructuring,
//	the way chartOfAccountsMetadataSchema.cpp says them for a chart of accounts.
////////////////////////////////////////////////////////////////////////////

#include "chartOfCharacteristicTypes.h"
#include "backend/metaData.h"                // GetTypeCtor / IsRegisterCtor - the chart's characteristic, a type still there
#include "backend/metadataSerialization.h"   // ibDeserializeValue - a declared Type, read with this configuration
#include "backend/objCtor.h"                 // ibCtorMetaValueType::AllowValue - what the contour takes
#include "backend/query/schemaSnapshot.h"    // ibSchemaTable::m_beforeChange - the rule the declaration carries
#include "backend/restructureInfo.h"         // ibRestructureInfo - where that rule states its reason

// ⭐ WHAT A PREDEFINED CHARACTERISTIC MUST DECLARE (#278), on top of what every item must have (the base's check of this
// chart's table): its Type, filled in — what a field typed by the characteristic holds — and within the chart's contour:
// every type in it one the configuration still has and the chart's Types of characteristics take (the characteristic's
// own AllowValue; not Undefined, which it passes as an empty value). A type deleted since, or a contour that lost it (an emptied contour reads as String), would leave a
// characteristic typed by what the chart does not hold. Refused before a statement, naming the characteristic. A folder
// declares no Type (it is an item's attribute).
void ibValueMetaObjectChartOfCharacteristicTypes::ContributeTables(ibSchemaSnapshot& out) const
{
	ibValueMetaObjectRecordDataHierarchyMutableRef::ContributeTables(out);

	out.Shared(GetMetaID(), GetPhysicalTableName()).m_beforeChange += [this](const ibSchemaTable*, ibRestructureInfo* report) -> bool {
		bool passed = true;
		const ibValueMetaObjectAttributePredefined* type = GetDataType();
		if (type == nullptr)
			return passed;
		const auto refuse = [report, &passed](const ibString& why) {
			if (report != nullptr)
				report->AppendError(why);
			passed = false;
		};
		const ibCtorMetaValueType* const characteristic = m_metaData->GetTypeCtor(this, ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic);
		for (const ibPredefinedItemDescription& item : m_predefinedDesc.m_items) {
			if (item.m_folder)
				continue;
			const auto cell = item.m_row.find(type->GetMetaID());
			const ibValue declared = cell != item.m_row.end() ? ibDeserializeValue(cell->second.m_value, m_metaData) : ibValue();
			ibValueTypeDescription* typeDesc = nullptr;
			if (!declared.ConvertToValue(typeDesc) || typeDesc == nullptr || typeDesc->IsEmpty()) {
				refuse(ibString::Format(_("%s.%s: the characteristic declares no Type"), GetName(), item.m_name));
				continue;
			}
			for (const ibClassID& clsid : typeDesc->GetTypeDesc().GetClsidList())
				if (clsid == g_valueUndefinedCLSID || !m_metaData->IsRegisterCtor(clsid)
					|| (characteristic != nullptr && !characteristic->AllowValue(clsid))) {
					refuse(ibString::Format(_("%s.%s: the characteristic's Type holds a type the chart does not take - pick it again among its Types of characteristics"),
						GetName(), item.m_name));
					break;
				}
		}
		return passed;
	};
}
