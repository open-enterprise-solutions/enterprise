#include "backend/query/typeChangeReport.h"

#include "backend/backend_exception.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"
#include "backend/metaCollection/partial/reference/reference.h"
#include "backend/query/columnLayout.h"
#include "backend/query/queryable.h"
#include "backend/query/schemaSnapshot.h"
#include "backend/restructureInfo.h"
#include "backend/system/systemManager.h"
#include "backend/system/value/valueType.h"

#include <map>

namespace {

constexpr int kSampleCap = 5;

bool IsBlank(const ibValue& stored)
{
	switch (stored.GetType()) {
	case ibValueTypes::TYPE_EMPTY:
	case ibValueTypes::TYPE_NULL:
		return true;
	case ibValueTypes::TYPE_STRING: {
		wxString text = stored.GetString();
		text.Trim(true);
		text.Trim(false);
		return text.IsEmpty();
	}
	case ibValueTypes::TYPE_DATE:
		return stored.GetDate().IsEmpty();
	case ibValueTypes::TYPE_CONST_REFFER:
	case ibValueTypes::TYPE_REFFER:
		return stored.IsEmpty();
	default:
		// False and zero are stored values. They are not an empty cell.
		return false;
	}
}

bool IsOnly(const ibTypeDescription& type, ibValueTypes vt)
{
	return type.GetClsidCount() == 1 && type.GetFirstClsid() == ibValue::GetIDByVT(vt);
}

wxString Trimmed(const wxString& text)
{
	wxString out = text;
	out.Trim(true);
	out.Trim(false);
	return out;
}

// The whole trimmed text is a number. FromString stops at the first character
// it does not want and still returns true, so "10 pcs" and "12x" must be
// rejected here rather than trusted to it.
bool WholeNumber(const wxString& text, ibNumber& out)
{
	const wxString trimmed = Trimmed(text);
	if (trimmed.IsEmpty())
		return false;
	const wchar_t* p = trimmed.wc_str();
	if (*p == L'+' || *p == L'-')
		++p;
	bool digit = false;
	while (*p >= L'0' && *p <= L'9') {
		digit = true;
		++p;
	}
	if (*p == L'.') {
		++p;
		while (*p >= L'0' && *p <= L'9') {
			digit = true;
			++p;
		}
	}
	if (*p == L'e' || *p == L'E') {
		++p;
		if (*p == L'+' || *p == L'-')
			++p;
		bool exp = false;
		while (*p >= L'0' && *p <= L'9') {
			exp = true;
			++p;
		}
		if (!exp)
			return false;
	}
	if (*p != 0 || !digit)
		return false;
	wxString upper = trimmed;
	upper.MakeUpper();
	return out.FromString(upper);
}

bool WholeBoolean(const wxString& text)
{
	const wxString trimmed = Trimmed(text);
	return trimmed.CmpNoCase(wxT("True")) == 0 || trimmed.CmpNoCase(wxT("False")) == 0;
}

// A date parse that stops after a prefix is not a date. wx's free-form reader
// reports success on the prefix; the rest of the text still has to be empty.
bool WholeDate(const wxString& text, ibDateTime& out)
{
	const wxString trimmed = Trimmed(text);
	if (trimmed.IsEmpty() || !out.FromString(trimmed))
		return false;
	wxDateTime parsed;
	const wxChar* end = parsed.ParseDateTime(trimmed);
	if (end == nullptr)
		end = parsed.ParseDate(trimmed);
	if (end != nullptr) {
		while (*end == wxT(' ') || *end == wxT('\t'))
			++end;
		if (*end != 0)
			return false;
	}
	return true;
}

wxString GroupedCount(std::int64_t count)
{
	wxString digits;
	digits << count;
	wxString grouped;
	int placed = 0;
	for (int i = static_cast<int>(digits.length()) - 1; i >= 0; --i) {
		if (placed > 0 && placed % 3 == 0)
			grouped = wxT(" ") + grouped;
		grouped = wxString(digits[i]) + grouped;
		++placed;
	}
	return grouped;
}

void DigitParts(const ibNumber& number, int& integerDigits, int& fractionDigits)
{
	integerDigits = 0;
	fractionDigits = 0;
	if (number.IsZero())
		return;
	wxString text = number.ToString();
	if (text.StartsWith(wxT("-")))
		text = text.Mid(1);
	const int dot = text.Find(wxT('.'));
	const wxString integer = dot == wxNOT_FOUND ? text : text.Left(dot);
	const wxString fraction = dot == wxNOT_FOUND ? wxString() : text.Mid(dot + 1);
	integerDigits = (integer.IsEmpty() || integer == wxT("0")) ? 0 : static_cast<int>(integer.length());
	fractionDigits = static_cast<int>(fraction.length());
}

bool NumberOverflows(const ibNumber& number, int precision, int scale)
{
	if (precision <= 0 || number.IsZero())
		return false;
	int integerDigits = 0;
	int fractionDigits = 0;
	DigitParts(number, integerDigits, fractionDigits);
	const int room = precision - scale;
	if (room < 0 || integerDigits > room)
		return true;
	return fractionDigits > scale;
}

ibTypeChangeFate LostWipe()
{
	ibTypeChangeFate fate;
	fate.state = ibTypeChangeState::Lost;
	fate.wipe = true;
	return fate;
}

ibTypeChangeFate LostNarrowed(const ibValue& converted)
{
	ibTypeChangeFate fate;
	fate.state = ibTypeChangeState::Lost;
	fate.narrowed = true;
	fate.value = converted;
	return fate;
}

wxString SampleText(const ibValue& stored)
{
	if (stored.IsReference()) {
		if (auto* ref = dynamic_cast<ibValueReferenceDataObject*>(stored.GetRef()))
			return ref->GetGuid().GetGuid().str();
		return stored.GetString();
	}
	return stored.GetString();
}

const ibMetaData* MetaOf(const ibSchemaTable& table)
{
	return table.m_queryable != nullptr ? table.m_queryable->GetMetaData() : nullptr;
}

wxString ObjectName(const ibSchemaTable& table)
{
	if (table.m_queryable != nullptr) {
		const wxString name = table.m_queryable->GetQueryName();
		if (!name.IsEmpty())
			return name;
	}
	return table.m_name;
}

wxString AttributeName(const ibBackendQueryColumn* column)
{
	if (column == nullptr)
		return wxString();
	const wxString name = column->GetName();
	return name.IsEmpty() ? column->GetPhysicalName() : name;
}

bool TagIsAbsent(ibQueryResult& cursor, const ibBackendQueryColumn* column)
{
	for (const ibColumnSlot& slot : DescribeColumnLayout(column)) {
		if (slot.m_role != ibColumnRole::Discriminator)
			continue;
		if (cursor.IsResultNull(slot.m_name))
			return true;
		const int tag = cursor.GetResultInt(slot.m_name);
		return tag == ibFieldTypes_Empty || tag == ibFieldTypes_Null;
	}
	return false;
}

void AddField(std::vector<wxString>& fields, const wxString& name)
{
	if (name.IsEmpty())
		return;
	for (const wxString& have : fields)
		if (have.IsSameAs(name, false))
			return;
	fields.push_back(name);
}

wxString RowLabel(ibQueryResult& cursor, const std::vector<const ibBackendQueryColumn*>& key, int row)
{
	wxString out;
	for (const ibBackendQueryColumn* column : key) {
		if (column == nullptr)
			continue;
		for (const ibColumnSlot& slot : DescribeColumnLayout(column)) {
			if (slot.m_role == ibColumnRole::Discriminator || cursor.IsResultNull(slot.m_name))
				continue;
			const wxString part = cursor.GetResultString(slot.m_name);
			if (part.IsEmpty())
				continue;
			if (!out.IsEmpty())
				out << wxT("/");
			out << part;
		}
	}
	if (out.IsEmpty())
		out << row;
	return out;
}

struct ChangedColumn {
	const ibBackendQueryColumn* oldColumn = nullptr;
	const ibBackendQueryColumn* newColumn = nullptr;
};

struct ChangedTable {
	const ibSchemaTable* was = nullptr;
	const ibSchemaTable* next = nullptr;
	std::vector<ChangedColumn> columns;
};

} // namespace

