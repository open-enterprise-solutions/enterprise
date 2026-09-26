////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : type unit
////////////////////////////////////////////////////////////////////////////

#include "valueType.h"
#include "valueArray.h"

//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////

ibValueType::ibValueType(const ibClassID& clsid) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = clsid;
}

#include "backend/metadataConfiguration.h"

ibValueType::ibValueType(const wxString& typeName) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = activeMetaData->GetIDObjectFromString(typeName);
}

ibValueType::ibValueType(const ibValue& cObject) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = cObject.GetClassType();
}

ibValueType::ibValueType(const ibValueType& cType) :
	ibValue(ibValueTypes::TYPE_VALUE, true)
{
	m_clsid = cType.m_clsid;
}

ibString ibValueType::GetString() const
{
	return activeMetaData->GetNameObjectFromID(m_clsid);
}

// The same naming door a single type uses, once per admitted type. Asked of the METADATA, so a
// configuration-specific reference reads as "CatalogRef.Goods" rather than a number.
ibString ibValueTypeDescription::GetString() const
{
	wxString presentation;
	for (const ibClassID& clsid : m_typeDesc.GetClsidList()) {
		if (!presentation.IsEmpty())
			presentation << wxT(", ");
		presentation << activeMetaData->GetNameObjectFromID(clsid);
	}
	return presentation;
}

//////////////////////////////////////////////////////////////////////

#include "backend/system/systemManager.h"

ibValue ibValueTypeDescription::AdjustValue(const ibTypeDescription& typeDescription,
	const ibMetaData* metaData)
{
	if (!typeDescription.IsOk())
		return wxEmptyValue;

	if (typeDescription.GetClsidCount() == 1) {

		const ibClassID& clsid = typeDescription.GetFirstClsid();

		// 🛑 THE PROCESS MAY HAVE NO ACTIVE CONFIGURATION AT ALL - a headless tool before it opens one, a test.
		// This went to `activeMetaData->` unasked, and the column codec reached here with the metadata it had
		// been handed left behind (columnLayout.cpp), so reading a cell whose tag the result does not carry was
		// an access violation instead of the typed empty value it is documented to answer (2026-09-20). What
		// the value registry can make by itself - a primitive - it makes; anything that needs a configuration
		// and has none is the empty value.
		//
		// ⭐ AND THE QUESTION IS ASKED ON EVERY ROAD, not only that one. A class the registry does not know
		// cannot be made by any of the three, and asking for it anyway is a refusal thrown at a caller that
		// only wanted to know what an empty cell of this column looks like: a configuration whose metaobjects
		// were built without runtime objects has the type DESCRIBED and not REGISTERED, and the IN-set fold
		// met exactly that (ComputedServerFix.In_AnEmptyReferenceAmongTheValuesGoesPairByPair, 2026-09-24).
		//
		// 🛑 …ASKED OF THE ONE THAT WILL MAKE IT, as the overload below already asks. A configuration's own
		// types - a catalog's reference, an enumeration's member - are registered in ITS image, and the value
		// registry has never heard of them: asked there, every reference column's typed empty came back
		// untyped, and a ledger balance split one item into two rows over a currency stored empty in two ways
		// (TypedEmpty tests, 2026-09-26). The metadata answers for its own image and falls through to the
		// value registry itself.
		const ibMetaData* const owner = (metaData != nullptr) ? metaData : activeMetaData;
		if (!(owner != nullptr ? owner->IsRegisterCtor(clsid) : ibValue::IsRegisterCtor(clsid)))
			return ibValue();

		return (owner != nullptr) ? owner->CreateObject(clsid) : ibValue::CreateObject(clsid);
	}

	return wxEmptyValue;
}

