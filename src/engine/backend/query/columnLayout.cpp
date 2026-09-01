#include "backend/query/columnLayout.h"
#include "backend/query/columnSpread.h"   // ibColumnSpread::DriveSpread — shared role-spread binding (value + wire codecs)

#include "backend/backend_core.h"    // emptyDate
#include "backend/metaData.h"        // ibMetaData::GetTypeCtor / GetAvailableCtor / CreateAndConvertObjectRef
#include "backend/objCtor.h"         // ibCtorMetaValueType / ibCtorObjectMetaType / ibCtorAbstractType
#include "backend/valueInfo.h"       // reference_size_t (= sizeof(ibReference)), ibReference
#include "backend/compiler/value.h"  // ibValue accessors + ibValuePtr
#include "backend/compiler/enumUnit.h"                              // ibValueEnumerationWrapper (enum read)
#include "backend/system/value/valueType.h"                        // ibValueTypeDescription::AdjustValue (typed-empty)
#include "backend/system/value/valuePointInTime.h"                 // g_valuePointInTimeCLSID — computed, never a column
#include "backend/metaCollection/partial/reference/reference.h"    // ibValueReferenceDataObject (reference assembly)
#include "backend/metaCollection/metaObject.h"                     // ibValueMetaObject (reference reconstruction)
#include "backend/databaseLayer/databaseLayerException.h"          // ibDatabaseLayerException — the "no such field" verdict
#include "backend/databaseLayer/databaseErrorCodes.h"              // DATABASE_LAYER_FIELD_NOT_IN_RESULTSET
// ibFieldTypes (the wire tag) now comes from query/queryColumn.h — the codec no longer depends on
// the attribute header (metaAttributeObject.h), only on the shared L3 column vocabulary.


#include <map>

// The single role -> suffix table (see columnLayout.h). Built once; an unknown role
// (only Raw) yields the empty suffix.
const wxString& ibFieldSuffix(ibColumnRole role)
{
	static const std::map<ibColumnRole, wxString> s_suffix = {
		{ ibColumnRole::Raw,           wxString()    },
		{ ibColumnRole::Discriminator, wxT("_TYPE")  },
		{ ibColumnRole::Boolean,       wxT("_B")     },
		{ ibColumnRole::Number,        wxT("_N")     },
		{ ibColumnRole::Date,          wxT("_D")     },
		{ ibColumnRole::String,        wxT("_S")     },
		{ ibColumnRole::Enum,          wxT("_E")     },
		{ ibColumnRole::ReferenceType, wxT("_RTRef") },
		{ ibColumnRole::ReferenceId,   wxT("_RRRef") },
		{ ibColumnRole::Schedule,      wxT("_SCH")   },
		{ ibColumnRole::TypeDescription, wxT("_TD")  },
	};
	static const wxString s_empty;
	const auto it = s_suffix.find(role);
	return it != s_suffix.end() ? it->second : s_empty;
}

wxString ibSqlAliasOf(const wxString& outputName)
{
	// THE PREFIX IS A NAMESPACE, NOT AN ESCAPE.
	//
	// These names are written INSIDE a projection of ours — a nested select, a declared query's body
	// — so they are ours to spell, all of them, and they are spelled in that projection's own
	// namespace. The prefix says where the name lives. That a prefixed word can no longer collide
	// with SQL's vocabulary is a CONSEQUENCE of saying so, not the purpose.
	//
	// 🛑 THE FIRST VERSION TESTED A WORD LIST and prefixed only on a hit. Two things were wrong with
	// it, and the second is the serious one:
	//   · the list is a guess — it has to be right for four dialects and stay right as each adds
	//     keywords, and nothing tells you it is incomplete;
	//   · a name that changed only for `Date` meant the writer/reader split was exercised almost
	//     never, so a reader that forgot to ask here would work for every query but one and fail
	//     SILENTLY there — an empty value, not a refusal. Prefixing everything makes the same
	//     mistake break loudly, on the first query anyone runs.
	//
	// ⭐ INJECTIVE, so distinct outputs stay distinct: `Date` -> `out_Date`, and an output actually
	// named `out_Date` -> `out_out_Date`. Stable, so the writer and the reader agree by computing it
	// rather than by passing the spelling between them.
	// An unnamed output has nothing to qualify — it is read by position, not by name.
	if (outputName.IsEmpty())
		return outputName;

	return wxT("out_") + outputName;
}

