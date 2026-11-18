#include "databaseQueryInterpreter.h"
#include "databaseMemoryBase.h"

#include "backend/backend_exception.h"                      // a division by zero refused, a cancel heard
#include "backend/databaseLayer/databaseErrorCodes.h"
#include "backend/databaseLayer/databaseLayerException.h"

#include <algorithm>
#include <cstring>
#include <functional>    // std::hash — a row found by the hash of its cells (RowHash)
#include <numeric>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

// ==========================================================================
// ibMemoryCell, ibMemoryRow — the bytes
// ==========================================================================

namespace {

using ibCellKind = ibMemoryCell::Kind;

template<class T>
T Rd(const unsigned char* base, std::uint32_t at) noexcept
{
	T value;
	std::memcpy(&value, base + at, sizeof(T));
	return value;
}

template<class T>
void Wr(unsigned char* base, std::uint32_t at, T value) noexcept
{
	std::memcpy(base + at, &value, sizeof(T));
}

// What a value's cell is before it is written: its kind and the bytes after the kind's byte — a length first where the
// kind has one. A decimal's digits are its own block (ibNumber::GetBuffer); everything else is where the value lies.
struct Spelled
{
	ibCellKind     m_kind = ibCellKind::Null;
	unsigned char  m_fixed[8] = {};
	std::uint32_t  m_fixedLen = 0;
	const void*    m_data = nullptr;   // after the length: the units, the bytes, the digits
	std::uint32_t  m_dataLen = 0;
	ibMemoryBuffer m_digits;

	bool HasLength() const noexcept { return m_kind == ibCellKind::Number || m_kind == ibCellKind::String || m_kind == ibCellKind::Blob; }
	std::uint32_t Size() const noexcept { return 1 + m_fixedLen + (HasLength() ? 4 + m_dataLen : 0); }

	void WriteTo(unsigned char* at) const noexcept
	{
		at[0] = static_cast<unsigned char>(m_kind);
		std::memcpy(at + 1, m_fixed, m_fixedLen);
		if (HasLength()) {
			Wr(at, 1 + m_fixedLen, m_kind == ibCellKind::String ? m_dataLen / 2 : m_dataLen);
			if (m_dataLen != 0)
				std::memcpy(at + 1 + m_fixedLen + 4, m_data, m_dataLen);
		}
	}
};

Spelled Spell(bool value)
{
	Spelled s;
	s.m_kind = ibCellKind::Bool;
	s.m_fixed[0] = value ? 1 : 0;
	s.m_fixedLen = 1;
	return s;
}

Spelled Spell(const ibNumber& value)
{
	Spelled s;
	// ONE SPELLING A NUMBER: a whole one that fits is written whole — so two equal numbers are equal bytes.
	long long whole = 0;
	if (value.ToInt(whole) == 0 && ibNumber(whole) == value) {
		s.m_kind = ibCellKind::Integer;
		std::memcpy(s.m_fixed, &whole, 8);
		s.m_fixedLen = 8;
		return s;
	}
	s.m_kind = ibCellKind::Number;
	s.m_digits = value.GetBuffer();
	s.m_data = s.m_digits.GetData();
	s.m_dataLen = static_cast<std::uint32_t>(s.m_digits.GetDataLen());
	return s;
}

Spelled Spell(const ibDateTime& value)
{
	Spelled s;
	s.m_kind = ibCellKind::Date;
	const std::int64_t ticks = static_cast<std::int64_t>(value.GetValue());
	std::memcpy(s.m_fixed, &ticks, 8);
	s.m_fixedLen = 8;
	return s;
}

Spelled Spell(const ibString& value)
{
	Spelled s;
	s.m_kind = ibCellKind::String;
	s.m_data = value.c_str();
	s.m_dataLen = static_cast<std::uint32_t>(value.length() * sizeof(ibChar));
	return s;
}

Spelled Spell(const void* data, size_t len)
{
	Spelled s;
	if (len == 0)
		return s;
	s.m_kind = ibCellKind::Blob;
	s.m_data = data;
	s.m_dataLen = static_cast<std::uint32_t>(len);
	return s;
}

// A cell's own block — the cell at its start; none for a NULL.
ibMemoryBuffer Block(const Spelled& s)
{
	ibMemoryBuffer bytes;
	if (s.m_kind == ibCellKind::Null)
		return bytes;
	unsigned char* const at = static_cast<unsigned char*>(bytes.GetWriteBuf(s.Size()));
	s.WriteTo(at);
	bytes.UngetWriteBuf(s.Size());
	return bytes;
}

// The row's head: its count, then a place a field.
constexpr std::uint32_t kPlaces = 4;

} // namespace

ibMemoryCell ibMemoryCell::Of(bool value)                  { return ibMemoryCell(Block(Spell(value)), 0); }
ibMemoryCell ibMemoryCell::Of(const ibNumber& value)       { return ibMemoryCell(Block(Spell(value)), 0); }
ibMemoryCell ibMemoryCell::Of(const ibDateTime& value)     { return ibMemoryCell(Block(Spell(value)), 0); }
ibMemoryCell ibMemoryCell::Of(const ibString& value)       { return ibMemoryCell(Block(Spell(value)), 0); }
ibMemoryCell ibMemoryCell::Of(const void* data, size_t len) { return ibMemoryCell(Block(Spell(data, len)), 0); }

ibMemoryCell::Kind ibMemoryCell::GetKind() const noexcept
{
	return m_bytes.IsEmpty() ? Kind::Null : static_cast<Kind>(Base()[m_at]);
}

bool ibMemoryCell::AsBool() const noexcept
{
	return GetKind() == Kind::Bool && Base()[m_at + 1] != 0;
}

std::int64_t ibMemoryCell::AsInteger() const noexcept
{
	return GetKind() == Kind::Integer ? Rd<std::int64_t>(Base(), m_at + 1) : 0;
}

ibNumber ibMemoryCell::AsNumber() const
{
	switch (GetKind()) {
	case Kind::Integer:
		return ibNumber(static_cast<int64_t>(AsInteger()));
	case Kind::Number: {
		ibNumber number;
		number.SetBuffer(Base() + m_at + 5, Rd<std::uint32_t>(Base(), m_at + 1));
		return number;
	}
	default:
		return ibNumber();
	}
}

ibDateTime ibMemoryCell::AsDate() const noexcept
{
	return GetKind() == Kind::Date ? ibDateTime(Rd<std::int64_t>(Base(), m_at + 1)) : ibDateTime();
}

ibString ibMemoryCell::AsString() const
{
	if (GetKind() != Kind::String)
		return ibString();
	return ibString(reinterpret_cast<const ibChar*>(Base() + m_at + 5), Rd<std::uint32_t>(Base(), m_at + 1));
}

const void* ibMemoryCell::GetData() const noexcept
{
	const Kind kind = GetKind();
	return kind == Kind::String || kind == Kind::Blob ? Base() + m_at + 5 : nullptr;
}

size_t ibMemoryCell::GetDataLen() const noexcept
{
	switch (GetKind()) {
	case Kind::String: return Rd<std::uint32_t>(Base(), m_at + 1) * sizeof(ibChar);
	case Kind::Blob:   return Rd<std::uint32_t>(Base(), m_at + 1);
	default:           return 0;
	}
}

std::uint32_t ibMemoryCell::Weight() const noexcept
{
	switch (GetKind()) {
	case Kind::Null:   return 0;
	case Kind::Bool:   return 2;
	case Kind::Integer:
	case Kind::Date:   return 9;
	case Kind::String: return 5 + Rd<std::uint32_t>(Base(), m_at + 1) * static_cast<std::uint32_t>(sizeof(ibChar));
	case Kind::Number:
	case Kind::Blob:   return 5 + Rd<std::uint32_t>(Base(), m_at + 1);
	}
	return 0;
}

ibMemoryCell CellOf(const ibValue& v)
{
	switch (v.GetType()) {
	case TYPE_BOOLEAN: return ibMemoryCell::Of(v.GetBoolean());
	case TYPE_NUMBER:  return ibMemoryCell::Of(v.GetNumber());
	case TYPE_DATE:    return ibMemoryCell::Of(v.GetDate());
	case TYPE_STRING:  return ibMemoryCell::Of(v.GetString());
	default:           return ibMemoryCell();
	}
}

ibMemoryRow::ibMemoryRow(size_t count, size_t room)
{
	if (count == 0)
		return;
	const std::uint32_t head = kPlaces + 4 * static_cast<std::uint32_t>(count);
	m_bytes.SetBufSize(head + room);
	unsigned char* const at = static_cast<unsigned char*>(m_bytes.GetWriteBuf(head));
	std::memset(at, 0, head);
	Wr(at, 0, static_cast<std::uint32_t>(count));
	m_bytes.UngetWriteBuf(head);
}

ibMemoryRow::ibMemoryRow(const std::vector<ibMemoryCell>& cells)
{
	if (cells.empty())
		return;
	const std::uint32_t head = kPlaces + 4 * static_cast<std::uint32_t>(cells.size());
	std::uint32_t size = head;
	for (const ibMemoryCell& cell : cells)
		size += cell.Weight();
	unsigned char* const at = static_cast<unsigned char*>(m_bytes.GetWriteBuf(size));
	Wr(at, 0, static_cast<std::uint32_t>(cells.size()));
	std::uint32_t next = head;
	for (size_t i = 0; i < cells.size(); ++i) {
		const std::uint32_t weight = cells[i].Weight();
		Wr(at, kPlaces + 4 * static_cast<std::uint32_t>(i), weight == 0 ? 0u : next);
		if (weight != 0)
			std::memcpy(at + next, cells[i].Base() + cells[i].m_at, weight);
		next += weight;
	}
	m_bytes.UngetWriteBuf(size);
}

size_t ibMemoryRow::size() const noexcept
{
	return m_bytes.IsEmpty() ? 0 : Rd<std::uint32_t>(static_cast<const unsigned char*>(m_bytes.GetData()), 0);
}

ibMemoryCell ibMemoryRow::operator[](size_t i) const
{
	if (i >= size())
		return ibMemoryCell();
	const std::uint32_t at = Rd<std::uint32_t>(static_cast<const unsigned char*>(m_bytes.GetData()), kPlaces + 4 * static_cast<std::uint32_t>(i));
	return at == 0 ? ibMemoryCell() : ibMemoryCell(m_bytes, at);
}

namespace {

// A field's cell put after the cells a row has, and its place written — the row's block made its own first.
template<class Write>
void Put(ibMemoryBuffer& bytes, size_t i, std::uint32_t weight, const Write& write)
{
	if (bytes.IsEmpty() || i >= Rd<std::uint32_t>(static_cast<const unsigned char*>(bytes.GetData()), 0))
		return;
	const std::uint32_t place = kPlaces + 4 * static_cast<std::uint32_t>(i);
	if (weight == 0) {
		unsigned char* const head = static_cast<unsigned char*>(bytes.GetWriteBuf(bytes.GetDataLen()));
		bytes.UngetWriteBuf(bytes.GetDataLen());
		Wr(head, place, 0u);
		return;
	}
	const std::uint32_t at = static_cast<std::uint32_t>(bytes.GetDataLen());
	write(static_cast<unsigned char*>(bytes.GetAppendBuf(weight)));
	bytes.UngetAppendBuf(weight);
	Wr(static_cast<unsigned char*>(bytes.GetData()), place, at);   // the block is this buffer's own: the append made it so
}

} // namespace

void ibMemoryRow::Set(size_t i, const ibMemoryCell& cell)
{
	Put(m_bytes, i, cell.Weight(), [&cell](unsigned char* at) { std::memcpy(at, cell.Base() + cell.m_at, cell.Weight()); });
}

void ibMemoryRow::Set(size_t i, bool value)
{
	const Spelled s = Spell(value);
	Put(m_bytes, i, s.Size(), [&s](unsigned char* at) { s.WriteTo(at); });
}

void ibMemoryRow::Set(size_t i, const ibNumber& value)
{
	const Spelled s = Spell(value);
	Put(m_bytes, i, s.Size(), [&s](unsigned char* at) { s.WriteTo(at); });
}

void ibMemoryRow::Set(size_t i, const ibDateTime& value)
{
	const Spelled s = Spell(value);
	Put(m_bytes, i, s.Size(), [&s](unsigned char* at) { s.WriteTo(at); });
}

void ibMemoryRow::Set(size_t i, const ibString& value)
{
	const Spelled s = Spell(value);
	Put(m_bytes, i, s.Size(), [&s](unsigned char* at) { s.WriteTo(at); });
}

void ibMemoryRow::Set(size_t i, const void* data, size_t len)
{
	const Spelled s = Spell(data, len);
	Put(m_bytes, i, s.m_kind == ibCellKind::Null ? 0 : s.Size(), [&s](unsigned char* at) { s.WriteTo(at); });
}