ibValue ibValueTypeDescription::AdjustValue(const ibTypeDescription& typeDescription, const ibValue& varValue,
	const ibMetaData* metaData)
{
	if (!typeDescription.IsOk())
		return varValue;

	auto iterator = std::find(typeDescription.m_listTypeClass.begin(), typeDescription.m_listTypeClass.end(), varValue.GetClassType());
	if (iterator != typeDescription.m_listTypeClass.end()) {
		ibValueTypes vt = ibValue::GetVTByID(varValue.GetClassType());
		if (vt < ibValueTypes::TYPE_REFFER) {
			if (vt == ibValueTypes::TYPE_NUMBER) {
				// Precision 0 is "no limit", as a string's length 0 is (Unqualified) — no rounding.
				if (typeDescription.m_typeData.m_number.m_precision == 0)
					return varValue;
				return ibValueSystemFunction::Round(varValue, typeDescription.m_typeData.m_number.m_scale);
			}
			else if (vt == ibValueTypes::TYPE_DATE) {
				if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Date)
					return ibValueSystemFunction::BegOfDay(varValue);
				else if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Time)
					return varValue;
				return varValue;
			}
			else if (vt == ibValueTypes::TYPE_STRING) {
				// ⚠ ZERO LENGTH IS "UNLIMITED", NOT "TRUNCATE TO NOTHING". A string qualifier left at
				// its default carries 0, and `Left(value, 0)` returns an EMPTY string — so adjusting a
				// value to such a type WIPED it. Typed in, saved, blank on the next read, with the
				// write reporting success all the way down.
				//
				// The same two-facts-one-number confusion as the driver's parameter clamp: only clamp
				// where a limit was actually declared.
				if (typeDescription.m_typeData.m_string.m_length == 0)
					return varValue;
				return ibValueSystemFunction::Left(varValue, typeDescription.m_typeData.m_string.m_length);
			}
		}
		return varValue;
	}

	if (typeDescription.GetClsidCount() == 1) {

		// The same rule as the overload above: the metadata handed in, else the active one - and with neither
		// (a headless tool before it opens a base, a test) what the value registry can make by itself.
		const ibMetaData* const source = metaData != nullptr ? metaData : activeMetaData;
		if (source != nullptr ? source->IsRegisterCtor(typeDescription.GetFirstClsid())
			: ibValue::IsRegisterCtor(typeDescription.GetFirstClsid())) {

			ibValueTypes vt = ibValue::GetVTByID(typeDescription.GetFirstClsid());
			if (vt < ibValueTypes::TYPE_REFFER) {
				if (vt == ibValueTypes::TYPE_NUMBER) {
					if (typeDescription.m_typeData.m_number.m_precision == 0)
						return ibValue(varValue.GetNumber());   // no limit: the number as it is (the value may be of another type)
					return ibValueSystemFunction::Round(varValue, typeDescription.m_typeData.m_number.m_scale);
				}
				else if (vt == ibValueTypes::TYPE_DATE) {
					if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Date)
						return ibValueSystemFunction::BegOfDay(varValue);
					else if (typeDescription.m_typeData.m_date.m_dateTime == ibDateFractions::ibDateFractions_Time)
						return varValue;
					return varValue.GetDate();
				}
				else if (vt == ibValueTypes::TYPE_STRING) {
					// Same rule as the branch above: 0 is "no declared limit", not "empty".
					//
					// 🛑 BUT THIS BRANCH IS REACHED BY A VALUE THAT IS *NOT* A STRING, and "no limit" is no reason
					// to leave it one. It returned the value as it came — so an Undefined adjusted to an unlimited
					// string stayed Undefined, where its siblings above hand back a number and a date. The field
					// then held no type at all: a characteristic whose kind says "string" was cleared to Undefined
					// instead of "", and the cell threw away whatever was typed into it, having nothing to read it
					// by (2026-09-23: under a string kind the cell stayed Undefined, while a boolean kind made it
					// `False` and a date kind an empty date).
					if (typeDescription.m_typeData.m_string.m_length == 0)
						return ibValue(varValue.GetString());
					return ibValueSystemFunction::Left(varValue, typeDescription.m_typeData.m_string.m_length);
				}
			}

			return source != nullptr
				? source->CreateObject(typeDescription.GetFirstClsid())
				: ibValue::CreateObject(typeDescription.GetFirstClsid());
		}
	}
	return wxEmptyValue;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

// ⭐⭐ A TYPE NAMED AT RUN TIME WITHOUT A QUALIFIER LIMITS NOTHING. `ibTypeData`'s own defaults — ten digits
// and no fraction, ten characters, a date without its time — are what the designer gives a NEW ATTRIBUTE,
// and they reached every description a script built: `New TypeDescription("String")` cut a text to ten
// characters, "Number" rounded to a whole number, "Date" dropped the time, and a value table's column added
// without a type kept the first ten characters of whatever was written into it (measured 2026-09-21).
//
// No qualifier means no limit: precision 0 (AdjustValue does not round), a date with its time, length 0
// (already "unlimited" to AdjustValue). The designer's defaults stay where they belong — on a new attribute,
// whose type becomes a column in the database and has to say how wide it is.
ibTypeDescription::ibTypeData ibValueTypeDescription::Unqualified()
{
	return ibTypeDescription::ibTypeData(ibQualifierNumber(0, 0),
		ibQualifierDate(ibDateFractions::ibDateFractions_DateTime), ibQualifierString(0));
}