const wxString& ibOwnerRefField()
{
	// Built from the ONE suffix table rather than written out: this column and a reference key hold
	// the same sixteen bytes, and they stay spelled alike because they are spelled from one place.
	static const wxString s_field = wxT("Row") + ibFieldSuffix(ibColumnRole::ReferenceId);
	return s_field;
}

ibBackendColumnRawDB ibOwnerRefColumn()
{
	return ibBackendColumnRawDB::Guid(ibOwnerRefField());
}


int ibPersistedTypeTag(ibColumnRole role)
{
	switch (role) {
	case ibColumnRole::Boolean:       return ibFieldTypes_Boolean;
	case ibColumnRole::Number:        return ibFieldTypes_Number;
	case ibColumnRole::Date:          return ibFieldTypes_Date;
	case ibColumnRole::String:        return ibFieldTypes_String;
	case ibColumnRole::Enum:          return ibFieldTypes_Enum;
	case ibColumnRole::ReferenceType: return ibFieldTypes_Reference;
	case ibColumnRole::ReferenceId:   return ibFieldTypes_Reference;
	case ibColumnRole::Schedule:      return ibFieldTypes_Schedule;
	case ibColumnRole::TypeDescription: return ibFieldTypes_TypeDescription;
	default:                          return ibFieldTypes_Empty;   // Raw / Discriminator carry no tag
	}
}


// Kept here because this is where the layout decides what a column BECOMES physically — so the
// storage fact and the "can it be compared" fact cannot drift apart.
//
//   STORED WHOLE — a schedule, a type description: one BLOB field, and SQL compares no blobs.
//   NOT STORED AT ALL — the MOMENT (PointInTime): computed from the date and the reference, with no
//   field of its own, so there is nothing to compare either. It is in the field tree so a QUERY can
//   address it, and a query that WRITES its fields asks the column instead (Kind::Synthetic).
bool ibIsComparableType(const ibTypeDescription& typeDesc)
{
	return !typeDesc.ContainType(g_valueScheduleCLSID)
	    && !typeDesc.ContainType(g_valueTypeDescriptionCLSID)
	    && !typeDesc.ContainType(g_valuePointInTimeCLSID);
}

// THE AUTHORITY, and it now asks the COLUMN — which answers with the derivation below unless it has
// a reason not to. Kept as a free function because that is how every tier already spells the
// question; what changed is who owns the answer.
std::vector<ibColumnSlot> DescribeColumnLayout(const ibBackendQueryColumn* col)
{
	return col->DescribeLayout();
}

// (THE DEFAULT DERIVATION lives in queryColumn.cpp, with the other things a column DOES — together
//  with the two helpers only it used (the primitive slot's canonical type, and a raw column's carried
//  type). This file keeps the layout VOCABULARY and the value codec.)

// A column's VALUE fields — its layout's value-role slots (every role EXCEPT the _TYPE discriminator
// and the reference TYPE id). The ONE metadata-free authority for "which fields carry this column's
// value", derived over DescribeColumnLayout — it replaces the former per-class GetValueFields
// overrides (raw -> its one field; an attribute / temp column -> its composite spread): the role
// model already encodes each column's storage shape. A free TIER helper, not a column method — only
// the DB provider asks it (sort / group-by / key field expansion); the column stays a pure descriptor.
bool ibIsValueRole(ibColumnRole role)
{
	return role != ibColumnRole::Discriminator && role != ibColumnRole::ReferenceType;
}

std::vector<ibColumnSlot> ColumnValueSlots(const ibBackendQueryColumn* col)
{
	std::vector<ibColumnSlot> out;
	for (ibColumnSlot& slot : DescribeColumnLayout(col))
		if (ibIsValueRole(slot.m_role))
			out.push_back(std::move(slot));
	return out;
}

ibColumnSlot FirstValueSlot(const ibBackendQueryColumn* col)
{
	std::vector<ibColumnSlot> slots = ColumnValueSlots(col);
	return slots.empty() ? ibColumnSlot() : std::move(slots.front());
}

