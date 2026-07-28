#include "backend/query/columnLayout.h"
#include "backend/query/columnSpread.h"   // ibColumnSpread::DriveSpread — shared role-spread binding (value + wire codecs)

#include "backend/backend_core.h"    // emptyDate
#include "backend/metaData.h"        // ibMetaData::GetTypeCtor / GetAvailableCtor / CreateAndConvertObjectRef
#include "backend/objCtor.h"         // ibCtorMetaValueType / ibCtorObjectMetaType / ibCtorAbstractType
#include "backend/valueInfo.h"       // reference_size_t (= sizeof(ibReference)), ibReference
#include "backend/compiler/value.h"  // ibValue accessors + ibValuePtr
#include "backend/compiler/enumUnit.h"                              // ibValueEnumerationWrapper (enum read)
#include "backend/system/value/valueType.h"                        // ibValueTypeDescription::AdjustValue (typed-empty)
#include "backend/metaCollection/partial/reference/reference.h"    // ibValueReferenceDataObject (reference assembly)
#include "backend/metaCollection/metaObject.h"                     // ibValueMetaObject (reference reconstruction)
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
	};
	static const wxString s_empty;
	const auto it = s_suffix.find(role);
	return it != s_suffix.end() ? it->second : s_empty;
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
	default:                          return ibFieldTypes_Empty;   // Raw / Discriminator carry no tag
	}
}

namespace {

// The canonical DDL type of one primitive slot, read off the column's type qualifiers
// (precision / scale / length / fixed / date fraction). The SAME mapping the former
// ibValueMetaObjectAttributeBase::GetSQLTypeObject did, now expressed as L2 canonical
// types so the dialect dictionary closes the per-DBMS spelling — no BYTEA/BLOB,
// DATE/TIMESTAMP, CHAR/VARCHAR forks here.
ibColumnType PrimitiveType(ibValueTypes vt, const ibTypeDescription& td)
{
	switch (vt) {
	case ibValueTypes::TYPE_BOOLEAN:
		return ibTypeBoolean();
	case ibValueTypes::TYPE_NUMBER:
		return ibTypeNumber(td.GetPrecision(), td.GetScale());
	case ibValueTypes::TYPE_DATE:
		switch (td.GetDateFraction()) {
		case ibDateFractions::ibDateFractions_Date: return ibTypeDate(ibDatePrec::Date);
		case ibDateFractions::ibDateFractions_Time: return ibTypeDate(ibDatePrec::Time);
		default:                                    return ibTypeDate(ibDatePrec::DateTime);
		}
	case ibValueTypes::TYPE_STRING:
		return td.GetAllowedLength() == ibAllowedLength::ibAllowedLength_Fixed
		     ? ibTypeChar(td.GetLength())
		     : ibTypeString(td.GetLength());
	case ibValueTypes::TYPE_ENUM:
		return ibTypeInteger();   // enum stored as its int ordinal
	default:
		return ibTypeInteger();
	}
}

ibColumnType RawType(const ibBackendQueryColumn* col)
{
	const auto* raw = dynamic_cast<const ibRawDBColumn*>(col);
	if (raw == nullptr)
		return ibTypeString(255);   // unknown raw column — a safe default; raw DDL is rare
	switch (raw->GetRawType()) {
	case ibRawDBColumn::RawType::String:  return ibTypeString(255);
	case ibRawDBColumn::RawType::Number:  return ibTypeNumber(18, 0);
	case ibRawDBColumn::RawType::Reference: return ibTypeBinary(reference_size_t);   // _RRRef fixed key
	case ibRawDBColumn::RawType::Date:    return ibTypeDate();
	case ibRawDBColumn::RawType::Boolean: return ibTypeBoolean();
	case ibRawDBColumn::RawType::Guid:    return ibTypeGuid();   // the uuid row-key
	case ibRawDBColumn::RawType::Blob:    return ibTypeBlob();   // a register's rowData
	}
	return ibTypeString(255);
}

} // namespace