ibTypeDescription ibValueType::GetOwnerTypeDescription() const
{
	return ibTypeDescription(GetOwnerTypeClass(), ibValueTypeDescription::Unqualified());
}

ibValueTypeDescription::ibValueTypeDescription() :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true){
}

ibValueTypeDescription::ibValueTypeDescription(ibValueType* valueType) :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_typeDesc({ valueType ? valueType->GetOwnerTypeClass() : 0 }, Unqualified()){
}

ibValueTypeDescription::ibValueTypeDescription(ibValueType* valueType, ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString) :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_typeDesc({ valueType ? valueType->GetOwnerTypeClass() : 0 },
		(qNumber ? *qNumber : Unqualified().m_number), (qDate ? *qDate : Unqualified().m_date), (qString ? *qString : Unqualified().m_string)){
}

ibValueTypeDescription::ibValueTypeDescription(const ibTypeDescription& typeDescription)
	: ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_typeDesc(typeDescription){
}

ibValueTypeDescription::ibValueTypeDescription(const std::vector<ibClassID>& array) :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_typeDesc(array, Unqualified()){
}

ibValueTypeDescription::ibValueTypeDescription(const std::vector<ibClassID>& array, ibValueQualifierNumber* qNumber, ibValueQualifierDate* qDate, ibValueQualifierString* qString) :
	ibValueStaticMembers(ibValueTypes::TYPE_VALUE, true), m_typeDesc(array,
		(qNumber ? *qNumber : Unqualified().m_number), (qDate ? *qDate : Unqualified().m_date), (qString ? *qString : Unqualified().m_string)){
}

ibValueTypeDescription::~ibValueTypeDescription()
{
}

bool ibValueTypeDescription::Init(ibValue** paParams, const long lSizeArray)
{
	if (lSizeArray < 1)
		return false;

	// A qualifier not given limits nothing (Unqualified); the ones given override it below.
	m_typeDesc.m_typeData = Unqualified();

	if (paParams[0]->GetType() == ibValueTypes::TYPE_STRING) {
		wxString classType = paParams[0]->GetString();
		if (activeMetaData->IsRegisterCtor(classType)) {
			ibValueQualifierNumber* qNumber = nullptr;
			if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
				m_typeDesc.m_typeData.m_number = *qNumber;
			ibValueQualifierDate* qDate = nullptr;
			if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
				m_typeDesc.m_typeData.m_date = *qDate;
			ibValueQualifierString* qString = nullptr;
			if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
				m_typeDesc.m_typeData.m_string = *qString;
			m_typeDesc.m_listTypeClass.emplace_back(
				activeMetaData->GetIDObjectFromString(classType)
			);
			return true;
		}
	}

	ibValueArray* valArray = nullptr;
	if (paParams[0]->ConvertToValue(valArray)) {
		ibValueQualifierNumber* qNumber = nullptr;
		if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
			m_typeDesc.m_typeData.m_number = *qNumber;
		ibValueQualifierDate* qDate = nullptr;
		if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
			m_typeDesc.m_typeData.m_date = *qDate;
		ibValueQualifierString* qString = nullptr;
		if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
			m_typeDesc.m_typeData.m_string = *qString;
		for (unsigned int i = 0; i < valArray->Count(); i++) {
			ibValue retValue; valArray->GetAt(i, retValue);
			ibValueType* valType = CastValue<ibValueType>(retValue);
			wxASSERT(valType);
			m_typeDesc.m_listTypeClass.emplace_back(
				valType->GetOwnerTypeClass()
			);
		}
		/*std::sort(m_listTypeClass.begin(), m_listTypeClass.end(), [](const ibClassID& a, const ibClassID& b) {
			return a < b; }
		);*/
		return true;
	}

	ibValueType* valType = nullptr;
	if (paParams[0]->ConvertToValue(valType)) {
		ibValueQualifierNumber* qNumber = nullptr;
		if (lSizeArray > 1 && paParams[1]->ConvertToValue(qNumber))
			m_typeDesc.m_typeData.m_number = *qNumber;
		ibValueQualifierDate* qDate = nullptr;
		if (lSizeArray > 2 && paParams[2]->ConvertToValue(qDate))
			m_typeDesc.m_typeData.m_date = *qDate;
		ibValueQualifierString* qString = nullptr;
		if (lSizeArray > 3 && paParams[3]->ConvertToValue(qString))
			m_typeDesc.m_typeData.m_string = *qString;
		m_typeDesc.m_listTypeClass.emplace_back(
			valType->GetOwnerTypeClass()
		);
		return true;
	}

	return false;
}

