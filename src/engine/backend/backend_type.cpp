#include "backend_type.h"
#include "backend/compiler/enumUnit.h"
#include "backend/system/value/valueTable.h"   // g_valueTableCLSID (the _table default); the primitive value clsids come via value.h

//***********************************************************************
//*                         Type factory                                *
//***********************************************************************


ibValue ibBackendTypeFactory::CreateValue() const
{
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (typeDesc.GetClsidCount() == 1) {
		const ibClassID& clsid = typeDesc.GetFirstClsid();
		if (ibValue::IsRegisterCtor(clsid)) {
			const ibCtorAbstractType* so = ibValue::GetAvailableCtor(clsid);
			if (so->GetObjectTypeCtor() == ibCtorObjectType::ibCtorObjectType_object_enum) {
				try {
					// ⚠⚠ THE VALUE BELONGS TO THE ENUMERATION, AND THE ENUMERATION DIES HERE.
					//
					// This read used to hold the enumeration in a std::shared_ptr — a second owner over
					// an object the runtime's own count already owns — and that is what stopped
					// compiling when ibValue became ibBackendRuntimeOwned. Refusing it uncovered a
					// live defect behind it: the variant is the enumeration's MEMBER
					// (ibValuePtr m_value), so `return enumVal->GetEnumVariantValue()` copies the
					// pointer, the enumeration is then destroyed, its member releases the variant to
					// zero — and the caller IncrRef's freed memory (ibValue::operator=(ibValue*)).
					//
					// So the value is given a reference of its OWN before its holder goes — and the
					// answer IS that reference: a return value is built before the locals are
					// destroyed, so the variant is held by the answer while the enumeration still
					// lives, and nothing is left over. (It used to be answered as a raw pointer with
					// one count taken by hand and never given back.) The honest question remains a
					// different one — this enumeration should be reached from the registry that
					// already keeps one, not built and thrown away per read.
					const ibValuePtr<ibValueEnumerationWrapper> enumVal = ibValue::CreateObject(so->GetClassName());
					ibValue* const variant = enumVal != nullptr ? enumVal->GetEnumVariantValue() : nullptr;
					if (variant != nullptr)
						return variant;
				}
				catch (...) {
				}
				return wxEmptyValue;
			}
			try {
				return ibValue::CreateObject(so->GetClassType());
			}
			catch (...) {
				return wxEmptyValue;
			}
		}
	}
	return wxEmptyValue;
}

#include "backend/system/value/valueType.h"

// ⭐⭐ A VALUE IS ADJUSTED TO WHAT MAY BE STORED, NOT TO WHAT WAS DECLARED (GetTypeValueDesc, see the
// header). The two differ for exactly one kind of declaration — a CHARACTERISTIC, which declares "whatever
// this chart admits" as one type and expands to the chart's list on demand. Adjusted to the declaration, a
// counterparty written into a slot declared as the chart's characteristic matched nothing and came out
// EMPTY: every account dimension of every posting was stored as its kind with no value (measured
// 2026-09-15 on a fresh accounting configuration — the kind landed, the value did not, already in memory).
// For every other declaration the two answers are the same object, so nothing else moves.
ibValue ibBackendTypeFactory::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(GetTypeValueDesc());
}

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(GetTypeValueDesc(), varValue);
}

ibValue ibBackendTypeFactory::AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const
{
	return ibValueTypeDescription::AdjustValue(limit, varValue);
}

/////////////////////////////////////////////////////////////////////////////////////

#include "backend/metaData.h"
#include "backend/objCtor.h"

ibValue ibBackendTypeConfigFactory::CreateValue() const
{
	ibMetaData const* metaData = GetMetaData();
	wxASSERT(metaData);
	const ibTypeDescription& typeDesc = GetTypeDesc();
	if (typeDesc.GetClsidCount() == 1) {
		const ibCtorMetaValueType* so = metaData->GetTypeCtor(typeDesc.GetFirstClsid());
		if (so != nullptr) {
			try {
				return metaData->CreateObject(so->GetClassType());
			}
			catch (...) {
				return wxEmptyValue;
			}
		}
	}
	return ibBackendTypeFactory::CreateValue();
}