std::vector<ibColumnSlot> DescribeColumnLayout(const ibBackendQueryColumn* col)
{
	std::vector<ibColumnSlot> slots;

	// A raw column is a single physical field with its own carried type — no spread.
	if (col->IsRawColumn()) {
		ibColumnSlot slot;
		slot.m_name = col->GetPhysicalName();
		slot.m_role = ibColumnRole::Raw;
		slot.m_type = RawType(col);
		slots.push_back(std::move(slot));
		return slots;
	}

	const ibTypeDescription& td = col->GetTypeDesc();
	const wxString base = col->GetPhysicalName();

	// A slot named off its role through the one suffix table.
	auto makeSlot = [&](ibColumnRole role, ibColumnType type, const wxString& def, bool notNull) {
		ibColumnSlot slot;
		slot.m_name    = base + ibFieldSuffix(role);
		slot.m_role    = role;
		slot.m_type    = std::move(type);
		slot.m_default = def;
		slot.m_notNull = notNull;
		return slot;
	};

	// _TYPE — the variant discriminator. Always present, DEFAULT 0 NOT NULL.
	slots.push_back(makeSlot(ibColumnRole::Discriminator, ibTypeInteger(), wxT("0"), true));

	// Primitive value slots — in the FIXED order B, N, D, S, E (load-bearing: matches the
	// keyset anchor + the write/read bind order). Enum carries DEFAULT 0 (undefined ordinal).
	auto pushPrim = [&](ibValueTypes vt, ibColumnRole role, const wxString& def) {
		if (td.ContainType(vt))
			slots.push_back(makeSlot(role, PrimitiveType(vt, td), def, false));
	};
	pushPrim(ibValueTypes::TYPE_BOOLEAN, ibColumnRole::Boolean, wxString());
	pushPrim(ibValueTypes::TYPE_NUMBER,  ibColumnRole::Number,  wxString());
	pushPrim(ibValueTypes::TYPE_DATE,    ibColumnRole::Date,    wxString());
	pushPrim(ibValueTypes::TYPE_STRING,  ibColumnRole::String,  wxString());
	pushPrim(ibValueTypes::TYPE_ENUM,    ibColumnRole::Enum,    wxT("0"));

	// Reference pair — _RTRef (target clsid, BIGINT) + _RRRef (pure guid blob, fixed-width
	// BINARY so it is indexable for the dot-walk = join). Present when ANY clsid in the type
	// is a reference target.
	bool hasReference = false;
	for (const auto& clsid : td.GetClsidList())
		if (IsReference(clsid)) { hasReference = true; break; }
	if (hasReference) {
		slots.push_back(makeSlot(ibColumnRole::ReferenceType, ibTypeBigInt(),                wxString(), false));
		slots.push_back(makeSlot(ibColumnRole::ReferenceId,   ibTypeBinary(reference_size_t), wxString(), false));
	}

	return slots;
}

// A column's VALUE fields — its layout's value-role slots (every role EXCEPT the _TYPE discriminator
// and the reference TYPE id). The ONE metadata-free authority for "which fields carry this column's
// value", derived over DescribeColumnLayout — it replaces the former per-class GetValueFields
// overrides (raw -> its one field; an attribute / temp column -> its composite spread): the role
// model already encodes each column's storage shape. A free TIER helper, not a column method — only
// the DB provider asks it (sort / group-by / key field expansion); the column stays a pure descriptor.
std::vector<wxString> ColumnValueFields(const ibBackendQueryColumn* col)
{
	std::vector<wxString> out;
	for (const ibColumnSlot& slot : DescribeColumnLayout(col))
		if (slot.m_role != ibColumnRole::Discriminator && slot.m_role != ibColumnRole::ReferenceType)
			out.push_back(slot.m_name);
	return out;
}

// ==========================================================================
// ibColumnCodec — the value <-> physical-fields codec (was ibDbTableProvider::Set/GetValueColumn).
// Lives here, next to the layout, so the field SHAPE (DescribeColumnLayout) and the field VALUES
// (this codec) share one home. The persisted variant tag (ibFieldTypes) stays local to this TU.
// ==========================================================================

bool ibColumnCodec::HasReference(const ibBackendQueryColumn* col)
{
	for (const auto& clsid : col->GetTypeDesc().GetClsidList())
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
	const int tag = ibColumnSpread::TagForValueType(cValue.GetType());

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

			if (createData) {

				ibValuePtr<ibValueReferenceDataObject> created_reference(
					ibValueReferenceDataObject::CreateFromPtr(metaData, refType, bufferData.GetData()));

				retValue = created_reference;
				return created_reference != nullptr;
			}

			ibValuePtr<ibValueReferenceDataObject> created_reference(
				ibValueReferenceDataObject::Create(metaData, refType, bufferData.GetData()));

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

bool ibColumnCodec::ReadValue(const wxString& fieldName,
	const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	ibFieldTypes fieldType =
		static_cast<ibFieldTypes>(result.GetResultInt(fieldName + ibFieldSuffix(ibColumnRole::Discriminator)));

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

	return false;
}

bool ibColumnCodec::ReadValue(const ibBackendQueryColumn* col, const ibMetaData* metaData, ibValue& retValue, ibQueryResult& result, bool createData)
{
	return ReadValue(col->GetPhysicalName(), col, metaData, retValue, result, createData);
}

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