std::vector<wxString> ColumnValueFields(const ibBackendQueryColumn* col)
{
	std::vector<wxString> out;
	for (const ibColumnSlot& slot : ColumnValueSlots(col))
		out.push_back(slot.m_name);
	return out;
}

// ==========================================================================
// ibColumnCodec — the value <-> physical-fields codec (was ibDbTableProvider::Set/GetValueColumn).
// Lives here, next to the layout, so the field SHAPE (DescribeColumnLayout) and the field VALUES
// (this codec) share one home. The persisted variant tag (ibFieldTypes) stays local to this TU.
// ==========================================================================

ibValue ReadSingleTargetReference(const ibMetaData* metaData, const ibClassID& target,
                                  const wxMemoryBuffer& keyBytes)
{
	if (metaData == nullptr || target == 0 || keyBytes.GetDataLen() < sizeof(ibReference))
		return ibValue();

	// The key is all a query column carries; whoever asks the reference what it IS pays for the read.
	ibValuePtr<ibValueReferenceDataObject> reference(
		ibValueReferenceDataObject::Create(metaData, target, const_cast<void*>(keyBytes.GetData()),
			ibReferenceLoad::OnDemand));
	return reference != nullptr ? ibValue(reference) : ibValue();
}

bool ibColumnCodec::HasReference(const ibBackendQueryColumn* col)
{
	for (const auto& clsid : col->GetTypeValueDesc().GetClsidList())
		if (IsReference(clsid))
			return true;
	return false;
}

void ibColumnCodec::WriteValue(const ibBackendQueryColumn* col, const ibMetaData* /*metaData*/,
	const ibValue& cValue, ibQueryStatement* statement, int& position)
{
	// (the codec's variant tags are the global ibFieldTypes_* names — query/queryColumn.h)
	// WRITE needs NO metadata: the reference slot is gated by the clsid KIND (IsReference) and the
	// blob comes off the value itself. metaData stays on the signature only to mirror ReadValue.
	const int tag = ibColumnSpread::TagForValue(cValue);

	// Schedule payload — serialised once, bound at the _SCH slot. Read off the value itself, like
	// the reference blob below: no metadata, no session.
	wxMemoryBuffer scheduleBlob;
	if (tag == ibFieldTypes_Schedule) {
		ibValueSchedule* schedule = nullptr;
		if (cValue.ConvertToValue(schedule) && schedule != nullptr)
			ibJobScheduleDescriptionMemory::WriteBuffer(scheduleBlob, schedule->GetSchedule());
	}

	// Type-description payload — the same arrangement one line up: serialised once here, bound at
	// the _TD slot below. The value knows its own description; the codec only moves bytes.
	wxMemoryBuffer typeDescBlob;
	if (tag == ibFieldTypes_TypeDescription) {
		ibValueTypeDescription* typeValue = nullptr;
		if (cValue.ConvertToValue(typeValue) && typeValue != nullptr)
			ibTypeDescriptionMemory::WriteBuffer(typeDescBlob, typeValue->m_typeDesc);
	}

	// Reference payload — resolved once, bound at the _RTRef/_RRRef slots. A non-reference value
	// (or an unconvertible reffer) leaves it empty, so the pair binds 0 / NULL.
	ibClassID   refClsid = 0;
	const void* refBlob  = nullptr;
	if (tag == ibFieldTypes_Reference) {
		const ibClassID& clsid = cValue.GetClassType();
		wxASSERT(clsid > 0);
		// Kind read straight from the clsid — no metadata lookup on this per-value hot path.
		ibValueReferenceDataObject* refData = nullptr;
		if (IsReference(clsid) && cValue.ConvertToValue(refData)) {
			refClsid = clsid;
			refBlob  = refData->GetReferenceData();
		}
	}

	ibColumnSpread::DriveSpread(col, tag, statement, position,
		[&](ibColumnRole role, int& p) {   // ACTIVE primitive — the real value off the ibValue
			switch (role) {
			case ibColumnRole::Boolean: statement->SetParamBool(p++, cValue.GetBoolean()); break;
			case ibColumnRole::Number:  statement->SetParamNumber(p++, cValue.GetNumber()); break;
			case ibColumnRole::Date:    statement->SetParamDate(p++, cValue.GetDate()); break;
			case ibColumnRole::String:  statement->SetParamString(p++, cValue.GetString()); break;
			case ibColumnRole::Enum:    statement->SetParamInt(p++, cValue.GetInteger()); break;
			case ibColumnRole::Schedule:
				statement->SetParamBlob(p++, scheduleBlob.GetData(), scheduleBlob.GetDataLen());
				break;
			case ibColumnRole::TypeDescription:
				statement->SetParamBlob(p++, typeDescBlob.GetData(), typeDescBlob.GetDataLen());
				break;
			default:                                                                        break;
			}
		},
		[&](ibColumnRole role, int& p) {   // reference pair (_RTRef clsid, _RRRef blob)
			if (role == ibColumnRole::ReferenceType)      statement->SetParamNumber(p++, refClsid);
			else if (refBlob != nullptr)                  statement->SetParamBlob(p++, refBlob, sizeof(ibReference));
			else                                          statement->SetParamNull(p++);
		});
}