bool ibValueTypeDescription::AdjustOutValue(const ibValue& varValue, ibValue& out) const
{
	// A DESCRIPTION OF NOTHING NARROWS NOTHING, and the value passes as it came — the same answer the
	// two-argument form above gives for it.
	if (!m_typeDesc.IsOk()) {
		out = varValue;
		return true;
	}

	// ⚠ AND A VALUE THE DESCRIPTION DOES NOT NAME COMES BACK AS THE EMPTY VALUE OF WHAT IT DOES —
	// `false` with a value of the right type in hand, never nothing. The narrowing is what the field
	// then holds, and its TYPE is what a caller asking "what does this narrow to" reads off it, so the
	// question is asked once (Max, 2026-09-24). The place that watches for a value going in and not
	// coming out is ibChoiceLinkResolver::Adjust, which says so in the journal.
	out = AdjustValue(m_typeDesc, varValue);
	return m_typeDesc.ContainType(varValue.GetClassType());
}

bool ibValueTypeDescription::ContainType(const ibValue& cType) const
{
	ibValueType* valueType = CastValue<ibValueType>(cType);
	wxASSERT(valueType);
	// The range ended at begin(), so the search was over NOTHING: find always answered begin(), and
	// comparing that with end() made the result "yes" for every type whenever the list was not empty.
	auto it = std::find(m_typeDesc.m_listTypeClass.begin(), m_typeDesc.m_listTypeClass.end(), valueType->GetOwnerTypeClass());
	return it != m_typeDesc.m_listTypeClass.end();
}

ibValue ibValueTypeDescription::AdjustValue() const
{
	return AdjustValue(m_typeDesc);
}

ibValue ibValueTypeDescription::AdjustValue(const ibValue& varValue) const
{
	return AdjustValue(m_typeDesc, varValue);
}

ibValue ibValueTypeDescription::Types() const
{
	ibValueArray* arr = new ibValueArray();
	for (auto clsid : m_typeDesc.m_listTypeClass)
		arr->Add(new ibValueType(clsid));
	return arr;
}

enum Func {
	enContainsType = 0,
	enAdjustValue,
	enTypes
};

// Bound contributor (push). Build() Clear()s before running this, so no
// ClearHelper() here. Type-invariant — ctx unused.
void ibValueTypeDescription_BindNames(ibValue::ibMemberTable& helper, const ibValue* /*ctx*/)
{
	helper.AppendConstructor(4, wxT("typeDescription(type, qNumber, qDate, qString)"));

	helper.AppendFunc(wxT("ContainType"), 1, wxT("containsType(type : type)"));
	helper.AppendFunc(wxT("AdjustValue"), 1, wxT("AdjustValue(value = undefined : any"));
	helper.AppendFunc(wxT("Types"), wxT("Types()"));
}

// Population lives in ibValueTypeDescription_BindNames above, run lazily by
// ibValue::EnsureBuilt() on first GetPMethods() — a per-instance member table
// built once on first access (thread-safe lazy build). PrepareNames() is gone.

bool ibValueTypeDescription::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray)
{
	switch (lMethodNum)
	{
	case enContainsType:
		pvarRetValue = ContainType(*paParams[0]);
		return true;
	case enAdjustValue: {
		// TWO CALLS, TWO MEANINGS: with a value it CONVERTS that value, without one it BUILDS an empty
		// one of this type. The no-argument line used to run unconditionally right after the other, so
		// the converted result was computed and then thrown away — `Type.AdjustValue("45.2")` answered
		// an empty number instead of 45.2, and every caller filling characteristics from raw text got
		// blanks that looked like an unfilled source rather than a lost conversion.
		pvarRetValue = lSizeArray > 0 && paParams != nullptr
			? AdjustValue(*paParams[0])
			: AdjustValue();
		return true;
	}
	case enTypes:
		pvarRetValue = Types();
		return true;
	}

	return false;
}

//**********************************************************************
//*                            Qualifiers                              *
//**********************************************************************

#include "backend/compiler/enumUnit.h"
#include "backend/backend_exception.h"