ibMemoryRows::ibMemoryRows(std::vector<ibMemoryRow> rows)
	: m_rows(std::make_shared<std::vector<ibMemoryRow>>(std::move(rows)))
{
}

std::vector<ibMemoryRow>& ibMemoryRows::GetWriteRows()
{
	if (m_rows == nullptr)
		m_rows = std::make_shared<std::vector<ibMemoryRow>>();
	else if (m_rows.use_count() > 1)
		m_rows = std::make_shared<std::vector<ibMemoryRow>>(*m_rows);
	return *m_rows;
}

std::vector<ibMemoryRow> ibMemoryRows::TakeRows()
{
	const std::shared_ptr<std::vector<ibMemoryRow>> held = std::move(m_rows);   // let go of either way
	if (held == nullptr)
		return {};
	if (held.use_count() > 1)
		return *held;
	return std::move(*held);
}

const std::vector<ibMemoryRow>& ibMemoryRows::Rows() const
{
	static const std::vector<ibMemoryRow> s_none;
	return m_rows != nullptr ? *m_rows : s_none;
}

namespace {

using ibRow = ibMemoryRow;
using ibCells = std::vector<ibMemoryCell>;   // a key, a sort's values: cells gathered, never a row of a table
using ibTableMap = std::unordered_map<ibString, ibMemoryTable, ibFieldNameHash, ibFieldNameEqual>;

// A statement this base cannot run is refused as a database refuses one it cannot parse.
[[noreturn]] void Refuse(const ibString& text)
{
	ibDatabaseLayerException::Throw(ibBackendDatabaseException::Kind::Syntax, DATABASE_LAYER_SQL_SYNTAX_ERROR,
		ibEmptyString, text);
}

// ---------------------------------------------------------------------------------------------------------------
// Cells, their truth and their order
// ---------------------------------------------------------------------------------------------------------------

// SQL's three values. A WHERE, an ON and a HAVING keep a row on True alone.
enum class Tri { False, True, Unknown };

Tri TriNot(Tri a) { return a == Tri::Unknown ? Tri::Unknown : a == Tri::True ? Tri::False : Tri::True; }

Tri TriAnd(Tri a, Tri b)
{
	if (a == Tri::False || b == Tri::False) return Tri::False;
	return a == Tri::True && b == Tri::True ? Tri::True : Tri::Unknown;
}

Tri TriOr(Tri a, Tri b)
{
	if (a == Tri::True || b == Tri::True) return Tri::True;
	return a == Tri::False && b == Tri::False ? Tri::False : Tri::Unknown;
}

ibMemoryCell NullCell() { return ibMemoryCell(); }

// TRUE and FALSE made once: a condition's answer is one more owner of one of them, not a cell made a row.
ibMemoryCell BoolCell(bool b)
{
	static const ibMemoryCell s_true = ibMemoryCell::Of(true), s_false = ibMemoryCell::Of(false);
	return b ? s_true : s_false;
}

ibMemoryCell TriCell(Tri t) { return t == Tri::Unknown ? NullCell() : BoolCell(t == Tri::True); }

ibMemoryCell NumberCell(const ibNumber& n) { return ibMemoryCell::Of(n); }

bool IsNumeric(ibCellKind kind) { return kind == ibCellKind::Integer || kind == ibCellKind::Number; }

// What a cell is as a condition: a boolean as it is, a number by being other than zero (SQLite's reading), NULL unknown.
Tri TriOf(const ibMemoryCell& cell)
{
	const ibCellKind kind = cell.GetKind();
	if (kind == ibCellKind::Null)
		return Tri::Unknown;
	if (kind == ibCellKind::Bool)
		return cell.AsBool() ? Tri::True : Tri::False;
	if (kind == ibCellKind::Integer)
		return cell.AsInteger() == 0 ? Tri::False : Tri::True;
	if (IsNumeric(kind))
		return cell.AsNumber().IsZero() ? Tri::False : Tri::True;
	return Tri::False;
}

int CompareBytes(const void* a, size_t aLen, const void* b, size_t bLen)
{
	const size_t common = std::min(aLen, bLen);
	const int c = common > 0 ? std::memcmp(a, b, common) : 0;
	if (c != 0)
		return c < 0 ? -1 : 1;
	return aLen < bLen ? -1 : aLen > bLen ? 1 : 0;
}

// Two texts where they lie, unit by unit — ibString::Cmp's order, with no text made.
int CompareUnits(const ibMemoryCell& a, const ibMemoryCell& b)
{
	const ibChar* const x = static_cast<const ibChar*>(a.GetData());
	const ibChar* const y = static_cast<const ibChar*>(b.GetData());
	const size_t xn = a.GetDataLen() / sizeof(ibChar), yn = b.GetDataLen() / sizeof(ibChar);
	for (size_t i = 0, n = std::min(xn, yn); i < n; ++i)
		if (x[i] != y[i])
			return x[i] < y[i] ? -1 : 1;
	return xn < yn ? -1 : xn > yn ? 1 : 0;
}

// Which stretch of the order a kind occupies — the runtime's (ibVariant::CompareValueLS, KindRank), bytes last.
int RankOf(ibCellKind kind)
{
	switch (kind) {
	case ibCellKind::Null:   return 0;
	case ibCellKind::Bool:   return 1;
	case ibCellKind::Integer:
	case ibCellKind::Number: return 2;
	case ibCellKind::Date:   return 3;
	case ibCellKind::String: return 4;
	case ibCellKind::Blob:   return 5;
	}
	return 5;
}

// ⭐ THE ORDER OF CELLS — NULL first, then values in the memory road's one order (CompareValueLS's: a kind's stretch,
// then the value), then bytes. A sort, a group, a DISTINCT and a UNION ask it, so two rows are the same row to all of
// them. Read where the cells lie: two whole numbers, two dates and two texts are compared with nothing made.
int Order(const ibMemoryCell& a, const ibMemoryCell& b)
{
	const ibCellKind x = a.GetKind(), y = b.GetKind();
	const int rx = RankOf(x), ry = RankOf(y);
	if (rx != ry)
		return rx < ry ? -1 : 1;
	switch (x) {
	case ibCellKind::Null:
		return 0;
	case ibCellKind::Bool:
		return a.AsBool() == b.AsBool() ? 0 : a.AsBool() ? 1 : -1;
	case ibCellKind::Integer:
	case ibCellKind::Number: {
		// Two whole numbers as the int64 they are: a reference's type is a clsid, past ibNumber's immediate tier, and a
		// group keyed by references made two numbers in the heap at every comparison (half of query 9, 2026-11-18).
		if (x == ibCellKind::Integer && y == ibCellKind::Integer) {
			const std::int64_t p = a.AsInteger(), q = b.AsInteger();
			return p < q ? -1 : p > q ? 1 : 0;
		}
		const int c = a.AsNumber().Compare(b.AsNumber());
		return c < 0 ? -1 : c > 0 ? 1 : 0;
	}
	case ibCellKind::Date: {
		const auto p = a.AsDate().GetValue(), q = b.AsDate().GetValue();
		return p < q ? -1 : p > q ? 1 : 0;
	}
	case ibCellKind::String:
		return CompareUnits(a, b);
	case ibCellKind::Blob:
		return CompareBytes(a.GetData(), a.GetDataLen(), b.GetData(), b.GetDataLen());
	}
	return 0;
}

// Two rows of a relation, or two gathered keys, cell by cell.
template<class Cells>
int OrderRows(const Cells& a, const Cells& b)
{
	const size_t n = std::min(a.size(), b.size());
	for (size_t i = 0; i < n; ++i)
		if (const int c = Order(a[i], b[i]))
			return c;
	return a.size() < b.size() ? -1 : a.size() > b.size() ? 1 : 0;
}

// ⭐ TWO ROWS ARE THE SAME ROW WHEN `Order` FINDS NOTHING BETWEEN THEM — and they are found by a hash of it, not by a
// tree. A group, a join's filing, DISTINCT and UNION only ask "is it here": a tree asked it a comparison per level, each
// cell by cell, and Debug checked every one twice — a fifth of query 9 of the million rows (2026-11-19).
// The hash agrees with `Order`: a number is spelled one way (Spell — a whole one is always Integer), so a whole number
// hashes as its int64 and a fraction as its double, which two equal fractions share; a text by its units, bytes by bytes.
size_t HashOf(const ibMemoryCell& cell)
{
	const ibCellKind kind = cell.GetKind();
	switch (kind) {
	case ibCellKind::Null:   return 0;
	case ibCellKind::Bool:   return cell.AsBool() ? 1 : 2;
	case ibCellKind::Integer:  return std::hash<std::int64_t>{}(cell.AsInteger());
	case ibCellKind::Number: return std::hash<double>{}(cell.AsNumber().ToDouble());
	case ibCellKind::Date:   return std::hash<std::int64_t>{}(static_cast<std::int64_t>(cell.AsDate().GetValue()));
	case ibCellKind::String:
		return std::hash<std::u16string_view>{}(std::u16string_view(static_cast<const char16_t*>(cell.GetData()),
			cell.GetDataLen() / sizeof(ibChar)));
	case ibCellKind::Blob:
		return std::hash<std::string_view>{}(std::string_view(static_cast<const char*>(cell.GetData()), cell.GetDataLen()));
	}
	return 0;
}

struct RowHash {
	template<class Cells>
	size_t operator()(const Cells& cells) const
	{
		size_t h = cells.size();
		for (size_t i = 0; i < cells.size(); ++i)
			h ^= HashOf(cells[i]) + 0x9e3779b9u + (h << 6) + (h >> 2);
		return h;
	}
};

struct RowEqual {
	template<class Cells>
	bool operator()(const Cells& a, const Cells& b) const { return OrderRows(a, b) == 0; }
};

// A comparison as the database answers it: UNKNOWN with a NULL on either side; values compared as a condition over
// rows in memory compares them (MatchRamCondition) — two of different kinds are never equal — bytes byte by byte.
Tri CompareTri(ibQueryBinOp op, const ibMemoryCell& a, const ibMemoryCell& b)
{
	if (a.IsNull() || b.IsNull())
		return Tri::Unknown;
	const int c = Order(a, b);
	bool res = false;
	switch (op) {
	case ibQueryBinOp::Eq: res = c == 0; break;
	case ibQueryBinOp::Ne: res = c != 0; break;
	case ibQueryBinOp::Lt: res = c < 0;  break;
	case ibQueryBinOp::Le: res = c <= 0; break;
	case ibQueryBinOp::Gt: res = c > 0;  break;
	case ibQueryBinOp::Ge: res = c >= 0; break;
	default:               break;
	}
	return res ? Tri::True : Tri::False;
}

ibMemoryCell Compare(ibQueryBinOp op, const ibMemoryCell& a, const ibMemoryCell& b)
{
	return TriCell(CompareTri(op, a, b));
}

// LIKE with the case aside — '%' any run, '_' one character, no escape — as the renderer asks it (UPPER on both
// sides) and as a condition over rows in memory reads it (RamLike).
bool Like(const ibString& text, const ibString& pattern)
{
	const ibString s = text.Lower(), p = pattern.Lower();
	size_t si = 0, pi = 0, star = ibString::npos, mark = 0;
	while (si < s.length()) {
		if (pi < p.length() && p[pi] != ibT('%') && (p[pi] == ibT('_') || p[pi] == s[si])) {
			++si;
			++pi;
		}
		else if (pi < p.length() && p[pi] == ibT('%')) {
			star = pi++;
			mark = si;
		}
		else if (star != ibString::npos) {
			pi = star + 1;
			si = ++mark;
		}
		else
			return false;
	}
	while (pi < p.length() && p[pi] == ibT('%'))
		++pi;
	return pi == p.length();
}

// The text a date is held as where a base keeps it as text (SQLite's TIMESTAMP) — what a concatenation, a CAST and a
// string read of the result set give.
const ibChar* const kDateText = ibT("%Y-%m-%d %H:%M:%S");

// The text a cell gives a concatenation, a substring or a CAST to a string. A date as SQLite keeps one.
ibString TextOf(const ibMemoryCell& cell)
{
	switch (cell.GetKind()) {
	case ibCellKind::Date:   return cell.AsDate().Format(kDateText);
	case ibCellKind::Bool:   return cell.AsBool() ? ibT("1") : ibT("0");
	case ibCellKind::Integer:
	case ibCellKind::Number: return cell.AsNumber().ToString();
	case ibCellKind::String: return cell.AsString();
	default:                 return ibString();
	}
}

// The number a cell gives an arithmetic, a fold or a numeric read of the result set: a boolean as 1 or 0, a text by its
// digits, anything else 0.
ibNumber NumberOf(const ibMemoryCell& cell)
{
	switch (cell.GetKind()) {
	case ibCellKind::Integer:
	case ibCellKind::Number: return cell.AsNumber();
	case ibCellKind::Bool:   return ibNumber(cell.AsBool() ? 1 : 0);
	case ibCellKind::String: {
		ibNumber n;
		return n.FromString(cell.AsString()) ? n : ibNumber();
	}
	default:                 return ibNumber();
	}
}

// The date a cell gives the calendar: a date, or a text that reads as one; false for anything else.
bool DateOf(const ibMemoryCell& cell, ibDateTime& date)
{
	if (cell.GetKind() == ibCellKind::Date) {
		date = cell.AsDate();
		return true;
	}
	return cell.GetKind() == ibCellKind::String && date.FromString(cell.AsString());
}

ibMemoryCell CastTo(const ibMemoryCell& cell, const ibColumnType& type)
{
	if (cell.IsNull() || cell.IsBlob())
		return cell;   // NULL stays NULL; bytes stay the bytes they are, whatever they are called
	const ibCellKind kind = cell.GetKind();
	switch (type.m_kind) {
	case ibCanonicalKind::Boolean:
		if (kind == ibCellKind::String) {
			const ibString text = cell.AsString();
			return BoolCell(text == ibT("1") || text.IsSameAs(ibT("true"), false));
		}
		return BoolCell(!NumberOf(cell).IsZero());
	case ibCanonicalKind::Integer:
	case ibCanonicalKind::BigInt:
		return kind == ibCellKind::Date ? NullCell() : kind == ibCellKind::Integer ? cell : NumberCell(NumberOf(cell).Round());
	case ibCanonicalKind::Number:
		if (kind == ibCellKind::Date)
			return NullCell();
		if (kind == ibCellKind::Integer)
			return cell;   // a whole number is the number it is at any scale
		return NumberCell(type.m_scale >= 0 ? NumberOf(cell).Round(type.m_scale) : NumberOf(cell));
	case ibCanonicalKind::String: {
		if (kind == ibCellKind::String && (type.m_length <= 0 || cell.GetDataLen() / sizeof(ibChar) <= static_cast<size_t>(type.m_length)))
			return cell;
		const ibString text = TextOf(cell);
		return ibMemoryCell::Of(type.m_length > 0 && text.length() > static_cast<size_t>(type.m_length)
			? text.Left(static_cast<size_t>(type.m_length)) : text);
	}
	case ibCanonicalKind::Text:
		return kind == ibCellKind::String ? cell : ibMemoryCell::Of(TextOf(cell));
	case ibCanonicalKind::Date: {
		ibDateTime date;
		if (!DateOf(cell, date))
			return NullCell();
		return ibMemoryCell::Of(type.m_datePrec == ibDatePrec::Date ? date.BeginOfPeriod(ibTotalsPeriod::Day) : date);
	}
	case ibCanonicalKind::Blob:
	case ibCanonicalKind::Binary:
	case ibCanonicalKind::Guid:
		return cell;
	}
	return cell;
}

bool IsAggregateName(const ibString& name)
{
	return name.IsSameAs(ibT("SUM"), false) || name.IsSameAs(ibT("COUNT"), false) || name.IsSameAs(ibT("MIN"), false)
		|| name.IsSameAs(ibT("MAX"), false) || name.IsSameAs(ibT("AVG"), false);
}

bool IsAggregate(const ibQueryExpr& e)
{
	return e.m_kind == ibQueryExprKind::Func && !e.m_over && IsAggregateName(e.m_name);
}

// Does a select fold its rows — an aggregate outside a window, here or under it (SUM(SUM(x)) OVER folds the group
// first)? A subquery's aggregates are its own.
bool HasAggregate(const ibQueryExprPtr& e)
{
	if (!e)
		return false;
	if (IsAggregate(*e))
		return true;
	if (HasAggregate(e->m_lhs) || HasAggregate(e->m_rhs) || HasAggregate(e->m_else))
		return true;
	for (const ibQueryExprPtr& arg : e->m_args)
		if (HasAggregate(arg))
			return true;
	for (const auto& branch : e->m_cases)
		if (HasAggregate(branch.first) || HasAggregate(branch.second))
			return true;
	if (e->m_over) {
		for (const ibQueryExprPtr& key : e->m_over->m_partitionBy)
			if (HasAggregate(key))
				return true;
		for (const ibQuerySortKey& key : e->m_over->m_orderBy)
			if (HasAggregate(key.m_expr))
				return true;
	}
	return false;
}

// Every window call an expression holds, in the order met. A window's own operands hold none (SQL forbids it).
void CollectWindows(const ibQueryExprPtr& e, std::vector<ibQueryExprPtr>& out)
{
	if (!e)
		return;
	if (e->m_kind == ibQueryExprKind::Func && e->m_over) {
		if (std::find(out.begin(), out.end(), e) == out.end())
			out.push_back(e);
		return;
	}
	CollectWindows(e->m_lhs, out);
	CollectWindows(e->m_rhs, out);
	CollectWindows(e->m_else, out);
	for (const ibQueryExprPtr& arg : e->m_args)
		CollectWindows(arg, out);
	for (const auto& branch : e->m_cases) {
		CollectWindows(branch.first, out);
		CollectWindows(branch.second, out);
	}
}

// Two expressions that say the same thing — how a projection item is known for a group key it repeats. The builders
// hand the same node to both (the renderer's GROUP BY position rests on it); a rebuilt one is compared by its parts.
bool SameExpr(const ibQueryExpr* a, const ibQueryExpr* b);

bool SameExpr(const ibQueryExprPtr& a, const ibQueryExprPtr& b)
{
	return SameExpr(a.get(), b.get());
}

bool SameExpr(const ibQueryExpr* a, const ibQueryExpr* b)
{
	if (a == b)
		return true;
	if (a == nullptr || b == nullptr || a->m_kind != b->m_kind)
		return false;
	if (!ibFieldNameEqual()(a->m_name, b->m_name) || !ibFieldNameEqual()(a->m_qualifier, b->m_qualifier))
		return false;
	if (a->m_const.GetType() != b->m_const.GetType() || !a->m_const.CompareValueEQ(b->m_const)
		|| CompareBytes(a->m_blob.GetData(), a->m_blob.GetDataLen(), b->m_blob.GetData(), b->m_blob.GetDataLen()) != 0)
		return false;
	if (a->m_paramIndex != b->m_paramIndex || a->m_distinct != b->m_distinct || a->m_binOp != b->m_binOp
		|| a->m_negated != b->m_negated || a->m_periodUnit != b->m_periodUnit || a->m_datePart != b->m_datePart)
		return false;
	if (a->m_castType.m_kind != b->m_castType.m_kind || a->m_castType.m_length != b->m_castType.m_length
		|| a->m_castType.m_precision != b->m_castType.m_precision || a->m_castType.m_scale != b->m_castType.m_scale
		|| a->m_castType.m_datePrec != b->m_castType.m_datePrec)
		return false;
	if (a->m_subquery != b->m_subquery || a->m_over != b->m_over)
		return false;
	if (!SameExpr(a->m_lhs, b->m_lhs) || !SameExpr(a->m_rhs, b->m_rhs) || !SameExpr(a->m_else, b->m_else))
		return false;
	if (a->m_args.size() != b->m_args.size() || a->m_cases.size() != b->m_cases.size())
		return false;
	for (size_t i = 0; i < a->m_args.size(); ++i)
		if (!SameExpr(a->m_args[i], b->m_args[i]))
			return false;
	for (size_t i = 0; i < a->m_cases.size(); ++i)
		if (!SameExpr(a->m_cases[i].first, b->m_cases[i].first) || !SameExpr(a->m_cases[i].second, b->m_cases[i].second))
			return false;
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Relations, scopes, groups
// ---------------------------------------------------------------------------------------------------------------

// A relation in flight: its columns as a statement addresses them (qualifier and name) and its rows — its own, or a
// table's / a named query's, held where they lie.
struct Relation
{
	std::vector<ibString> m_qualifiers;
	std::vector<ibString> m_names;
	ibMemoryRows          m_rows;

	size_t Width() const { return m_names.size(); }
};

// A grouped select's keys, flattened: a composite key of a ROLLUP (a Func with no name, its elements as arguments) is
// its elements, each knowing which key it belongs to — a level keeps or rolls away a whole key.
struct Grouping
{
	std::vector<ibQueryExprPtr> m_flat;
	std::vector<size_t>         m_keyOf;    // per flat element, its key
	std::vector<int>            m_column;   // per flat element, the source column it names (-1: not a bare column)
};

struct Group
{
	std::vector<const ibRow*> m_rows;
	ibCells                   m_keys;       // per flat element; NULL where the level rolled its key away
	size_t                    m_level = 0;  // keys kept
};

using WindowValues = std::unordered_map<const ibQueryExpr*, std::vector<ibMemoryCell>>;

// What an expression is evaluated against: a row of a relation (or the two halves of one while a join's ON is asked),
// the row of the statement around it (a correlated subquery), a group, and — while the ORDER BY is read — the output
// row and its names.
struct Scope
{
	const Relation* m_rel   = nullptr;
	const ibRow*    m_row   = nullptr;
	const ibRow*    m_right = nullptr;
	size_t          m_split = 0;
	const Scope*    m_outer = nullptr;

	const Grouping* m_grouping = nullptr;
	const Group*    m_group    = nullptr;

	const std::vector<ibString>* m_outNames = nullptr;
	const ibRow*                 m_outRow   = nullptr;
	const WindowValues*          m_windows  = nullptr;
	size_t                       m_outIndex = 0;

	ibMemoryCell CellAt(size_t i) const
	{
		if (m_right != nullptr && i >= m_split)
			return (*m_right)[i - m_split];
		return m_row != nullptr ? (*m_row)[i] : ibMemoryCell();
	}
};

// A column of a relation by qualifier and name, without case; -1 when it has none, kAmbiguous when two carry it.
constexpr int kAmbiguous = -2;

int SeekColumn(const Relation& rel, const ibString& qualifier, const ibString& name)
{
	const ibFieldNameEqual same;
	int found = -1;
	for (size_t i = 0; i < rel.Width(); ++i) {
		if (!same(rel.m_names[i], name) || (!qualifier.IsEmpty() && !same(rel.m_qualifiers[i], qualifier)))
			continue;
		if (found >= 0)
			return kAmbiguous;
		found = static_cast<int>(i);
	}
	return found;
}

// …and as a statement reads it: a name two columns carry is refused, as the database refuses it.
int FindColumn(const Relation& rel, const ibString& qualifier, const ibString& name)
{
	const int found = SeekColumn(rel, qualifier, name);
	if (found == kAmbiguous)
		Refuse(ibString::Format(_("Column '%s' is ambiguous"), name));
	return found;
}

// ⭐ THE KEY OF A JOIN — the equalities of its ON that pair a field of one side with a field of the other, each conjunct
// of an AND: (the left side's column, the right side's). A field both sides carry, or neither (the statement around a
// correlated one), keys nothing; an ON with no such equality has no key and is met pair by pair.
void JoinKeys(const ibQueryExprPtr& on, const Relation& left, const Relation& right, std::vector<std::pair<size_t, size_t>>& keys)
{
	if (!on || on->m_kind != ibQueryExprKind::BinOp)
		return;
	if (on->m_binOp == ibQueryBinOp::And) {
		JoinKeys(on->m_lhs, left, right, keys);
		JoinKeys(on->m_rhs, left, right, keys);
		return;
	}
	if (on->m_binOp != ibQueryBinOp::Eq || !on->m_lhs || !on->m_rhs
		|| on->m_lhs->m_kind != ibQueryExprKind::Column || on->m_rhs->m_kind != ibQueryExprKind::Column)
		return;
	// The side a column node names alone, or -1.
	auto sideOf = [](const Relation& here, const Relation& there, const ibQueryExpr& e) {
		const int at = SeekColumn(here, e.m_qualifier, e.m_name);
		return at >= 0 && SeekColumn(there, e.m_qualifier, e.m_name) == -1 ? at : -1;
	};
	const int lhsLeft = sideOf(left, right, *on->m_lhs), rhsRight = sideOf(right, left, *on->m_rhs);
	if (lhsLeft >= 0 && rhsRight >= 0) {
		keys.emplace_back(static_cast<size_t>(lhsLeft), static_cast<size_t>(rhsRight));
		return;
	}
	const int rhsLeft = sideOf(left, right, *on->m_rhs), lhsRight = sideOf(right, left, *on->m_lhs);
	if (rhsLeft >= 0 && lhsRight >= 0)
		keys.emplace_back(static_cast<size_t>(rhsLeft), static_cast<size_t>(lhsRight));
}

// One select's parts, flattened from the relation chain exactly as ibQueryRenderer::RenderSelect flattens it — the
// first projection, sort, limit and aggregate from the top, every filter. A chain this reads differently from the
// renderer would be one query with two meanings.
struct SelectParts
{
	ibQueryRelPtr                m_source;
	std::vector<ibQueryExprPtr>  m_predicates;
	std::vector<ibQueryProjItem> m_projection;
	std::vector<ibQueryExprPtr>  m_groupKeys;
	ibQueryExprPtr               m_having;
	std::vector<ibQuerySortKey>  m_sortKeys;
	bool m_distinct = false, m_rollup = false, m_hasLimit = false;
	long m_limitCount = -1, m_limitOffset = 0;
};

SelectParts Flatten(const ibQueryRelPtr& root)
{
	SelectParts parts;
	ibQueryRelPtr node = root;
	while (node != nullptr) {
		switch (node->m_kind) {
		case ibQueryRelKind::Scan:
		case ibQueryRelKind::Join:
		case ibQueryRelKind::Subquery:
			parts.m_source = node;
			node = nullptr;
			break;
		case ibQueryRelKind::Filter:
			if (node->m_predicate) parts.m_predicates.push_back(node->m_predicate);
			node = node->m_input;
			break;
		case ibQueryRelKind::Aggregate:
			if (parts.m_projection.empty()) parts.m_projection = node->m_projection;
			if (parts.m_groupKeys.empty())  parts.m_groupKeys  = node->m_groupKeys;
			if (!parts.m_having)            parts.m_having     = node->m_having;
			parts.m_rollup = parts.m_rollup || node->m_rollup;
			node = node->m_input;
			break;
		case ibQueryRelKind::Project:
			if (parts.m_projection.empty()) parts.m_projection = node->m_projection;
			node = node->m_input;
			break;
		case ibQueryRelKind::Sort:
			if (parts.m_sortKeys.empty()) parts.m_sortKeys = node->m_sortKeys;
			node = node->m_input;
			break;
		case ibQueryRelKind::Limit:
			if (!parts.m_hasLimit) {
				parts.m_hasLimit    = true;
				parts.m_limitCount  = node->m_limitCount;
				parts.m_limitOffset = node->m_limitOffset;
			}
			node = node->m_input;
			break;
		case ibQueryRelKind::Distinct:
			parts.m_distinct = true;
			node = node->m_input;
			break;
		default:
			node = node->m_input;
			break;
		}
	}
	return parts;
}

// One output column of a select: an expression, or — for a `*` — a source column taken as it is.
struct OutputColumn
{
	ibQueryExprPtr m_expr;
	int            m_column = -1;
	ibString       m_name;
};

// A fold of SUM / COUNT / MIN / MAX / AVG, fed one value at a time — the group's and the window frame's alike.
struct Fold
{
	ibNumber     m_sum;
	long long    m_values = 0;   // non-NULL values
	long long    m_rows   = 0;   // every row (COUNT(*))
	ibMemoryCell m_min, m_max;

	void Add(const ibMemoryCell& v)
	{
		++m_rows;
		if (v.IsNull())
			return;
		if (m_values == 0 || Order(v, m_min) < 0) m_min = v;
		if (m_values == 0 || Order(v, m_max) > 0) m_max = v;
		m_sum = m_sum + NumberOf(v);
		++m_values;
	}

	ibMemoryCell Result(const ibString& name, bool star) const
	{
		if (name.IsSameAs(ibT("COUNT"), false))
			return NumberCell(ibNumber(star ? m_rows : m_values));
		if (m_values == 0)
			return NullCell();
		if (name.IsSameAs(ibT("SUM"), false)) return NumberCell(m_sum);
		if (name.IsSameAs(ibT("MIN"), false)) return m_min;
		if (name.IsSameAs(ibT("MAX"), false)) return m_max;
		return NumberCell(m_sum / ibNumber(m_values));   // AVG
	}
};

// ---------------------------------------------------------------------------------------------------------------
// One statement
// ---------------------------------------------------------------------------------------------------------------

class Statement
{
public:
	// The parameters as cells, once — the border a value of the IR crosses.
	Statement(ibTableMap& tables, const std::vector<ibValue>& params, const std::atomic<bool>& cancelled)
		: m_tables(tables), m_cancelled(cancelled)
	{
		m_params.reserve(params.size());
		for (const ibValue& p : params)
			m_params.push_back(CellOf(p));
	}

	// A named query, declared once by its name: an ordinary one run, a recursive one walked to its end.
	void Declare(const ibQueryCte& cte);

	Relation Select(const ibQueryRelPtr& root, const Scope* outer);
	ibMemoryCell Eval(const ibQueryExprPtr& e, const Scope& s);
	Tri Test(const ibQueryExprPtr& e, const Scope& s) { return TriOf(Eval(e, s)); }

	ibMemoryTable& Table(const ibString& name);

private:
	Relation Source(const ibQueryRelPtr& rel, const Scope* outer);
	Relation Join(const ibQueryRelPtr& rel, const Scope* outer);
	ibMemoryCell Column(const ibQueryExpr& e, const Scope& s);
	ibMemoryCell Aggregate(const ibQueryExpr& e, const Scope& s);
	int KeyOf(const ibQueryExpr& e, const Scope& s) const;
	ibMemoryCell Call(const ibQueryExpr& e, const Scope& s);
	ibMemoryCell In(const ibQueryExpr& e, const Scope& s);
	const std::vector<ibMemoryCell>& SetOf(const ibQueryRelPtr& rel, const Scope& s, std::vector<ibMemoryCell>& correlated);
	void Windows(const std::vector<ibQueryExprPtr>& windows, const std::vector<Scope>& contexts, WindowValues& out);
	void Tick() const
	{
		if (m_cancelled.load(std::memory_order_relaxed))
			ibBackendInterruptException::Error();
	}

	ibTableMap&                       m_tables;
	std::vector<ibMemoryCell>         m_params;
	const std::atomic<bool>&          m_cancelled;
	// A constant of the IR as a cell, made the first time it is read — not once a row.
	std::unordered_map<const ibQueryExpr*, ibMemoryCell> m_consts;
	std::unordered_map<ibString, Relation, ibFieldNameHash, ibFieldNameEqual> m_named;
	// The rows of an `x IN (SELECT …)` — read once; a subquery that reads the row around it is read per row instead.
	std::unordered_map<const ibQueryRel*, std::vector<ibMemoryCell>> m_sets;
	std::unordered_set<const ibQueryRel*>                            m_correlated;
	// Where a column was found last: a column node is read against one relation row after row, so its name is
	// compared once and its position kept (checked against the name each time, a relation at the same address being
	// possibly another one).
	std::unordered_map<const ibQueryExpr*, std::pair<const Relation*, int>> m_found;
};

ibMemoryTable& Statement::Table(const ibString& name)
{
	const ibTableMap::iterator found = m_tables.find(name);
	if (found == m_tables.end())
		Refuse(ibString::Format(_("Table unknown: %s"), name));
	return found->second;
}

void Statement::Declare(const ibQueryCte& cte)
{
	if (cte.m_name.IsEmpty() || !cte.m_query || m_named.find(cte.m_name) != m_named.end())
		return;
	if (!cte.m_recursive) {
		m_named.emplace(cte.m_name, Select(cte.m_query, nullptr));
		return;
	}
	// A RECURSIVE ONE IS A UNION OF A START AND A STEP: the step reads, under the query's own name, the rows the round
	// before gave, until a round gives none. UNION drops a row already given, which is what ends a chain that loops.
	const ibQueryRelPtr& walk = cte.m_query;
	if (walk->m_kind != ibQueryRelKind::Union && walk->m_kind != ibQueryRelKind::UnionAll)
		Refuse(ibString::Format(_("A recursive named query '%s' is not a union of a start and a step"), cte.m_name));
	const bool dropsRepeats = walk->m_kind == ibQueryRelKind::Union;
	Relation all = Select(walk->m_input, nullptr);
	std::unordered_set<ibRow, RowHash, RowEqual> seen;
	if (dropsRepeats) {
		std::vector<ibRow> unique;
		for (ibRow& row : all.m_rows.TakeRows())
			if (seen.insert(row).second)
				unique.push_back(std::move(row));
		all.m_rows = ibMemoryRows(std::move(unique));
	}
	Relation round;
	round.m_names = all.m_names;
	round.m_qualifiers = all.m_qualifiers;
	round.m_rows = all.m_rows;
	static constexpr int kRounds = 1024;   // Firebird's own recursion limit, where it stops such a walk with an error
	for (int n = 0; !round.m_rows.empty(); ++n) {
		if (n == kRounds)
			Refuse(ibString::Format(_("A recursive named query '%s' did not end in %d rounds"), cte.m_name, kRounds));
		m_named.erase(cte.m_name);
		m_named.emplace(cte.m_name, std::move(round));
		Relation step = Select(walk->m_right, nullptr);
		round = Relation();
		round.m_names = all.m_names;
		round.m_qualifiers = all.m_qualifiers;
		std::vector<ibRow>& given = all.m_rows.GetWriteRows();
		std::vector<ibRow> next;
		for (ibRow& row : step.m_rows.TakeRows()) {
			if (dropsRepeats && !seen.insert(row).second)
				continue;
			given.push_back(row);
			next.push_back(std::move(row));
		}
		round.m_rows = ibMemoryRows(std::move(next));
	}
	m_named.erase(cte.m_name);
	m_named.emplace(cte.m_name, std::move(all));
}

Relation Statement::Source(const ibQueryRelPtr& rel, const Scope* outer)
{
	if (!rel) {
		// A select with no source reads one row of no columns (FROM RDB$DATABASE, or no FROM at all).
		Relation one;
		one.m_rows.GetWriteRows().emplace_back();
		return one;
	}
	switch (rel->m_kind) {
	case ibQueryRelKind::Scan: {
		Relation r;
		const ibString& reference = rel->m_alias.IsEmpty() ? rel->m_table : rel->m_alias;
		const auto named = m_named.find(rel->m_table);
		if (named != m_named.end()) {
			r.m_names = named->second.m_names;
			r.m_rows = named->second.m_rows;
		}
		else {
			const ibMemoryTable& table = Table(rel->m_table);
			r.m_names = table.m_fields;
			r.m_rows = table.m_rows;
		}
		r.m_qualifiers.assign(r.m_names.size(), reference);
		return r;
	}
	case ibQueryRelKind::Subquery: {
		Relation r = Select(rel->m_input, outer);
		r.m_qualifiers.assign(r.m_names.size(), rel->m_alias);
		return r;
	}
	case ibQueryRelKind::Join:
		return Join(rel, outer);
	default:
		return Source(rel->m_input, outer);   // a wrapping node as a FROM source — its input, as RenderSource reads it
	}
}

Relation Statement::Join(const ibQueryRelPtr& rel, const Scope* outer)
{
	const Relation left  = Source(rel->m_input, outer);
	const Relation right = Source(rel->m_right, outer);

	Relation out;
	out.m_names = left.m_names;
	out.m_names.insert(out.m_names.end(), right.m_names.begin(), right.m_names.end());
	out.m_qualifiers = left.m_qualifiers;
	out.m_qualifiers.insert(out.m_qualifiers.end(), right.m_qualifiers.begin(), right.m_qualifiers.end());

	const bool keepLeft  = rel->m_joinType == ibQueryJoinType::Left  || rel->m_joinType == ibQueryJoinType::Full;
	const bool keepRight = rel->m_joinType == ibQueryJoinType::Right || rel->m_joinType == ibQueryJoinType::Full;
	const ibRow leftNulls(left.Width()), rightNulls(right.Width());
	ibCells gathered;
	auto joined = [&gathered, &left, &right](const ibRow& l, const ibRow& r) {
		gathered.clear();
		for (size_t i = 0; i < left.Width(); ++i)
			gathered.push_back(l[i]);
		for (size_t i = 0; i < right.Width(); ++i)
			gathered.push_back(r[i]);
		return ibRow(gathered);
	};

	Scope on;
	on.m_rel   = &out;
	on.m_split = left.Width();
	on.m_outer = outer;
	const ibMemoryRows& rightRows = right.m_rows;
	std::vector<char> rightMatched(rightRows.size(), 0);
	std::vector<ibRow> rows;

	// ⭐ A JOIN WITH A KEY FILES THE RIGHT SIDE BY IT ONCE, and a left row meets only the rows its key names — not every
	// row of the other side. The whole ON is still asked of each pair met, so the rest of it answers as it does pair by
	// pair; the filing is by `Order`'s equality (RowEqual), which never tells apart two cells `=` calls equal, and a key
	// holding a NULL files nowhere, a NULL being equal to nothing. The rows meet in the order the pair-by-pair walk meets them.
	std::vector<std::pair<size_t, size_t>> keys;
	JoinKeys(rel->m_joinPredicate, left, right, keys);
	auto keyOf = [&keys](const ibRow& row, bool leftSide, ibCells& key) {
		key.clear();
		for (const std::pair<size_t, size_t>& k : keys) {
			ibMemoryCell cell = row[leftSide ? k.first : k.second];
			if (cell.IsNull())
				return false;
			key.push_back(std::move(cell));
		}
		return true;
	};
	std::unordered_map<ibCells, std::vector<size_t>, RowHash, RowEqual> filed;
	ibCells key;
	if (!keys.empty())
		for (size_t j = 0; j < rightRows.size(); ++j)
			if (keyOf(rightRows[j], false, key))
				filed[key].push_back(j);
	std::vector<size_t> every;
	if (keys.empty()) {
		every.resize(rightRows.size());
		std::iota(every.begin(), every.end(), size_t{ 0 });
	}
	static const std::vector<size_t> s_none;

	for (const ibRow& l : left.m_rows) {
		Tick();
		const std::vector<size_t>* met = &every;
		if (!keys.empty()) {
			const auto found = keyOf(l, true, key) ? filed.find(key) : filed.end();
			met = found != filed.end() ? &found->second : &s_none;
		}
		bool matched = false;
		for (const size_t j : *met) {
			on.m_row   = &l;
			on.m_right = &rightRows[j];
			if (rel->m_joinPredicate && Test(rel->m_joinPredicate, on) != Tri::True)
				continue;
			matched = true;
			rightMatched[j] = 1;
			rows.push_back(joined(l, rightRows[j]));
		}
		if (!matched && keepLeft)
			rows.push_back(joined(l, rightNulls));
	}
	if (keepRight)
		for (size_t j = 0; j < rightRows.size(); ++j)
			if (!rightMatched[j])
				rows.push_back(joined(leftNulls, rightRows[j]));
	out.m_rows = ibMemoryRows(std::move(rows));
	return out;
}

Relation Statement::Select(const ibQueryRelPtr& root, const Scope* outer)
{
	if (!root)
		Refuse(_("A statement with no query"));

	// A SET OPERATION is a combinator over two selects; the names are the first one's.
	if (root->m_kind == ibQueryRelKind::Union || root->m_kind == ibQueryRelKind::UnionAll) {
		Relation left  = Select(root->m_input, outer);
		Relation right = Select(root->m_right, outer);
		if (left.Width() != right.Width())
			Refuse(_("The members of a union do not have the same number of columns"));
		Relation out;
		out.m_names = left.m_names;
		out.m_qualifiers.assign(out.m_names.size(), ibString());
		std::vector<ibRow> rows = left.m_rows.TakeRows();
		for (ibRow& row : right.m_rows.TakeRows())
			rows.push_back(std::move(row));
		if (root->m_kind == ibQueryRelKind::Union) {
			std::unordered_set<ibRow, RowHash, RowEqual> seen;
			std::vector<ibRow> unique;
			for (ibRow& row : rows)
				if (seen.insert(row).second)
					unique.push_back(std::move(row));
			rows = std::move(unique);
		}
		out.m_rows = ibMemoryRows(std::move(rows));
		return out;
	}

	const SelectParts parts = Flatten(root);

	// --- FROM, WHERE ------------------------------------------------------------------------------------------
	Relation src = Source(parts.m_source, outer);   // not const: a select that hands it on gives its rows away
	std::vector<const ibRow*> live;
	{
		Scope where;
		where.m_rel   = &src;
		where.m_outer = outer;
		for (const ibRow& row : src.m_rows) {
			Tick();
			where.m_row = &row;
			bool keep = true;
			for (const ibQueryExprPtr& p : parts.m_predicates)
				if (Test(p, where) != Tri::True) {
					keep = false;
					break;
				}
			if (keep)
				live.push_back(&row);
		}
	}

	// --- the output columns (a `*` is every source column, or a qualifier's) ----------------------------------
	std::vector<OutputColumn> columns;
	if (parts.m_projection.empty()) {
		for (size_t i = 0; i < src.Width(); ++i)
			columns.push_back(OutputColumn{ nullptr, static_cast<int>(i), src.m_names[i] });
	}
	else {
		const ibFieldNameEqual same;
		for (const ibQueryProjItem& item : parts.m_projection) {
			const ibQueryExprPtr& e = item.m_expr;
			if (e && e->m_kind == ibQueryExprKind::Column && e->m_name == ibT("*")) {
				for (size_t i = 0; i < src.Width(); ++i)
					if (e->m_qualifier.IsEmpty() || same(src.m_qualifiers[i], e->m_qualifier))
						columns.push_back(OutputColumn{ nullptr, static_cast<int>(i), src.m_names[i] });
				continue;
			}
			ibString name = item.m_alias;
			if (name.IsEmpty() && e && e->m_kind == ibQueryExprKind::Column)
				name = e->m_name;
			columns.push_back(OutputColumn{ e, -1, name });
		}
	}

	// --- GROUP BY, HAVING: the contexts the output rows are evaluated in --------------------------------------
	bool grouped = !parts.m_groupKeys.empty() || parts.m_having != nullptr;   // a HAVING folds, as in SQL
	for (const OutputColumn& c : columns)
		grouped = grouped || HasAggregate(c.m_expr);

	// A bare column of the source, in a select that folds nothing, is the source's cell at one place — found once here,
	// as a `*` is, rather than by its name in every row: a copy read whole, 132 402 rows of 36 fields, paid two name
	// comparisons a cell for it (2026-11-18). One the source has not is the row around's, and is asked for as before.
	if (!grouped)
		for (OutputColumn& c : columns)
			if (c.m_column < 0 && c.m_expr && c.m_expr->m_kind == ibQueryExprKind::Column && !c.m_expr->m_with)
				c.m_column = FindColumn(src, c.m_expr->m_qualifier, c.m_expr->m_name);

	// ⭐ A SELECT THAT ONLY HANDS ITS SOURCE ON — every column in its place, nothing kept back, folded, ordered or cut — IS
	// its source under its own names: the rows go as they are, not made again cell by cell — a table's held, not copied
	// (ibMemoryRows). `SELECT * FROM (…) AS x`, the statement a composite is read by, made a copy's 132 402 rows of 36
	// cells twice over for nothing (2026-11-18).
	bool handsOn = parts.m_predicates.empty() && !grouped && parts.m_sortKeys.empty() && !parts.m_distinct
		&& !parts.m_hasLimit && columns.size() == src.Width();
	for (size_t i = 0; handsOn && i < columns.size(); ++i)
		handsOn = columns[i].m_column == static_cast<int>(i);
	if (handsOn) {
		Relation out;
		out.m_names.reserve(columns.size());
		for (const OutputColumn& c : columns)
			out.m_names.push_back(c.m_name);
		out.m_qualifiers.assign(out.m_names.size(), ibString());
		out.m_rows = std::move(src.m_rows);
		return out;
	}

	Grouping grouping;
	std::vector<Group> groups;
	std::vector<Scope> contexts;
	if (!grouped) {
		contexts.reserve(live.size());
		for (const ibRow* row : live) {
			Scope c;
			c.m_rel   = &src;
			c.m_row   = row;
			c.m_outer = outer;
			contexts.push_back(c);
		}
	}
	else {
		for (size_t k = 0; k < parts.m_groupKeys.size(); ++k) {
			const ibQueryExprPtr& key = parts.m_groupKeys[k];
			std::vector<ibQueryExprPtr> elements;
			if (parts.m_rollup && key && key->m_kind == ibQueryExprKind::Func && key->m_name.IsEmpty())
				elements = key->m_args;
			else
				elements.push_back(key);
			for (const ibQueryExprPtr& element : elements) {
				grouping.m_flat.push_back(element);
				grouping.m_keyOf.push_back(k);
				grouping.m_column.push_back(element && element->m_kind == ibQueryExprKind::Column
					? FindColumn(src, element->m_qualifier, element->m_name) : -1);
			}
		}
		// Each live row's key, once.
		std::vector<ibCells> keys(live.size());
		{
			Scope k;
			k.m_rel   = &src;
			k.m_outer = outer;
			for (size_t r = 0; r < live.size(); ++r) {
				k.m_row = live[r];
				keys[r].reserve(grouping.m_flat.size());
				for (size_t f = 0; f < grouping.m_flat.size(); ++f)   // a bare column at the place found above, as an output's
					keys[r].push_back(grouping.m_column[f] >= 0 ? k.CellAt(static_cast<size_t>(grouping.m_column[f]))
					                                            : Eval(grouping.m_flat[f], k));
			}
		}
		// A level keeps its first `level` keys: every key for GROUP BY, each from all of them down to none for ROLLUP.
		// The level of no keys is one group even over no rows — the total of nothing, as a select with no GROUP BY.
		const size_t keyCount = parts.m_groupKeys.size();
		const size_t lowest = parts.m_rollup ? 0 : keyCount;
		for (size_t level = keyCount + 1; level-- > lowest;) {
			std::unordered_map<ibCells, size_t, RowHash, RowEqual> index;
			const size_t first = groups.size();
			for (size_t r = 0; r < live.size(); ++r) {
				ibCells key = keys[r];
				for (size_t f = 0; f < key.size(); ++f)
					if (grouping.m_keyOf[f] >= level)
						key[f] = NullCell();
				const auto at = index.emplace(key, groups.size());
				if (at.second) {
					Group g;
					g.m_keys  = std::move(key);
					g.m_level = level;
					groups.push_back(std::move(g));
				}
				groups[at.first->second].m_rows.push_back(live[r]);
			}
			if (level == 0 && groups.size() == first) {
				Group g;
				g.m_keys.assign(grouping.m_flat.size(), NullCell());
				g.m_level = 0;
				groups.push_back(std::move(g));
			}
		}
		contexts.reserve(groups.size());
		for (const Group& g : groups) {
			Scope c;
			c.m_rel      = &src;
			c.m_row      = g.m_rows.empty() ? nullptr : g.m_rows.front();
			c.m_outer    = outer;
			c.m_grouping = &grouping;
			c.m_group    = &g;
			if (!parts.m_having || Test(parts.m_having, c) == Tri::True)
				contexts.push_back(c);
		}
	}

	// --- the windows, then the projection -----------------------------------------------------------------------
	std::vector<ibQueryExprPtr> windows;
	for (const OutputColumn& c : columns)
		CollectWindows(c.m_expr, windows);
	for (const ibQuerySortKey& key : parts.m_sortKeys)
		CollectWindows(key.m_expr, windows);
	WindowValues windowValues;
	Windows(windows, contexts, windowValues);

	Relation out;
	out.m_names.reserve(columns.size());
	for (const OutputColumn& c : columns)
		out.m_names.push_back(c.m_name);
	out.m_qualifiers.assign(out.m_names.size(), ibString());
	std::vector<ibRow> made;
	made.reserve(contexts.size());
	ibCells gathered;   // a row's cells before they are one block — the vector kept, its room with it
	gathered.reserve(columns.size());
	for (size_t i = 0; i < contexts.size(); ++i) {
		Scope c = contexts[i];
		c.m_windows  = &windowValues;
		c.m_outIndex = i;
		gathered.clear();
		for (const OutputColumn& column : columns)
			gathered.push_back(column.m_column >= 0 ? c.CellAt(static_cast<size_t>(column.m_column)) : Eval(column.m_expr, c));
		made.emplace_back(gathered);
	}

	// --- ORDER BY keys (an output name first, then the source), DISTINCT, the sort, the page -----------------
	std::vector<ibCells> sortKeys(parts.m_sortKeys.empty() ? 0 : made.size());   // none made where nothing is ordered
	if (!parts.m_sortKeys.empty())
		for (size_t i = 0; i < made.size(); ++i) {
			Scope c = contexts[i];
			c.m_windows  = &windowValues;
			c.m_outIndex = i;
			c.m_outNames = &out.m_names;
			c.m_outRow   = &made[i];
			for (const ibQuerySortKey& key : parts.m_sortKeys)
				sortKeys[i].push_back(Eval(key.m_expr, c));
		}

	std::vector<size_t> order(made.size());
	std::iota(order.begin(), order.end(), size_t(0));
	if (parts.m_distinct) {
		std::unordered_set<ibRow, RowHash, RowEqual> seen;
		std::vector<size_t> unique;
		for (size_t i : order)
			if (seen.insert(made[i]).second)
				unique.push_back(i);
		order = std::move(unique);
	}
	if (!parts.m_sortKeys.empty())
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			for (size_t k = 0; k < parts.m_sortKeys.size(); ++k) {
				const int c = Order(sortKeys[a][k], sortKeys[b][k]);
				if (c != 0)
					return parts.m_sortKeys[k].m_dir == ibQuerySortDir::Desc ? c > 0 : c < 0;
			}
			return false;
		});