// …the same rule for a configuration's declarations, where a characteristic actually occurs.
ibValue ibBackendTypeConfigFactory::AdjustValue() const
{
	return ibValueTypeDescription::AdjustValue(
		GetTypeValueDesc(),
		GetMetaData()
	);
}

ibValue ibBackendTypeConfigFactory::AdjustValue(const ibValue& varValue) const
{
	return ibValueTypeDescription::AdjustValue(
		GetTypeValueDesc(),
		varValue,
		GetMetaData()
	);
}

ibValue ibBackendTypeConfigFactory::AdjustValue(const ibValue& varValue, const ibTypeDescription& limit) const
{
	return ibValueTypeDescription::AdjustValue(
		limit,
		varValue,
		GetMetaData()
	);
}

#include "backend/metaCollection/partial/chartOfCharacteristicTypes.h"   // a characteristic answers with its chart's list

// WHAT A VALUE HERE MAY BE. Ordinary declarations answer with themselves; a characteristic answers
// with the list its CHART declares — the owner keeps it, the holder borrows it, so a chart that gains
// a type widens every field declared through it at once, with nothing copied or recomputed.
//
// ⭐⭐ HERE, FOR EVERY HOLDER OF SUCH A DECLARATION, and not on the attribute alone. It lived on the
// attribute, and everything else that holds `Characteristic.<chart>` — a control bound to the field, a
// filter cell, a form's own attribute — answered with the one class no value carries: a stored `True`
// read back through the control as nothing, and an empty kind offered no type to choose. The control
// then asked the FIELD through its binding for the answer, a second road to the same rule (Max,
// 2026-09-24: "what is the point of this"). A factory has its configuration, so it answers itself.
//
// The chart is the factory's own configuration's, never the active one's. The registry is asked first,
// and answers in one step; a configuration that is only LOADED has no registry, and there the chart is
// found in the tree, by the id the characteristic's class carries. A chart that cannot be found leaves
// the declaration standing.
//
// 🛑 THE LOADED-ONLY COPY IS NOT A CORNER CASE. It is the APPLIED configuration an apply compares the
// edited one against (ibMetaDataConfigurationStorage::OnSaveDatabase). Answering there with the
// declaration while the edited copy answered with the chart's list would lay one field out two ways
// in a single comparison.
//
// 🛑 AND A LINK BY TYPE DOES NOT ENTER IT. Which ONE of the chart's types a value turns out to be is
// decided by the kind standing beside it, and only a holder of VALUES can read that
// (ibChoiceLinkResolver). Followed statically, the link hands back the governing field's declaration —
// for a kind, "a reference to the chart" — and every value adjusted to that was written empty
// (2026-09-23: characteristics lost on write, a cell that did not react).
ibTypeDescription& ibBackendTypeConfigFactory::GetTypeValueDesc() const
{
	ibTypeDescription& declared = GetTypeDesc();
	const ibMetaData* metaData = GetMetaData();
	if (metaData == nullptr || declared.GetClsidCount() != 1 || !IsCharacteristic(declared.GetFirstClsid()))
		return declared;

	const ibClassID clsid = declared.GetFirstClsid();
	const ibValueMetaObjectChartOfCharacteristicTypes* chart = nullptr;
	if (const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(clsid)) {
		if (typeCtor->GetMetaObject() != nullptr)
			typeCtor->GetMetaObject()->ConvertToValue(chart);
	}
	if (chart == nullptr)
		chart = metaData->FindAnyObjectByFilter<ibValueMetaObjectChartOfCharacteristicTypes>(
			static_cast<ibMetaID>(metaID_from_clsid(clsid)));
	if (chart == nullptr)
		return declared;

	// The chart's list, BORROWED: the owner keeps it, the holder only points at it.
	//
	// Non-const on purpose. Reaching it THROUGH THE METADATA is the legal way to get at a live
	// declaration — a configuration is edited, so its type descriptions are state, not a frozen
	// snapshot. Constifying the borrow here would only force a cast at the first editor that needs it.
	return chart->GetTypesOfCharacteristics();
}