ibTypeChangeFate ibAssessTypeChange(const ibValue& stored, const ibTypeDescription& next,
                                    const ibMetaData* oldMeta, const ibMetaData* newMeta)
{
	(void)oldMeta;
	ibTypeChangeFate fate;
	if (!next.IsOk() || IsBlank(stored)) {
		fate.state = ibTypeChangeState::Absent;
		return fate;
	}

	const ibValueTypes from = stored.GetType();

	if (from == ibValueTypes::TYPE_STRING && IsOnly(next, ibValueTypes::TYPE_NUMBER)) {
		ibNumber number;
		if (!WholeNumber(stored.GetString(), number))
			return LostWipe();
	}
	if (from == ibValueTypes::TYPE_STRING && IsOnly(next, ibValueTypes::TYPE_BOOLEAN)) {
		if (!WholeBoolean(stored.GetString()))
			return LostWipe();
	}
	if (from == ibValueTypes::TYPE_STRING && IsOnly(next, ibValueTypes::TYPE_DATE)) {
		ibDateTime date;
		if (!WholeDate(stored.GetString(), date))
			return LostWipe();
	}

	if (stored.IsReference()) {
		if (!ibValueTypeDescription::AllowValue(next, stored.GetClassType(), newMeta))
			return LostWipe();
		fate.state = ibTypeChangeState::Kept;
		fate.value = stored;
		return fate;
	}

	ibValue converted;
	try {
		converted = ibValueTypeDescription::AdjustValue(next, stored, newMeta);
	}
	catch (const ibBackendException&) {
		return LostWipe();
	}

	if (IsBlank(converted))
		return LostWipe();

	bool narrowed = false;

	if (from == ibValueTypes::TYPE_NUMBER && (IsOnly(next, ibValueTypes::TYPE_NUMBER) || next.ContainType(ibValueTypes::TYPE_NUMBER))
	    && converted.GetType() == ibValueTypes::TYPE_NUMBER) {
		const int precision = next.GetPrecision();
		const int scale = next.GetScale();
		if (precision > 0) {
			const ibNumber rounded = ibValueSystemFunction::Round(stored, scale);
			if (!(rounded == stored.GetNumber()))
				narrowed = true;
			if (NumberOverflows(rounded, precision, scale))
				return LostWipe();
			converted = ibValue(rounded);
		}
	}

	if (converted.GetType() == ibValueTypes::TYPE_STRING && next.GetLength() > 0) {
		const wxString text = from == ibValueTypes::TYPE_STRING ? stored.GetString() : converted.GetString();
		if (static_cast<int>(text.length()) > next.GetLength())
			narrowed = true;
	}

	if (from == ibValueTypes::TYPE_DATE && converted.GetType() == ibValueTypes::TYPE_DATE) {
		const ibDateFractions fraction = next.GetDateFraction();
		const ibDateTime date = stored.GetDate();
		const ibDateTime day = ibValueSystemFunction::BegOfDay(stored).GetDate();
		const bool hasTime = date.GetValue() != day.GetValue();
		const bool hasDate = !day.IsEmpty();
		if (fraction == ibDateFractions::ibDateFractions_Date && hasTime)
			narrowed = true;
		if (fraction == ibDateFractions::ibDateFractions_Time && hasDate)
			narrowed = true;
		if (fraction == ibDateFractions::ibDateFractions_Date)
			converted = ibValue(day);
	}

	if (narrowed)
		return LostNarrowed(converted);

	fate.state = ibTypeChangeState::Kept;
	fate.value = converted;
	return fate;
}