	size_t begin = 0, end = order.size();
	if (parts.m_hasLimit) {
		begin = std::min(order.size(), static_cast<size_t>(std::max(0L, parts.m_limitOffset)));
		if (parts.m_limitCount >= 0)
			end = std::min(order.size(), begin + static_cast<size_t>(parts.m_limitCount));
	}
	std::vector<ibRow> rows;
	rows.reserve(end > begin ? end - begin : 0);
	for (size_t i = begin; i < end; ++i)
		rows.push_back(std::move(made[order[i]]));
	out.m_rows = ibMemoryRows(std::move(rows));
	return out;
}

// ⭐ EVERY WINDOW OVER THE OUTPUT ROWS, before any of them is projected: the rows of a partition in their order, and
// the frame each one folds — the whole partition with no order, the peers included (RANGE, and what SQL takes with no
// frame), through the current row, or before it.
void Statement::Windows(const std::vector<ibQueryExprPtr>& windows, const std::vector<Scope>& contexts, WindowValues& out)
{
	const size_t n = contexts.size();
	for (const ibQueryExprPtr& w : windows) {
		const ibQueryWindow& window = *w->m_over;
		std::vector<ibCells> partition(n), ordering(n);
		for (size_t i = 0; i < n; ++i) {
			for (const ibQueryExprPtr& key : window.m_partitionBy)
				partition[i].push_back(Eval(key, contexts[i]));
			for (const ibQuerySortKey& key : window.m_orderBy)
				ordering[i].push_back(Eval(key.m_expr, contexts[i]));
		}
		std::vector<size_t> order(n);
		std::iota(order.begin(), order.end(), size_t(0));
		std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
			if (const int c = OrderRows(partition[a], partition[b]))
				return c < 0;
			for (size_t k = 0; k < window.m_orderBy.size(); ++k) {
				const int c = Order(ordering[a][k], ordering[b][k]);
				if (c != 0)
					return window.m_orderBy[k].m_dir == ibQuerySortDir::Desc ? c > 0 : c < 0;
			}
			return false;
		});

		std::vector<ibMemoryCell>& values = out[w.get()];
		values.assign(n, NullCell());
		const bool ranking = w->m_name.IsSameAs(ibT("ROW_NUMBER"), false) || w->m_name.IsSameAs(ibT("RANK"), false)
			|| w->m_name.IsSameAs(ibT("DENSE_RANK"), false);
		if (!ranking && !IsAggregateName(w->m_name))
			Refuse(ibString::Format(_("A base in memory has no window function %s"), w->m_name));
		const bool star = w->m_args.empty();

		for (size_t b = 0; b < n;) {
			size_t e = b + 1;
			while (e < n && OrderRows(partition[order[e]], partition[order[b]]) == 0)
				++e;
			// The end of each row's peers — the rows its order cannot tell from it.
			std::vector<size_t> peersEnd(e - b);
			for (size_t p = e; p-- > b;)
				peersEnd[p - b] = p + 1 < e && OrderRows(ordering[order[p]], ordering[order[p + 1]]) == 0
					? peersEnd[p + 1 - b] : p + 1;
			if (ranking) {
				long long dense = 0;
				size_t peersBegin = b;
				for (size_t p = b; p < e; ++p) {
					if (p == b || OrderRows(ordering[order[p]], ordering[order[p - 1]]) != 0) {
						peersBegin = p;
						++dense;
					}
					const long long number = w->m_name.IsSameAs(ibT("ROW_NUMBER"), false) ? static_cast<long long>(p - b + 1)
						: w->m_name.IsSameAs(ibT("RANK"), false) ? static_cast<long long>(peersBegin - b + 1) : dense;
					values[order[p]] = NumberCell(ibNumber(number));
				}
			}
			else {
				// The fold of the partition's first k rows, for every k: each frame here is such a prefix.
				std::vector<ibMemoryCell> prefix(e - b + 1);
				Fold fold;
				std::unordered_set<ibCells, RowHash, RowEqual> distinct;
				prefix[0] = fold.Result(w->m_name, star);
				for (size_t p = b; p < e; ++p) {
					const ibMemoryCell v = star ? NumberCell(ibNumber(1)) : Eval(w->m_args.front(), contexts[order[p]]);
					if (!w->m_distinct || v.IsNull() || distinct.insert(ibCells{ v }).second)
						fold.Add(v);
					prefix[p - b + 1] = fold.Result(w->m_name, star);
				}
				for (size_t p = b; p < e; ++p) {
					size_t end = e;
					if (!window.m_orderBy.empty() || window.m_frame != ibQueryFrame::NoFrame) {
						switch (window.m_frame) {
						case ibQueryFrame::RowsThroughCurrent: end = p + 1;               break;
						case ibQueryFrame::RowsBeforeCurrent:  end = p;                   break;
						case ibQueryFrame::RangeThroughPeers:
						case ibQueryFrame::NoFrame:            end = peersEnd[p - b];     break;
						}
					}
					values[order[p]] = prefix[end - b];
				}
			}
			b = e;
		}
	}
}