// The one filter-kind -> default value clsid mapping. Static so both ibVariantDataAttribute::DoSetDefault-
// MetaType and ibValueControl::AutoBindNewSource resolve the SAME default type for a given filter kind.
ibClassID ibBackendTypeConfigFactory::GetDefaultTypeByFilter(ibSelectorDataType filterDataType)
{
	switch (filterDataType) {
	case ibSelectorDataType::ibSelectorDataType_boolean:  return g_valueBooleanCLSID;
	case ibSelectorDataType::ibSelectorDataType_resource: return g_valueNumberCLSID;
	case ibSelectorDataType::ibSelectorDataType_table:    return g_valueTableCLSID;
	case ibSelectorDataType::ibSelectorDataType_reference:
	default:                                              return g_valueStringCLSID;
	}
}

#include "backend/metaData.h"                          // the registry the referenceable kinds come from
#include "backend/system/value/valueDynamicList.h"     // g_valueDynamicListCLSID
#include "backend/system/value/valueDataComposition.h" // g_valueDataCompositionCLSID
#include "backend/system/value/valueSpreadsheet.h"     // g_valueSpreadsheetCLSID

// ⭐⭐ WHAT A FIELD OF THIS KIND MAY HOLD — see the header. Built from the registry, so nothing keeps a
// list of metatypes that would have to learn about each new one.
//
// 🛑 THE BODY CAME FROM THE TYPE PICKER (frontend/win/dlgs/typeSelector.cpp), where it was a static
// function only that dialog could reach — which is why the MCP door, in this very library, could set a
// type the designer does not offer. The picker now asks this.
void ibBackendTypeConfigFactory::GetTypesByFilter(ibSelectorDataType filterDataType,
	const ibMetaData* metaData, std::vector<ibClassID>& out)
{
	const bool anyType = filterDataType == ibSelectorDataType::ibSelectorDataType_any;

	if (anyType)
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_EMPTY));

	// The primitives. A reference shape carries them too: a characteristic may be a number or a
	// string just as well as a reference to something.
	if (anyType || filterDataType == ibSelectorDataType::ibSelectorDataType_reference) {
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN));
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_DATE));
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_STRING));
	}
	else if (filterDataType == ibSelectorDataType::ibSelectorDataType_boolean) {
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_BOOLEAN));
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
	}
	else if (filterDataType == ibSelectorDataType::ibSelectorDataType_resource) {
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NUMBER));
	}

	if (anyType)
		out.push_back(ibValue::GetIDByVT(ibValueTypes::TYPE_NULL));

	// ⭐ THE CONTAINER KINDS — what a field may BE when it holds ROWS rather than one value: a table, a
	// dynamic list, a data composition. They need no metadata: they are registered value types, not
	// something a configuration declares.
	if (anyType || filterDataType == ibSelectorDataType::ibSelectorDataType_table) {
		out.push_back(g_valueTableCLSID);
		out.push_back(g_valueDynamicListCLSID);
		out.push_back(g_valueDataCompositionCLSID);
		// …and the document a gridbox shows: the control creates the variable, so the variable has to
		// be nameable on its own too.
		out.push_back(g_valueSpreadsheetCLSID);
	}

	if (metaData == nullptr)
		return;

	// EVERYTHING REFERENCEABLE, asked of the registry — and the CHARACTERISTICS beside them, which are
	// the declaration standing for whatever their chart allows. A table shape wants the tabular sources
	// instead: those are its references.
	if (anyType || filterDataType == ibSelectorDataType::ibSelectorDataType_reference ||
		filterDataType == ibSelectorDataType::ibSelectorDataType_table) {
		for (auto so : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Reference))
			out.push_back(so->GetClassType());
		for (auto so : metaData->GetListCtorsByType(ibCtorObjectMetaType::ibCtorObjectMetaType_Characteristic))
			out.push_back(so->GetClassType());
	}
}

/////////////////////////////////////////////////////////////////////////////////////

#include "backend/formatString.h"   // ibFormatString — what the type description gives