bool ibTypeChangeReport::HasDataLoss() const
{
	for (const ibAttributeTypeChange& line : lines)
		if (line.lost > 0 || line.narrowed > 0)
			return true;
	return false;
}

wxString ibTypeChangeReport::Text() const
{
	std::int64_t kept = 0;
	std::int64_t lost = 0;
	std::int64_t narrowed = 0;
	wxString text;
	for (const ibAttributeTypeChange& line : lines) {
		kept += line.kept;
		lost += line.lost;
		narrowed += line.narrowed;
		if (!text.IsEmpty())
			text << wxT("\n");
		text << wxString::Format(_("%s / %s: %s converted, %s lost, %s narrowed"),
			line.object, line.attribute,
			GroupedCount(line.kept), GroupedCount(line.lost), GroupedCount(line.narrowed));
		for (const ibTypeChangeSample& sample : line.samples)
			text << wxString::Format(_("\n  %s: %s"), sample.ref, sample.oldValue);
	}
	if (!text.IsEmpty())
		text << wxT("\n");
	text << wxString::Format(_("Total: %s rows, %s converted, %s lost, %s narrowed"),
		GroupedCount(rowsRead), GroupedCount(kept), GroupedCount(lost), GroupedCount(narrowed));
	return text;
}

namespace {

void NoteFate(ibAttributeTypeChange& line, const wxString& ref, const ibValue& stored, const ibTypeChangeFate& fate)
{
	if (fate.state == ibTypeChangeState::Absent)
		return;
	if (fate.state == ibTypeChangeState::Kept)
		++line.kept;
	if (fate.wipe)
		++line.lost;
	if (fate.narrowed)
		++line.narrowed;
	if ((fate.wipe || fate.narrowed) && static_cast<int>(line.samples.size()) < kSampleCap)
		line.samples.push_back(ibTypeChangeSample{ ref, SampleText(stored) });
}

} // namespace