ibMemoryCell Statement::Column(const ibQueryExpr& e, const Scope& s)
{
	// …an output column's name first, while the ORDER BY is read — SQL takes the output's name before the source's.
	if (s.m_outNames != nullptr && e.m_qualifier.IsEmpty()) {
		const ibFieldNameEqual same;
		for (size_t i = 0; i < s.m_outNames->size(); ++i)
			if (same((*s.m_outNames)[i], e.m_name))
				return (*s.m_outRow)[i];
	}
	const auto cached = m_found.find(&e);
	for (const Scope* at = &s; at != nullptr; at = at->m_outer) {
		if (at->m_rel == nullptr)
			continue;
		if (cached != m_found.end() && cached->second.first == at->m_rel) {
			const size_t i = static_cast<size_t>(cached->second.second);
			if (i < at->m_rel->Width() && ibFieldNameEqual()(at->m_rel->m_names[i], e.m_name)
				&& (e.m_qualifier.IsEmpty() || ibFieldNameEqual()(at->m_rel->m_qualifiers[i], e.m_qualifier)))
				return at->CellAt(i);
		}
		const int i = FindColumn(*at->m_rel, e.m_qualifier, e.m_name);
		if (i >= 0) {
			m_found[&e] = { at->m_rel, i };
			return at->CellAt(static_cast<size_t>(i));
		}
	}
	Refuse(ibString::Format(_("Column unknown: %s"), e.m_qualifier.IsEmpty() ? e.m_name : e.m_qualifier + ibT(".") + e.m_name));
}