void ibColumnCodec::WriteValue(const ibBackendQueryColumn* col, const ibMetaData* metaData, const ibValue& cValue, ibQueryStatement* statement)
{
	int position = 1;
	WriteValue(col, metaData, cValue, statement, position);
}

// Read ONE physical field of a known variant type into the value. The leaf of the read codec —
// `fieldType` is the persisted ibFieldTypes tag passed as a plain int, so the header stays light.
bool ibColumnCodec::ReadField(const wxString& fieldName, int fieldType,
	const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	switch (fieldType)
	{
	case ibFieldTypes_Boolean:
		retValue = result.GetResultBool(fieldName);
		return true;
	case ibFieldTypes_Number:
		retValue = result.GetResultNumber(fieldName);
		return true;
	case ibFieldTypes_Date:
		retValue = result.GetResultDate(fieldName);
		return true;
	case ibFieldTypes_String:
		retValue = result.GetResultString(fieldName);
		return true;
	case ibFieldTypes_Null:
		retValue = ibValue(ibValueTypes::TYPE_NULL);   // fresh NULL — releases any prior reffer/string in the slot (operator=(ibValueTypes) would leak it)
		return true;
	case ibFieldTypes_Schedule:
	{
		// The blob IS the value. An empty or unreadable one yields a DEFAULT schedule rather than
		// an empty value: a row whose cell was never written must still answer "when am I due",
		// and the description's own defaults are that answer (every one of them = "not restricted").
		wxMemoryBuffer bufferData;
		result.GetResultBlob(fieldName, bufferData);

		ibJobScheduleDescription schedule;
		ibJobScheduleDescriptionMemory::ReadBuffer(bufferData.GetData(), bufferData.GetDataLen(), schedule);

		ibValuePtr<ibValueSchedule> created(new ibValueSchedule(schedule));
		retValue = created;
		return true;
	}
	case ibFieldTypes_TypeDescription:
	{
		// The blob IS the value, exactly as above. An empty or unreadable cell yields an EMPTY
		// description rather than the column's own type: a characteristic that names no type is a
		// defect the write refuses, so reading one back must not manufacture a plausible answer —
		// it would look like the narrowing worked while admitting everything.
		wxMemoryBuffer bufferData;
		result.GetResultBlob(fieldName, bufferData);

		ibTypeDescription typeDesc;
		ibTypeDescriptionMemory::ReadBuffer(bufferData.GetData(), bufferData.GetDataLen(), typeDesc);

		ibValuePtr<ibValueTypeDescription> created(new ibValueTypeDescription(typeDesc));
		retValue = created;
		return true;
	}
	case ibFieldTypes_Enum:
	{
		wxASSERT(metaData);
		// The enum ctor is keyed by the ENUM clsid — taken from the column's type descriptor. The
		// error fallback yields an empty value (an unreadable enum degrades to empty).
		const ibClassID enumClsid = col->GetTypeDesc().GetFirstClsid();
		const ibCtorAbstractType* so = metaData != nullptr ? metaData->GetAvailableCtor(enumClsid) : nullptr;

		if (so != nullptr) {

			ibValue enumVariant(result.GetResultInt(fieldName));
			ibValue* ppParams[] = { &enumVariant };

			try {
				ibValuePtr<ibValueEnumerationWrapper> creator(
					metaData->CreateAndConvertObjectRef<ibValueEnumerationWrapper>(so->GetClassName(), ppParams, 1));
				retValue = creator->GetEnumVariantValue();
			}
			catch (...) {
				retValue = ibValue();
				return false;
			}

			return true;
		}

		retValue = ibValue();
		return false;
	}
	case ibFieldTypes_Reference:
	{
		wxASSERT(metaData);
		if (metaData == nullptr)
			return false;
		const ibClassID refType = static_cast<ibClassID>(result.GetResultLong(fieldName + ibFieldSuffix(ibColumnRole::ReferenceType)));

		wxMemoryBuffer bufferData;
		result.GetResultBlob(fieldName + ibFieldSuffix(ibColumnRole::ReferenceId), bufferData);
		if (!bufferData.IsEmpty()) {

			// Reading the row is what `createData` asks for here — it never meant "settle it", which is
			// why the reference stays unlatched and a later use may read it again. This used to be an
			// if/else over two differently NAMED creation functions, and neither name said anything
			// about reading; the branch is the argument now.
			ibValuePtr<ibValueReferenceDataObject> created_reference(
				ibValueReferenceDataObject::Create(metaData, refType, bufferData.GetData(),
					createData ? ibReferenceLoad::Unlatched : ibReferenceLoad::OnDemand));

			retValue = created_reference;
			return created_reference != nullptr;
		}
		else if (refType > 0) {

			const ibCtorMetaValueType* typeCtor = metaData->GetTypeCtor(refType);
			if (typeCtor != nullptr) {

				const ibValueMetaObject* metaObject = typeCtor->GetMetaObject();
				wxASSERT(metaObject);

				ibValuePtr<ibValueReferenceDataObject> created_reference(
					ibValueReferenceDataObject::Create(metaData, metaObject->GetMetaID()));

				retValue = created_reference;
				return created_reference != nullptr;
			}

			return false;
		}

		// Empty _RRRef and no refType — the reference is empty / its dot-walk join did not match: the
		// column's TYPED EMPTY value, not UNDEFINED.
		retValue = (col != nullptr) ? ibValueTypeDescription::AdjustValue(col->GetTypeDesc()) : ibValue();
		return true;
	}
	}

	return false;
}