ibTypeChangeReport ibReadTypeChangeReport(const ibSchemaSnapshot* baseline,
                                          const ibSchemaSnapshot& target,
                                          const std::shared_ptr<ibDatabaseLayer>& layer,
                                          ibRestructureInfo* journal)
{
	ibTypeChangeReport report;
	if (baseline == nullptr || !layer)
		return report;

	std::vector<ChangedTable> tables;
	for (const ibSchemaTable& next : target.Tables()) {
		if (next.m_derived)
			continue;
		const ibSchemaTable* was = baseline->Find(next.m_id);
		if (was == nullptr || was->m_derived || was->m_name.IsEmpty() || !layer->TableExists(was->m_name))
			continue;
		ChangedTable table;
		table.was = was;
		table.next = &next;
		for (const ibSchemaColumn& column : next.m_columns) {
			if (column.m_column == nullptr)
				continue;
			const ibSchemaColumn* previous = nullptr;
			for (const ibSchemaColumn& candidate : was->m_columns)
				if (candidate.m_id == column.m_id)
					previous = &candidate;
			if (previous == nullptr || previous->m_column == nullptr)
				continue;
			if (previous->m_column->GetTypeValueDesc() == column.m_column->GetTypeValueDesc())
				continue;
			table.columns.push_back(ChangedColumn{ previous->m_column, column.m_column });
		}
		if (!table.columns.empty())
			tables.push_back(std::move(table));
	}
	if (tables.empty())
		return report;

	// After the lock, before the caller emits DDL. A code-only apply never
	// reaches here: there is no type change to read.
	ibRestructureInfo::RequireExclusiveForDDL();

	for (const ChangedTable& table : tables) {
		std::vector<wxString> fields;
		std::vector<const ibBackendQueryColumn*> key;
		if (table.was->m_queryable != nullptr)
			key = table.was->m_queryable->GetPrimaryKeyColumns();
		for (const ibBackendQueryColumn* column : key)
			if (column != nullptr)
				for (const ibColumnSlot& slot : DescribeColumnLayout(column))
					AddField(fields, slot.m_name);
		for (const ChangedColumn& changed : table.columns)
			for (const ibColumnSlot& slot : DescribeColumnLayout(changed.oldColumn))
				AddField(fields, slot.m_name);
		if (fields.empty())
			continue;

		wxString sql = wxT("SELECT ");
		for (size_t i = 0; i < fields.size(); ++i) {
			if (i != 0)
				sql << wxT(", ");
			sql << fields[i];
		}
		sql << wxT(" FROM ") << table.was->m_name;

		ibPreparedStatement* statement = layer->PrepareStatement(wxT("%s"), sql);
		if (statement == nullptr)
			ibBackendCoreException::Error(
				wxString::Format(_("The stored values of %s could not be read. The database structure was not changed."),
					table.was->m_name));
		ibDatabaseResultSet* rows = nullptr;
		try {
			rows = statement->RunQueryWithResults();
		}
		catch (const ibBackendException& err) {
			layer->CloseStatement(statement);
			ibBackendCoreException::Error(wxString::Format(
				_("The stored values of %s could not be read (%s). The database structure was not changed."),
				table.was->m_name, err.GetErrorDescription()));
		}
		ibQueryResult cursor(layer, statement, rows);

		const wxString object = ObjectName(*table.was);
		const ibMetaData* const oldMeta = MetaOf(*table.was);
		const ibMetaData* const newMeta = MetaOf(*table.next);
		std::vector<ibAttributeTypeChange> lines(table.columns.size());
		for (size_t i = 0; i < table.columns.size(); ++i) {
			lines[i].object = object;
			lines[i].attribute = AttributeName(table.columns[i].newColumn);
		}

		int row = 0;
		while (cursor.Next()) {
			++row;
			for (size_t i = 0; i < table.columns.size(); ++i) {
				if (TagIsAbsent(cursor, table.columns[i].oldColumn))
					continue;
				const wxString ref = RowLabel(cursor, key, row);
				ibValue stored;
				try {
					table.columns[i].oldColumn->ReadValue(
						table.columns[i].oldColumn->GetPhysicalName(), oldMeta, stored, cursor, false);
				}
				catch (const ibBackendException& err) {
					ibBackendCoreException::Error(wxString::Format(
						_("A stored value in %s, row %s, could not be read (%s). The database structure was not changed."),
						table.was->m_name, ref, err.GetErrorDescription()));
				}
				const ibTypeChangeFate fate = ibAssessTypeChange(
					stored, table.columns[i].newColumn->GetTypeValueDesc(), oldMeta, newMeta);
				NoteFate(lines[i], ref, stored, fate);
			}
		}

		report.rowsRead += row;
		for (ibAttributeTypeChange& line : lines)
			line.rows = row;
		if (journal != nullptr)
			journal->AppendInfo(wxString::Format(_("%s: %s rows read"), object, GroupedCount(row)));
		for (ibAttributeTypeChange& line : lines)
			report.lines.push_back(std::move(line));
	}
	return report;
}

bool ibTypeChangeBlocksApply(const ibTypeChangeReport& report, wxString& refusal)
{
	if (!report.HasDataLoss())
		return false;
	refusal = _("A type change would lose stored values. The database structure was not changed.");
	const wxString text = report.Text();
	if (!text.IsEmpty())
		refusal << wxT("\n") << text;
	return true;
}