// The group key (its flattened element) a grouped select's expression repeats, or -1. A bare column is known by the
// source column it names, whatever its spelling; anything else by what it says.
int Statement::KeyOf(const ibQueryExpr& e, const Scope& s) const
{
	const Grouping& grouping = *s.m_grouping;
	const int column = e.m_kind == ibQueryExprKind::Column && e.m_name != ibT("*") && s.m_rel != nullptr
		? FindColumn(*s.m_rel, e.m_qualifier, e.m_name) : -1;
	for (size_t f = 0; f < grouping.m_flat.size(); ++f)
		if (column >= 0 ? grouping.m_column[f] == column : SameExpr(grouping.m_flat[f].get(), &e))
			return static_cast<int>(f);
	return -1;
}

ibMemoryCell Statement::Aggregate(const ibQueryExpr& e, const Scope& s)
{
	const bool star = e.m_args.empty();
	Fold fold;
	std::unordered_set<ibCells, RowHash, RowEqual> distinct;
	Scope row;
	row.m_rel   = s.m_rel;
	row.m_outer = s.m_outer;
	for (const ibRow* r : s.m_group->m_rows) {
		row.m_row = r;
		const ibMemoryCell v = star ? NumberCell(ibNumber(1)) : Eval(e.m_args.front(), row);
		if (!e.m_distinct || v.IsNull() || distinct.insert(ibCells{ v }).second)
			fold.Add(v);
	}
	return fold.Result(e.m_name, star);
}