// ⭐⭐ DOES THE STORED TAG NAME A FIELD THIS COLUMN ACTUALLY HAS?
//
// The _TYPE tag decides which sub-field the read then asks the driver for BY NAME, and the sub-fields
// that exist are decided by the column's TYPE — two facts that must agree and, once a row is written,
// no longer can be made to. A row carrying a tag the column cannot spread to (the Reference tag on an
// enum column, written by the shape bug in TagForValue) sent the read after `fld<n>_RTRef`, the driver
// raised "field not found in the resultset", and the LIST FETCH that swallows it showed an empty page:
// one bad row, and nothing at all is displayed.
//
// So the tag is checked against the layout before it is believed. A tag that does not fit reads as the
// column's TYPED EMPTY value — the same answer an untagged cell gets — which keeps rows already in the
// database readable instead of requiring them to be rewritten. Asked through the same ContainType /
// HasReference gates DescribeColumnLayout builds the slots from, so the two cannot drift apart.
static bool TagFitsColumn(const ibBackendQueryColumn* col, ibFieldTypes tag)
{
	const ibTypeDescription& td = col->GetTypeValueDesc();
	switch (tag) {
	case ibFieldTypes_Boolean:         return td.ContainType(ibValueTypes::TYPE_BOOLEAN);
	case ibFieldTypes_Number:          return td.ContainType(ibValueTypes::TYPE_NUMBER);
	case ibFieldTypes_Date:            return td.ContainType(ibValueTypes::TYPE_DATE);
	case ibFieldTypes_String:          return td.ContainType(ibValueTypes::TYPE_STRING);
	case ibFieldTypes_Enum:            return td.ContainType(ibValueTypes::TYPE_ENUM);
	case ibFieldTypes_Schedule:        return td.ContainType(g_valueScheduleCLSID);
	case ibFieldTypes_TypeDescription: return td.ContainType(g_valueTypeDescriptionCLSID);
	case ibFieldTypes_Reference:       return ibColumnCodec::HasReference(col);
	default:                           return true;   // Empty / Null read no sub-field at all
	}
}