// The two closed sets a qualifier is written with — so `QualifierDate` has something to be given at all.
class ibValueEnumDateFractions : public ibValueEnumeration<ibDateFractions> {
public:
	ibValueEnumDateFractions() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibDateFractions::ibDateFractions_Date,     wxT("Date"),     _("Date"));
		AddEnumeration(ibDateFractions::ibDateFractions_Time,     wxT("Time"),     _("Time"));
		AddEnumeration(ibDateFractions::ibDateFractions_DateTime, wxT("DateTime"), _("Date and time"));
	}
};

class ibValueEnumAllowedLength : public ibValueEnumeration<ibAllowedLength> {
public:
	ibValueEnumAllowedLength() : ibValueEnumeration() {}
	virtual void CreateEnumeration() override {
		AddEnumeration(ibAllowedLength::ibAllowedLength_Variable, wxT("Variable"), _("Variable"));
		AddEnumeration(ibAllowedLength::ibAllowedLength_Fixed,    wxT("Fixed"),    _("Fixed"));
	}
};

// 🛑 A QUALIFIER TAKES ITS ARGUMENTS. The three had no Init of their own, so the base answered every
// `New QualifierNumber(15, 2)` by ignoring what was given: the qualifier kept the default ten digits and
// no fraction, and `New TypeDescription("Number", New QualifierNumber(15, 2))` rounded 0.25 to 0 — no
// fraction could be declared from a script at all (measured 2026-09-21). Given nothing, a qualifier
// limits nothing, the same as a description that names none (ibValueTypeDescription::Unqualified).
bool ibValueQualifierNumber::Init()
{
	m_qNumber = ibValueTypeDescription::Unqualified().m_number;
	return true;
}

bool ibValueQualifierNumber::Init(ibValue** paParams, const long lSizeArray)
{
	const long precision = lSizeArray > 0 ? paParams[0]->GetInteger() : 0;
	const long scale     = lSizeArray > 1 ? paParams[1]->GetInteger() : 0;
	if (precision < 0 || precision > 38)
		ibBackendCoreException::Error(_("QualifierNumber: the number of digits is from 0 (no limit) to 38, not %d"), (int)precision);
	if (scale < 0 || (precision > 0 && scale > precision))
		ibBackendCoreException::Error(_("QualifierNumber: the digits after the point are from 0 to the number of digits, not %d"), (int)scale);
	m_qNumber = ibQualifierNumber(static_cast<unsigned char>(precision), static_cast<char>(scale),
		lSizeArray > 2 && paParams[2]->GetBoolean());
	return true;
}

bool ibValueQualifierDate::Init()
{
	m_qDate = ibValueTypeDescription::Unqualified().m_date;
	return true;
}

bool ibValueQualifierDate::Init(ibValue** paParams, const long lSizeArray)
{
	m_qDate = ibValueTypeDescription::Unqualified().m_date;
	if (lSizeArray > 0 && !paParams[0]->IsEmpty())
		m_qDate = ibQualifierDate(paParams[0]->ConvertToEnumValue<ibDateFractions>());
	return true;
}

bool ibValueQualifierString::Init()
{
	m_qString = ibValueTypeDescription::Unqualified().m_string;
	return true;
}

bool ibValueQualifierString::Init(ibValue** paParams, const long lSizeArray)
{
	const long length = lSizeArray > 0 ? paParams[0]->GetInteger() : 0;
	if (length < 0 || length > 65535)
		ibBackendCoreException::Error(_("QualifierString: the length is from 0 (no limit) to 65535, not %d"), (int)length);
	m_qString = ibQualifierString(static_cast<unsigned short>(length),
		lSizeArray > 1 && !paParams[1]->IsEmpty() ? paParams[1]->ConvertToEnumValue<ibAllowedLength>()
		                                          : ibAllowedLength::ibAllowedLength_Variable);
	return true;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

VALUE_TYPE_REGISTER(ibValueType, "Type", value_to_clsid("VL_TYPE"));
VALUE_TYPE_REGISTER(ibValueTypeDescription, "TypeDescription", value_to_clsid("VL_TYPED"));

VALUE_TYPE_REGISTER(ibValueQualifierNumber, "QualifierNumber", value_to_clsid("VL_QNUM"));
VALUE_TYPE_REGISTER(ibValueQualifierDate, "QualifierDate", value_to_clsid("VL_QDAT"));
VALUE_TYPE_REGISTER(ibValueQualifierString, "QualifierString", value_to_clsid("VL_QSTR"));

ENUM_TYPE_REGISTER(ibValueEnumDateFractions, "DateFractions", enum_to_clsid("EN_DFRAC"));
ENUM_TYPE_REGISTER(ibValueEnumAllowedLength, "AllowedLength", enum_to_clsid("EN_ALLEN"));