ibMemoryCell Statement::Call(const ibQueryExpr& e, const Scope& s)
{
	if (e.m_over) {
		if (s.m_windows != nullptr) {
			const auto found = s.m_windows->find(&e);
			if (found != s.m_windows->end() && s.m_outIndex < found->second.size())
				return found->second[s.m_outIndex];
		}
		Refuse(_("A window function stands where no output row is being made"));
	}
	if (e.m_name.IsSameAs(ibT("COALESCE"), false)) {
		for (const ibQueryExprPtr& arg : e.m_args) {
			ibMemoryCell v = Eval(arg, s);
			if (!v.IsNull())
				return v;
		}
		return NullCell();
	}
	if (IsAggregateName(e.m_name) || e.m_name.IsSameAs(ibT("GROUPING"), false))
		Refuse(ibString::Format(_("%s stands outside a grouped select"), e.m_name));
	Refuse(ibString::Format(_("A base in memory has no function %s"), e.m_name.IsEmpty() ? ibString(ibT("( , )")) : e.m_name));
}

const std::vector<ibMemoryCell>& Statement::SetOf(const ibQueryRelPtr& rel, const Scope& s, std::vector<ibMemoryCell>& correlated)
{
	auto firstColumn = [](Relation r) {
		std::vector<ibMemoryCell> values;
		for (const ibRow& row : r.m_rows)
			values.push_back(row[0]);   // NULL for a row of no fields
		return values;
	};
	const auto known = m_sets.find(rel.get());
	if (known != m_sets.end())
		return known->second;
	if (m_correlated.count(rel.get()) == 0) {
		// Read once with no row around it; a subquery that needs the row around it says so by not finding a column.
		try {
			return m_sets.emplace(rel.get(), firstColumn(Select(rel, nullptr))).first->second;
		}
		catch (const ibDatabaseLayerException&) {
			m_correlated.insert(rel.get());
		}
	}
	correlated = firstColumn(Select(rel, &s));
	return correlated;
}

ibMemoryCell Statement::In(const ibQueryExpr& e, const Scope& s)
{
	// An empty list is a constant — the probe is not read (the renderer writes 1 = 0).
	if (!e.m_subquery && e.m_args.empty())
		return TriCell(e.m_negated ? Tri::True : Tri::False);
	const ibMemoryCell probe = Eval(e.m_lhs, s);
	std::vector<ibMemoryCell> listed, correlated;
	const std::vector<ibMemoryCell>* values = &listed;
	if (e.m_subquery)
		values = &SetOf(e.m_subquery, s, correlated);
	else
		for (const ibQueryExprPtr& arg : e.m_args)
			listed.push_back(Eval(arg, s));
	if (values->empty())
		return TriCell(e.m_negated ? Tri::True : Tri::False);
	if (probe.IsNull())
		return NullCell();
	Tri res = Tri::False;
	for (const ibMemoryCell& v : *values) {
		const Tri t = CompareTri(ibQueryBinOp::Eq, probe, v);
		if (t == Tri::True) {
			res = Tri::True;
			break;
		}
		if (t == Tri::Unknown)
			res = Tri::Unknown;
	}
	return TriCell(e.m_negated ? TriNot(res) : res);
}