// ⭐ "I HAVE NO SUCH VALUE" IS AN ANSWER, NOT A CATASTROPHE.
//
// The result set not carrying a field this column needs is a fact about ONE cell — the codec says so
// the way it already says everything else, by returning false with the column's typed empty in hand,
// and the CALLER decides: a portion read swallows it and keeps the other rows, a write or a targeted
// read lets it travel. Before, it propagated no matter who asked, so a single unreadable cell took a
// whole list page with it.
//
// Costs nothing on the normal path — the guard is an exception handler, entered only when the fault
// actually happens — and the ordinary form of this fault does not even reach it: TagFitsColumn above
// answers from the column's own type before any sub-field is named.
//
// ONLY this fault. A dropped connection or a driver failure is a fault of the READ, not of the cell,
// and still travels: degrading those would paint blank rows over a database that stopped answering.
bool ibColumnCodec::ReadValue(const wxString& fieldName,
	const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	try {
		return ReadTaggedValue(fieldName, col, metaData, retValue, result, createData);
	}
	catch (const ibDatabaseLayerException& err) {
		if (err.GetDriverErrorCode() != DATABASE_LAYER_FIELD_NOT_IN_RESULTSET)
			throw;
		retValue = (col != nullptr) ? ibValueTypeDescription::AdjustValue(col->GetTypeDesc()) : ibValue();
		return false;
	}
}

bool ibColumnCodec::ReadTaggedValue(const wxString& fieldName,
	const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	// ⭐ A COMPUTED OUTPUT HAS ONE FIELD AND NO TAG BESIDE IT.
	//
	// `15 AS x`, `a * b AS y` — the expression is projected under its alias and nothing declares a
	// `_TYPE` next to it (DescribeLayout emits the single slot, queryColumn.cpp). So the tag cannot be
	// READ here; it comes from what the output IS — the type the lowering gave the schema when it
	// resolved the expression.
	//
	// Without this the read asked for a discriminator that was never projected, the driver answered
	// "field not found", and ReadValue below turned that into an empty cell — silently, 496 times in
	// one report (Max, 2026-08-24: a computed field is a legitimate grouping key).
	//
	// Gated on the type being KNOWN: an output the lowering could not type yet keeps the old road
	// rather than being read as a type nobody vouched for.
	if (col != nullptr && col->GetColumnKind() == ibBackendQueryColumn::Kind::Computed
	    && col->GetTypeDesc().GetClsidCount() == 1) {
		const ibFieldTypes tag = ibColumnSpread::TagForValueType(ibValue::GetVTByID(col->GetTypeDesc().GetByIdx(0)));
		return ReadField(fieldName, tag, col, metaData, retValue, result, createData);
	}

	ibFieldTypes fieldType =
		static_cast<ibFieldTypes>(result.GetResultInt(fieldName + ibFieldSuffix(ibColumnRole::Discriminator)));

	if (col != nullptr && !TagFitsColumn(col, fieldType)) {
		retValue = ibValueTypeDescription::AdjustValue(col->GetTypeDesc());
		return true;
	}

	switch (fieldType)
	{
	case ibFieldTypes_Boolean:
		return ReadField(fieldName + ibFieldSuffix(ibColumnRole::Boolean), ibFieldTypes_Boolean, col, metaData, retValue, result, createData);
	case ibFieldTypes_Number:
		return ReadField(fieldName + ibFieldSuffix(ibColumnRole::Number), ibFieldTypes_Number, col, metaData, retValue, result, createData);
	case ibFieldTypes_Date:
		return ReadField(fieldName + ibFieldSuffix(ibColumnRole::Date), ibFieldTypes_Date, col, metaData, retValue, result, createData);
	case ibFieldTypes_String:
		return ReadField(fieldName + ibFieldSuffix(ibColumnRole::String), ibFieldTypes_String, col, metaData, retValue, result, createData);
	case ibFieldTypes_Null:
		retValue = ibValue(ibValueTypes::TYPE_NULL);   // fresh NULL — releases any prior reffer/string in the slot (operator=(ibValueTypes) would leak it)
		return true;
	case ibFieldTypes_Schedule:
		return ReadField(fieldName + ibFieldSuffix(ibColumnRole::Schedule), ibFieldTypes_Schedule, col, metaData, retValue, result, createData);
	case ibFieldTypes_TypeDescription:
		return ReadField(fieldName + ibFieldSuffix(ibColumnRole::TypeDescription), ibFieldTypes_TypeDescription, col, metaData, retValue, result, createData);
	case ibFieldTypes_Enum:
		return ReadField(fieldName + ibFieldSuffix(ibColumnRole::Enum), ibFieldTypes_Enum, col, metaData, retValue, result, createData);
	case ibFieldTypes_Reference:
		return ReadField(fieldName, ibFieldTypes_Reference, col, metaData, retValue, result, createData);
	default:
		// The stored TYPE tag is NULL / untagged (0): no value in this cell — a fresh row, or a dot-walk
		// through an empty / broken reference whose LEFT JOIN did not match. Yield the COLUMN'S TYPED EMPTY
		// empty value, never UNDEFINED, and NEVER read a sub-field the column lacks (a number column has no
		// _RRRef). A real reference value tags _TYPE = Reference and takes the case above.
		// (docs/query-language-arc.md §22.4b — typed-empty dot-walk)
		retValue = (col != nullptr) ? ibValueTypeDescription::AdjustValue(col->GetTypeDesc()) : ibValue();
		return true;
	}
	// (No tail return: the `default` above answers every tag there is, so one here is unreachable —
	//  and an unreachable `return false` reads as a failure mode this function does not have.)
}