bool ibBackendTypeConfigFactory::GetFormatFromTypeDesc(const ibTypeDescription& type, ibFormatString& formatString)
{
	bool written = false;

	// A number: as many digits after the point as the type keeps — `5.00`, not `5`. A number nobody bounded
	// (precision 0, "no limit" — an average, a product) keeps no count of its own and is shown as it is.
	if (type.ContainType(ibValueTypes::TYPE_NUMBER) && type.GetPrecision() > 0) {
		formatString.m_number.m_fractionDigits = type.GetScale();
		written = true;
	}

	// A date: what its fractions keep — a date alone shows no time, a time no date.
	if (type.ContainType(ibValueTypes::TYPE_DATE)) {
		switch (type.GetDateFraction()) {
		case ibDateFractions::ibDateFractions_Date:
			formatString.m_date.m_pattern = ibFormatString::PresetPattern(ibDatePreset::Date);
			break;
		case ibDateFractions::ibDateFractions_Time:
			formatString.m_date.m_pattern = ibFormatString::PresetPattern(ibDatePreset::Time);
			break;
		default:
			formatString.m_date.m_pattern = ibFormatString::PresetPattern(ibDatePreset::DateTime);
			break;
		}
		written = true;
	}

	return written;
}

const ibFormatString& ibBackendTypeConfigFactory::GetFormatFromColumn(const ibTranslateString& format, const ibTypeDescription& type)
{
	// Compared by what they say, not by whose they are: an attribute edited in the designer keeps its
	// address, and a column's type in a value table is changed in place.
	//
	// ONE KEPT: a table is painted a column at a time, top to bottom, so the cells of a column ask one question
	// in a row and the first cell of the next column asks a new one. The answer handed back is good until the
	// next ask on this thread — read it at once.
	struct ibKeptFormat {
		wxString m_text;
		ibTypeDescription m_type;
		ibFormatString m_formatString;
	};
	static thread_local ibKeptFormat s_kept;

	// The text in the language in force, through a scratch: compared with the one kept without allocating
	// (comparing the translations themselves copied every language of both, every cell).
	thread_local wxString s_text;
	const wxString& text = format.GetString(s_text);
	if (s_kept.m_text == text && s_kept.m_type == type)
		return s_kept.m_formatString;

	s_kept.m_formatString = ibFormatString();
	if (text.IsEmpty() || !ibFormatString::Parse(text, s_kept.m_formatString))
		GetFormatFromTypeDesc(type, s_kept.m_formatString);
	s_kept.m_text = text;
	s_kept.m_type = type;
	return s_kept.m_formatString;
}

/////////////////////////////////////////////////////////////////////////////////////

#include "backend/query/queryColumn.h"                      // ibBackendSourceColumn — the leaf the dot returns

const ibBackendSourceColumn* ibBackendTypeSourceFactory::WalkSource(
	const ibSourceDescription& desc, bool* valid, wxString* outText) const
{
	if (valid != nullptr) *valid = false;
	const std::vector<ibSourceHop>& path = desc.GetPath();
	if (path.empty()) return nullptr;

	// Gate 1: path[0] must be one of THIS context's source attributes (form-local).
	ibBackendFormAttributeValue* headHolder = FindSourceHolder(desc.GetFirst());
	if (headHolder == nullptr) return nullptr;
	if (outText != nullptr) *outText = headHolder->GetName();
	// A whole-attribute binding (length 1) is valid with no column leaf.
	if (path.size() == 1) { if (valid != nullptr) *valid = true; return nullptr; }

	// Deeper hops delegate to THE shared structure-resolve hop — it walks each source's EXPLORER (the same
	// self-describing structure the runtime value-hop steps), descending into each reference's OWN columns.
	// No metaID -> name -> FindAnyObjectByFilter fallback: the reference-as-source explorer already holds the
	// target's columns, so a miss is a genuinely broken binding. ONE resolve path, the design-time twin of
	// ResolvePath (which the tablebox renderer + GetValueByPath fetch values through).
	ibSourceDataObject* source = headHolder->GetSourceValue();
	const ibBackendSourceColumn* leaf = nullptr;
	const bool resolved = (source != nullptr) && source->WalkColumns(path, 1, leaf, outText);
	if (valid != nullptr) *valid = resolved;
	return resolved ? leaf : nullptr;
}

ibBackendFormAttributeValue* ibBackendTypeSourceFactory::FindSourceHolder(const ibMetaID& id) const
{
	std::vector<ibBackendFormAttributeValue*> holders;
	GetSourceList(holders);
	for (ibBackendFormAttributeValue* holder : holders)
		if (holder != nullptr && id == holder->GetId())
			return holder;
	return nullptr;
}