ibMemoryCell Statement::Eval(const ibQueryExprPtr& ptr, const Scope& s)
{
	if (!ptr)
		return NullCell();
	const ibQueryExpr& e = *ptr;
	if (e.m_with)
		Declare(*e.m_with);

	// IN A GROUPED SELECT an aggregate folds the group, GROUPING says whether its key was rolled away, and a key is
	// the group's — NULL in a level that rolled it away.
	if (s.m_group != nullptr) {
		if (IsAggregate(e))
			return Aggregate(e, s);
		if (e.m_kind == ibQueryExprKind::Func && !e.m_over && e.m_name.IsSameAs(ibT("GROUPING"), false)) {
			const int f = e.m_args.empty() || !e.m_args.front() ? -1 : KeyOf(*e.m_args.front(), s);
			if (f < 0)
				Refuse(_("GROUPING asks of an expression that is not a group key"));
			return NumberCell(ibNumber(s.m_grouping->m_keyOf[static_cast<size_t>(f)] < s.m_group->m_level ? 0 : 1));
		}
		if (e.m_kind != ibQueryExprKind::Const && e.m_kind != ibQueryExprKind::Param) {
			const int f = KeyOf(e, s);
			if (f >= 0)
				return s.m_grouping->m_keyOf[static_cast<size_t>(f)] < s.m_group->m_level
					? s.m_group->m_keys[static_cast<size_t>(f)] : NullCell();
		}
	}

	switch (e.m_kind) {
	case ibQueryExprKind::Column:
		return Column(e, s);

	case ibQueryExprKind::Const: {
		const auto known = m_consts.find(&e);
		if (known != m_consts.end())
			return known->second;
		return m_consts.emplace(&e, e.m_blob.GetDataLen() > 0 ? ibMemoryCell::Of(e.m_blob.GetData(), e.m_blob.GetDataLen())
		                                                      : CellOf(e.m_const)).first->second;
	}

	case ibQueryExprKind::Param:
		return e.m_paramIndex >= 0 && e.m_paramIndex < static_cast<int>(m_params.size())
			? m_params[static_cast<size_t>(e.m_paramIndex)] : NullCell();

	case ibQueryExprKind::BinOp: {
		if (e.m_binOp == ibQueryBinOp::And || e.m_binOp == ibQueryBinOp::Or) {
			const Tri a = Test(e.m_lhs, s);
			// The second operand is not read where the first decides — a refusal it would raise is not raised.
			if (e.m_binOp == ibQueryBinOp::And && a == Tri::False) return TriCell(Tri::False);
			if (e.m_binOp == ibQueryBinOp::Or  && a == Tri::True)  return TriCell(Tri::True);
			const Tri b = Test(e.m_rhs, s);
			return TriCell(e.m_binOp == ibQueryBinOp::And ? TriAnd(a, b) : TriOr(a, b));
		}
		const ibMemoryCell a = Eval(e.m_lhs, s);
		const ibMemoryCell b = Eval(e.m_rhs, s);
		switch (e.m_binOp) {
		case ibQueryBinOp::Eq: case ibQueryBinOp::Ne: case ibQueryBinOp::Lt:
		case ibQueryBinOp::Le: case ibQueryBinOp::Gt: case ibQueryBinOp::Ge:
			return Compare(e.m_binOp, a, b);
		case ibQueryBinOp::Like:
			if (a.IsNull() || b.IsNull())
				return NullCell();
			return BoolCell(Like(TextOf(a), TextOf(b)));
		default:
			break;
		}
		if (a.IsNull() || b.IsNull())
			return NullCell();
		if (e.m_binOp == ibQueryBinOp::Concat)
			return ibMemoryCell::Of(TextOf(a) + TextOf(b));
		const ibNumber x = NumberOf(a), y = NumberOf(b);
		switch (e.m_binOp) {
		case ibQueryBinOp::Add: return NumberCell(x + y);
		case ibQueryBinOp::Sub: return NumberCell(x - y);
		case ibQueryBinOp::Mul: return NumberCell(x * y);
		// A DIVISION BY ZERO IS REFUSED, in the language's own sentence — as PostgreSQL, Firebird and MS SQL refuse it,
		// and as the memory road does (EvalColumnExprRow).
		case ibQueryBinOp::Div:
		case ibQueryBinOp::Mod:
			if (y.IsZero())
				ibBackendCoreException::Error(ibScriptError_DivideByZero);
			return NumberCell(e.m_binOp == ibQueryBinOp::Div ? x / y : x % y);
		default:
			break;
		}
		return NullCell();
	}

	case ibQueryExprKind::Func:
		return Call(e, s);

	case ibQueryExprKind::Case:
		for (const auto& branch : e.m_cases)
			if (Test(branch.first, s) == Tri::True)
				return Eval(branch.second, s);
		return Eval(e.m_else, s);

	case ibQueryExprKind::In:
		return In(e, s);

	case ibQueryExprKind::IsNull: {
		const bool isNull = Eval(e.m_lhs, s).IsNull();
		return BoolCell(e.m_negated ? !isNull : isNull);
	}

	case ibQueryExprKind::Not:
		return TriCell(TriNot(Test(e.m_lhs, s)));

	case ibQueryExprKind::Cast:
		return CastTo(Eval(e.m_lhs, s), e.m_castType);

	case ibQueryExprKind::Exists: {
		const bool any = !Select(e.m_subquery, &s).m_rows.empty();
		return BoolCell(e.m_negated ? !any : any);
	}

	case ibQueryExprKind::PeriodTrunc:
	case ibQueryExprKind::PeriodEnd:
	case ibQueryExprKind::DatePart: {
		ibDateTime date;
		if (!DateOf(Eval(e.m_lhs, s), date))
			return NullCell();
		if (e.m_kind == ibQueryExprKind::DatePart)
			return NumberCell(ibNumber(date.GetPart(e.m_datePart)));
		return ibMemoryCell::Of(e.m_kind == ibQueryExprKind::PeriodTrunc ? date.BeginOfPeriod(e.m_periodUnit) : date.EndOfPeriod(e.m_periodUnit));
	}

	case ibQueryExprKind::DateAdd: {
		ibDateTime date;
		if (!DateOf(Eval(e.m_lhs, s), date))
			return NullCell();
		const ibMemoryCell count = e.m_args.empty() ? NumberCell(ibNumber()) : Eval(e.m_args.front(), s);
		if (count.IsNull())
			return NullCell();
		long steps = 0;
		NumberOf(count).ToInt(steps);
		return ibMemoryCell::Of(date.AddPeriods(e.m_periodUnit, steps));
	}

	case ibQueryExprKind::DateDiff: {
		ibDateTime from, to;
		if (!DateOf(Eval(e.m_lhs, s), from) || !DateOf(Eval(e.m_rhs, s), to))
			return NullCell();
		return NumberCell(ibNumber(from.PeriodsUntil(to, e.m_periodUnit)));
	}

	case ibQueryExprKind::Substring: {
		const ibMemoryCell text = Eval(e.m_lhs, s);
		// The renderer's defaults: from 1, and a length of 0 where none is given.
		const ibMemoryCell from = e.m_args.size() > 0 ? Eval(e.m_args[0], s) : NumberCell(ibNumber(1));
		const ibMemoryCell len  = e.m_args.size() > 1 ? Eval(e.m_args[1], s) : NumberCell(ibNumber());
		if (text.IsNull() || from.IsNull() || len.IsNull())
			return NullCell();
		const ibString value = TextOf(text);
		long long y = 0, z = 0;
		NumberOf(from).ToInt(y);
		NumberOf(len).ToInt(z);
		// SQL's positions: from max(y, 1) through y + z - 1, both 1-based.
		const long long first = std::max<long long>(y, 1);
		const long long last  = std::min<long long>(y + z - 1, static_cast<long long>(value.length()));
		if (last < first)
			return ibMemoryCell::Of(ibString());
		return ibMemoryCell::Of(value.Mid(static_cast<size_t>(first - 1), static_cast<size_t>(last - first + 1)));
	}
	}
	return NullCell();
}

// A column's DEFAULT (ibDdlColumn::m_default) — a constant of the IR, the cell Eval makes of one; none is NULL.
ibMemoryCell DefaultOf(const ibQueryExprPtr& value)
{
	if (!value)
		return NullCell();
	if (value->m_kind != ibQueryExprKind::Const)
		Refuse(_("A base in memory takes a constant for a column's default"));
	return value->m_blob.GetDataLen() > 0 ? ibMemoryCell::Of(value->m_blob.GetData(), value->m_blob.GetDataLen())
	                                      : CellOf(value->m_const);
}

int FieldIndex(const ibMemoryTable& table, const ibString& field)
{
	const ibFieldNameEqual same;
	for (size_t i = 0; i < table.m_fields.size(); ++i)
		if (same(table.m_fields[i], field))
			return static_cast<int>(i);
	Refuse(ibString::Format(_("Column unknown: %s"), field));
}

} // namespace

// ==========================================================================
// ibQueryInterpreter
// ==========================================================================

ibQueryInterpreter::ibQueryInterpreter(ibDatabaseLayerMemory& base)
	: m_base(base)
{
}

ibMemoryTable ibQueryInterpreter::Execute(const ibQueryIR& ir, const std::vector<ibValue>& params)
{
	std::lock_guard<std::mutex> lock(m_base.m_tables.m_mutex);
	m_base.m_cancelled = false;
	Statement statement(m_base.m_tables.m_byName, params, m_base.m_cancelled);
	for (const ibQueryCte& cte : ir.m_with)
		statement.Declare(cte);
	Relation result = statement.Select(ir.m_root, nullptr);
	ibMemoryTable table;
	table.m_fields = result.m_names;
	table.m_rows   = std::move(result.m_rows);
	return table;
}

int ibQueryInterpreter::Execute(const ibDdlStatement& ddl)
{
	std::lock_guard<std::mutex> lock(m_base.m_tables.m_mutex);
	ibTableMap& tables = m_base.m_tables.m_byName;
	const auto found = tables.find(ddl.m_table);
	auto table = [&]() -> ibMemoryTable& {
		if (found == tables.end())
			Refuse(ibString::Format(_("Table unknown: %s"), ddl.m_table));
		return found->second;
	};
	// A row is one block: a field added or dropped makes every row again.
	auto addColumn = [](ibMemoryTable& t, const ibDdlColumn& column) {
		t.m_fields.push_back(column.m_name);
		t.m_defaults.push_back(DefaultOf(column.m_default));
		ibCells cells;
		for (ibRow& row : t.m_rows.GetWriteRows()) {
			cells.clear();
			for (size_t i = 0; i + 1 < t.m_fields.size(); ++i)
				cells.push_back(row[i]);
			cells.push_back(t.m_defaults.back());
			row = ibRow(cells);
		}
	};
	auto dropColumn = [](ibMemoryTable& t, const ibString& name) {
		const size_t at = static_cast<size_t>(FieldIndex(t, name));
		const size_t width = t.m_fields.size();
		t.m_fields.erase(t.m_fields.begin() + static_cast<std::ptrdiff_t>(at));
		t.m_defaults.erase(t.m_defaults.begin() + static_cast<std::ptrdiff_t>(at));
		ibCells cells;
		for (ibRow& row : t.m_rows.GetWriteRows()) {
			cells.clear();
			for (size_t i = 0; i < width; ++i)
				if (i != at)
					cells.push_back(row[i]);
			row = ibRow(cells);
		}
	};

	switch (ddl.m_kind) {
	case ibDdlKind::CreateTable: {
		if (found != tables.end()) {
			if (ddl.m_ifNotExists)
				return 0;
			Refuse(ibString::Format(_("Table %s already exists"), ddl.m_table));
		}
		ibMemoryTable created;
		for (const ibDdlColumn& column : ddl.m_columns) {
			created.m_fields.push_back(column.m_name);
			created.m_defaults.push_back(DefaultOf(column.m_default));
		}
		tables.emplace(ddl.m_table, std::move(created));
		return 0;
	}
	case ibDdlKind::DropTable:
		if (found == tables.end()) {
			if (ddl.m_ifExists)
				return 0;
			Refuse(ibString::Format(_("Table unknown: %s"), ddl.m_table));
		}
		tables.erase(found);
		return 0;
	case ibDdlKind::AddColumn:
		if (!ddl.m_columns.empty())
			addColumn(table(), ddl.m_columns.front());
		return 0;
	case ibDdlKind::DropColumn:
		if (!ddl.m_columns.empty())
			dropColumn(table(), ddl.m_columns.front().m_name);
		return 0;
	case ibDdlKind::RenameColumn:
		if (ddl.m_columns.size() >= 2) {
			ibMemoryTable& t = table();
			t.m_fields[static_cast<size_t>(FieldIndex(t, ddl.m_columns[0].m_name))] = ddl.m_columns[1].m_name;
		}
		return 0;
	case ibDdlKind::AlterTable: {
		ibMemoryTable& t = table();
		for (const ibAlterClause& clause : ddl.m_alterClauses) {
			if (clause.m_op == ibAlterOp::Add)
				addColumn(t, clause.m_column);
			else
				dropColumn(t, clause.m_column.m_name);
		}
		return 0;
	}
	// A cell holds its own value whatever the column is declared as; an index and the optimiser's statistics have
	// nothing to stand for here.
	case ibDdlKind::AlterColumn:
	case ibDdlKind::CreateIndex:
	case ibDdlKind::DropIndex:
	case ibDdlKind::Analyze:
		return 0;
	}
	return 0;
}

int ibQueryInterpreter::Execute(const ibDmlStatement& dml, const std::vector<ibValue>& params)
{
	return static_cast<int>(ExecuteReturning(dml, params).m_rows.size());
}