bool ibColumnCodec::ReadValue(const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	return ReadValue(col->GetPhysicalName(), col, metaData, retValue, result, createData);
}

// (THE DEFAULT READ / BIND live in queryColumn.cpp — what a column DOES is kept apart from the layout
//  it HAS. Both forward to the codec above; a column that is not stored the ordinary way overrides
//  them there.)

// ==========================================================================
// SQL fragment builders — the field spellings the hand-written query / upsert SQL used to
// assemble per-attribute, now derived once from the layout (same field SET + ORDER as the codec).
// ==========================================================================

std::vector<wxString> ColumnFieldNames(const ibBackendQueryColumn* col)
{
	std::vector<wxString> out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(col))
		out.push_back(slot.m_name);
	return out;
}

wxString ColumnFieldList(const ibBackendQueryColumn* col, const wxString& aggr)
{
	wxString out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(col)) {
		// The numeric / date MEASURES carry the aggregate wrapper (SUM(f_N) AS f_N); the tag /
		// dimension fields stay bare — mirrors the former GetSQLFieldName.
		const bool measure = (slot.m_role == ibColumnRole::Number || slot.m_role == ibColumnRole::Date);
		const wxString f = (!aggr.empty() && measure)
		                 ? aggr + wxT("(") + slot.m_name + wxT(") AS ") + slot.m_name
		                 : slot.m_name;
		out += out.empty() ? f : (wxT(",") + f);
	}
	return out;
}

wxString ColumnComparePredicate(const ibBackendQueryColumn* col, const wxString& cmp)
{
	wxString out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(col)) {
		// The discriminator and the reference TYPE id always match by equality; the value fields
		// (and the reference blob) use the caller's comparator — mirrors GetCompositeSQLFieldName.
		const bool eq = (slot.m_role == ibColumnRole::Discriminator || slot.m_role == ibColumnRole::ReferenceType);
		const wxString term = slot.m_name + wxT(" ") + (eq ? wxString(wxT("=")) : cmp) + wxT(" ?");
		out += out.empty() ? term : (wxT(" AND ") + term);
	}
	return out;
}


// (The binary-wire codec — BinaryToStatement / BinaryFromResult — moved to the L3-3 data mover
//  (query/dataMover.{h,cpp}): it is the dump / restore PRIMITIVE, and its only consumers are the
//  mover and the constant's single-cell dump. It still drives ibColumnSpread::DriveSpread + this
//  codec's HasReference, so the value codec and the wire codec stay byte-identical.)