// ⭐ ONE WRITE, ITS ROWS HANDED BACK: the count a plain write answers is the number of rows a RETURNING one gives — so
// the two cannot disagree about what was written. An empty RETURNING list hands back the rows with no fields.
ibMemoryTable ibQueryInterpreter::ExecuteReturning(const ibDmlStatement& dml, const std::vector<ibValue>& params)
{
	std::lock_guard<std::mutex> lock(m_base.m_tables.m_mutex);
	m_base.m_cancelled = false;
	Statement statement(m_base.m_tables.m_byName, params, m_base.m_cancelled);
	ibMemoryTable& target = statement.Table(dml.m_table);

	std::vector<int> returning;
	for (const ibString& field : dml.m_returning)
		returning.push_back(FieldIndex(target, field));
	ibMemoryTable written;
	written.m_fields = dml.m_returning;
	std::vector<ibRow>& writtenRows = written.m_rows.GetWriteRows();
	ibCells back;
	auto wrote = [&](const ibRow& row) {
		back.clear();
		for (int i : returning)
			back.push_back(row[static_cast<size_t>(i)]);
		writtenRows.emplace_back(back);
	};
	// A row as a write leaves it: `cells` are what it holds, `at` the fields the values given go to.
	ibCells cells;
	auto placed = [&cells](const std::vector<int>& at, const ibRow& values) {
		for (size_t c = 0; c < at.size(); ++c)
			cells[static_cast<size_t>(at[c])] = values[c];
		return ibRow(cells);
	};
	auto cellsOf = [&cells](const ibRow& row) {
		cells.clear();
		for (size_t i = 0; i < row.size(); ++i)
			cells.push_back(row[i]);
	};

	// The table as a relation its WHERE and its values read — by its own name, and its names alone: a scope reads the
	// row it is handed (rowScope.m_row), and a relation holding the rows would make every write here copy the table.
	Relation self;
	self.m_names = target.m_fields;
	self.m_qualifiers.assign(self.m_names.size(), dml.m_table);
	Scope rowScope;
	rowScope.m_rel = &self;
	const Scope none{};

	switch (dml.m_kind) {
	case ibDmlKind::Insert:
	case ibDmlKind::Upsert: {
		std::vector<int> columns;
		std::vector<ibRow> incoming;
		if (dml.m_kind == ibDmlKind::Insert && dml.m_selectSource) {
			Relation source = statement.Select(dml.m_selectSource, nullptr);
			for (const ibString& field : dml.m_insertColumns.empty() ? target.m_fields : dml.m_insertColumns)
				columns.push_back(FieldIndex(target, field));
			if (source.Width() != columns.size())
				Refuse(_("INSERT ... SELECT gives a different number of columns than it fills"));
			incoming = source.m_rows.TakeRows();
		}
		else {
			ibCells first;
			for (const ibDmlAssign& assign : dml.m_assignments) {
				columns.push_back(FieldIndex(target, assign.m_column));
				first.push_back(statement.Eval(assign.m_value, none));
			}
			incoming.emplace_back(first);
			for (const std::vector<ibQueryExprPtr>& extra : dml.m_extraRows) {
				ibCells row;
				for (const ibQueryExprPtr& value : extra)
					row.push_back(statement.Eval(value, none));
				row.resize(columns.size());
				incoming.emplace_back(row);
			}
		}
		std::vector<int> keys;
		for (const ibString& key : dml.m_matchKeys)
			keys.push_back(FieldIndex(target, key));
		std::vector<ibRow>& rows = target.m_rows.GetWriteRows();
		for (ibRow& values : incoming) {
			ibRow* existing = nullptr;
			if (dml.m_kind == ibDmlKind::Upsert && !keys.empty()) {
				// The row with the same key — NULL matching nothing, as a key with a NULL conflicts with no row.
				for (ibRow& row : rows) {
					bool same = true;
					for (int key : keys) {
						const auto at = std::find(columns.begin(), columns.end(), key);
						const ibMemoryCell value = at != columns.end() ? values[static_cast<size_t>(at - columns.begin())] : NullCell();
						const ibMemoryCell held = row[static_cast<size_t>(key)];
						if (value.IsNull() || held.IsNull() || Order(value, held) != 0) {
							same = false;
							break;
						}
					}
					if (same) {
						existing = &row;
						break;
					}
				}
			}
			if (existing != nullptr) {
				cellsOf(*existing);
				for (size_t c = 0; c < columns.size(); ++c)
					if (std::find(keys.begin(), keys.end(), columns[c]) == keys.end())
						cells[static_cast<size_t>(columns[c])] = values[c];
				*existing = ibRow(cells);
				wrote(*existing);
				continue;
			}
			cells = target.m_defaults;   // a field the INSERT does not list holds its DEFAULT, as in SQL
			rows.push_back(placed(columns, values));
			wrote(rows.back());
		}
		break;
	}
	case ibDmlKind::Update: {
		std::vector<int> columns;
		for (const ibDmlAssign& assign : dml.m_assignments)
			columns.push_back(FieldIndex(target, assign.m_column));
		for (ibRow& row : target.m_rows.GetWriteRows()) {
			rowScope.m_row = &row;
			if (dml.m_where && statement.Test(dml.m_where, rowScope) != Tri::True)
				continue;
			// Every value read off the row as it was, then written — SET a = b, b = a swaps.
			ibCells values;
			for (const ibDmlAssign& assign : dml.m_assignments)
				values.push_back(statement.Eval(assign.m_value, rowScope));
			cellsOf(row);
			for (size_t c = 0; c < columns.size(); ++c)
				cells[static_cast<size_t>(columns[c])] = values[c];
			row = ibRow(cells);
			wrote(row);
		}
		break;
	}
	case ibDmlKind::Delete: {
		// Which rows go is decided over the table as it stands, and only then are they taken out — a condition that
		// reads the same table (a correlated EXISTS) reads it whole.
		std::vector<char> goes(target.m_rows.size(), 0);
		for (size_t r = 0; r < target.m_rows.size(); ++r) {
			rowScope.m_row = &target.m_rows[r];
			goes[r] = !dml.m_where || statement.Test(dml.m_where, rowScope) == Tri::True;
		}
		std::vector<ibRow> all = target.m_rows.TakeRows();
		std::vector<ibRow> kept;
		for (size_t r = 0; r < all.size(); ++r) {
			if (goes[r])
				wrote(all[r]);
			else
				kept.push_back(std::move(all[r]));
		}
		target.m_rows = ibMemoryRows(std::move(kept));
		break;
	}
	}
	return written;
}

int ibQueryInterpreter::Insert(const ibString& table, ibMemoryTable rows)
{
	std::lock_guard<std::mutex> lock(m_base.m_tables.m_mutex);
	ibTableMap& tables = m_base.m_tables.m_byName;
	const auto found = tables.find(table);
	if (found == tables.end())
		Refuse(ibString::Format(_("Table unknown: %s"), table));
	ibMemoryTable& target = found->second;

	std::vector<int> columns;
	columns.reserve(rows.m_fields.size());
	for (const ibString& field : rows.m_fields)
		columns.push_back(FieldIndex(target, field));
	// The rows grow as push_back grows them. Reserved to exactly what each call adds, the table moved whole at every call:
	// a copy filled fifty rows at a time moved its 132 402 rows 2 649 times (2026-11-18, 69 s of a 112 s query).
	std::vector<ibRow> incoming = rows.m_rows.TakeRows();
	std::vector<ibRow>& into = target.m_rows.GetWriteRows();
	// ⭐ ROWS BOUND IN THE TABLE'S OWN LAYOUT — every field, in its order — GO IN AS THEY ARE: the block a batch bound is
	// the table's row, one more owner of it. Only a batch that fills some of the fields, or in another order, has its rows
	// made again around the table's defaults.
	bool asTheyAre = columns.size() == target.m_fields.size();
	for (size_t c = 0; asTheyAre && c < columns.size(); ++c)
		asTheyAre = columns[c] == static_cast<int>(c);
	ibCells cells;
	for (ibRow& values : incoming) {
		if (asTheyAre && values.size() == columns.size()) {
			into.push_back(std::move(values));
			continue;
		}
		cells = target.m_defaults;
		for (size_t c = 0; c < columns.size(); ++c)
			cells[static_cast<size_t>(columns[c])] = values[c];
		into.emplace_back(cells);
	}
	return static_cast<int>(incoming.size());
}

// ==========================================================================
// ibDatabaseResultSetMemory
// ==========================================================================

ibDatabaseResultSetMemory::ibDatabaseResultSetMemory(ibMemoryTable table)
	: m_table(std::move(table))
{
	// The first field of a name wins, as ibQueryResult::ColumnIndex finds it.
	for (size_t i = 0; i < m_table.m_fields.size(); ++i)
		m_fieldLookupMap.emplace(m_table.m_fields[i], static_cast<int>(i + 1));
}

bool ibDatabaseResultSetMemory::Next()
{
	if (m_row + 1 >= static_cast<long long>(m_table.m_rows.size())) {
		m_row = static_cast<long long>(m_table.m_rows.size());
		return false;
	}
	++m_row;
	return true;
}

void ibDatabaseResultSetMemory::Close()
{
	CloseMetaData();
	m_table.m_rows = ibMemoryRows();   // its count let go — the rows may be a table's still
	m_row = -1;
}

int ibDatabaseResultSetMemory::LookupField(const ibString& strField)
{
	// Refused as every driver refuses it (sqliteResultSet.cpp says why a missing field throws).
	const StringToIntMap::const_iterator found = m_fieldLookupMap.find(strField);
	if (found == m_fieldLookupMap.end())
		ibDatabaseLayerException::Throw(ibBackendDatabaseException::Kind::Unknown, DATABASE_LAYER_FIELD_NOT_IN_RESULTSET,
			ibEmptyString, ibT("Field '") + strField + ibT("' not found in the resultset"));
	return found->second;
}

ibMemoryCell ibDatabaseResultSetMemory::Cell(int nField) const
{
	if (m_row < 0 || m_row >= static_cast<long long>(m_table.m_rows.size()) || nField < 1)
		return ibMemoryCell();
	return m_table.m_rows[static_cast<size_t>(m_row)][static_cast<size_t>(nField - 1)];   // NULL past the row
}

int ibDatabaseResultSetMemory::GetResultInt(int nField)
{
	return NumberOf(Cell(nField)).ToInt();
}

ibString ibDatabaseResultSetMemory::GetResultString(int nField)
{
	return TextOf(Cell(nField));
}

long long ibDatabaseResultSetMemory::GetResultLong(int nField)
{
	long long value = 0;
	NumberOf(Cell(nField)).ToInt(value);
	return value;
}

bool ibDatabaseResultSetMemory::GetResultBool(int nField)
{
	const ibMemoryCell cell = Cell(nField);
	if (cell.GetKind() == ibCellKind::Bool)
		return cell.AsBool();
	if (cell.GetKind() == ibCellKind::String) {
		const ibString text = cell.AsString();
		return text == ibT("1") || text.IsSameAs(ibT("true"), false);
	}
	return !NumberOf(cell).IsZero();
}

ibDateTime ibDatabaseResultSetMemory::GetResultDate(int nField)
{
	ibDateTime date;
	return DateOf(Cell(nField), date) ? date : ibDateTime();
}

void* ibDatabaseResultSetMemory::GetResultBlob(int nField, ibMemoryBuffer& buffer)
{
	const ibMemoryCell cell = Cell(nField);
	if (!cell.IsBlob()) {
		buffer = ibMemoryBuffer();
		return nullptr;
	}
	// A copy of its own, as a driver hands bytes out of its cursor: the caller may write into what it was given.
	buffer = ibMemoryBuffer(cell.GetData(), cell.GetDataLen());
	return buffer.GetData();
}

double ibDatabaseResultSetMemory::GetResultDouble(int nField)
{
	return NumberOf(Cell(nField)).ToDouble();
}

ibNumber ibDatabaseResultSetMemory::GetResultNumber(int nField)
{
	return NumberOf(Cell(nField));
}

bool ibDatabaseResultSetMemory::IsFieldNull(int nField)
{
	return Cell(nField).IsNull();
}

ibResultSetMetaData* ibDatabaseResultSetMemory::GetMetaData()
{
	ibResultSetMetaData* metaData = new ibResultSetMetaDataMemory(m_table);
	LogMetaDataForCleanup(metaData);
	return metaData;
}

// ==========================================================================
// ibResultSetMetaDataMemory
// ==========================================================================

ibResultSetMetaDataMemory::ibResultSetMetaDataMemory(const ibMemoryTable& table)
	: m_table(table)
{
}

// A column's type is what its first value holds — a table in memory declares none, as SQLite's column has an affinity
// and every value its own type.
int ibResultSetMetaDataMemory::GetColumnType(int i)
{
	if (i < 1 || i > static_cast<int>(m_table.m_fields.size()))
		return COLUMN_UNKNOWN;
	for (const ibMemoryRow& row : m_table.m_rows) {
		switch (row[static_cast<size_t>(i - 1)].GetKind()) {
		case ibCellKind::Blob:   return COLUMN_BLOB;
		case ibCellKind::Bool:   return COLUMN_BOOL;
		case ibCellKind::Integer:
		case ibCellKind::Number: return COLUMN_DOUBLE;
		case ibCellKind::Date:   return COLUMN_DATE;
		case ibCellKind::String: return COLUMN_STRING;
		case ibCellKind::Null:   break;
		}
	}
	return COLUMN_NULL;
}

int ibResultSetMetaDataMemory::GetColumnSize(int)
{
	return 0;
}

ibString ibResultSetMetaDataMemory::GetColumnName(int i)
{
	return i >= 1 && i <= static_cast<int>(m_table.m_fields.size()) ? m_table.m_fields[static_cast<size_t>(i - 1)] : ibString();
}

int ibResultSetMetaDataMemory::GetColumnCount()
{
	return static_cast<int>(m_table.m_fields.size());
}
