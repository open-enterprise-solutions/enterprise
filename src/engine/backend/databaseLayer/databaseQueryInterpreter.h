#ifndef __DATABASE_QUERY_INTERPRETER_H__
#define __DATABASE_QUERY_INTERPRETER_H__

// ⭐⭐ THE IR EXECUTED OVER ROWS IN MEMORY — what ibQueryRenderer is for a base that speaks SQL, for the base that does
// not (databaseMemoryBase.h). L2 has two executors of one IR: the renderer writes it in a dialect for a driver, this
// runs it. It reads the IR the way the renderer writes it, so the two cannot mean different things:
//
//   - a relation chain is ONE select, flattened as RenderSelect flattens it (the first projection, sort, limit and
//     aggregate from the top, every filter AND-ed), and then run in SQL's order: FROM, WHERE, GROUP BY, HAVING, the
//     windows, the projection, DISTINCT, ORDER BY, LIMIT;
//   - logic is three-valued: a comparison with a NULL is UNKNOWN, a WHERE / ON / HAVING keeps a row on TRUE alone;
//   - a constant or a parameter binds as ibBindParam binds it — an empty value is NULL;
//   - LIKE compares with the case aside, as the renderer asks it (UPPER on both sides);
//   - NULL sorts first ascending and last descending; a division by zero is refused.
//
// docs/private/query-one-road.md.

#include "backend/databaseLayer/databaseQueryBuilder.h"   // ibQueryIR / ibDdlStatement / ibDmlStatement, ibValue
#include "backend/databaseLayer/databaseResultSet.h"
#include "backend/databaseLayer/resultSetMetaData.h"
#include "core/memory/memoryBuffer.h"

#include <memory>   // std::shared_ptr — rows held by every one who reads them
#include <vector>

class ibDatabaseLayerMemory;

// ⭐ A CELL WHERE IT LIES — the bytes it is in, held (one more owner of them), and its place there: read out of a row it
// is the row's own bytes, no copy; made here (Of) it is a block of its own. THIS BASE'S OWN FORMAT, nobody else's — a
// kind's byte, then what the kind needs:
//   Null nothing · Bool a byte · Integer an int64 · Number a u32 length and ibNumber's bytes · Date an int64
//   String a u32 count and the UTF-16 units · Blob a u32 length and the bytes.
// ONE SPELLING A VALUE — a whole number that fits is an Integer — so two equal cells are equal bytes. Only primitives and
// bytes: what a reference or a value of no declared type IS is not this layer's question (the column codec reads them).
class ibMemoryCell
{
public:
	enum class Kind : unsigned char { Null = 0, Bool, Integer, Number, Date, String, Blob };

	ibMemoryCell() noexcept = default;   // NULL: nothing held

	static ibMemoryCell Of(bool value);
	static ibMemoryCell Of(const ibNumber& value);
	static ibMemoryCell Of(const ibDateTime& value);
	static ibMemoryCell Of(const ibString& value);
	// …and bytes; none — NULL.
	static ibMemoryCell Of(const void* data, size_t len);

	Kind GetKind() const noexcept;
	bool IsNull() const noexcept { return GetKind() == Kind::Null; }
	bool IsBlob() const noexcept { return GetKind() == Kind::Blob; }

	bool         AsBool() const noexcept;
	std::int64_t AsInteger() const noexcept; // an Integer's int64; 0 for any other kind
	ibNumber     AsNumber() const;           // an Integer or a Number
	ibDateTime   AsDate() const noexcept;
	ibString     AsString() const;
	// A String's units or a Blob's bytes where they lie, and their length in bytes.
	const void* GetData() const noexcept;
	size_t      GetDataLen() const noexcept;

private:
	friend class ibMemoryRow;

	ibMemoryCell(const ibMemoryBuffer& bytes, std::uint32_t at) noexcept : m_bytes(bytes), m_at(at) {}

	const unsigned char* Base() const noexcept { return static_cast<const unsigned char*>(m_bytes.GetData()); }
	// The bytes it takes, its kind's byte with them.
	std::uint32_t Weight() const noexcept;

	ibMemoryBuffer m_bytes;   // none — NULL
	std::uint32_t  m_at = 0;
};

// A value as ibBindParam binds it — a boolean, a number, a date or a string as it is, anything else NULL: the IR's
// constant and parameter, met at the border once. Nothing else arrives — the codec binds a reference and a value of no
// declared type as their bytes (ibMemoryCell::Of(data, len)).
ibMemoryCell CellOf(const ibValue& v);

// ⭐ A ROW IN ONE BLOCK — at its head the count of its fields and where each one's cell begins (a u32 each; 0 — NULL),
// then the cells, one after another. A copy is the same bytes, one more owner; a cell read out is a handle into them.
// So a row a statement makes, a batch binds or a write puts is one block, not one a cell, and a copy into this base is
// paid once, at the border: inside it a row and a cell travel by their count.
class ibMemoryRow
{
public:
	ibMemoryRow() noexcept = default;   // no fields
	// `count` NULLs, with room for `room` bytes of cells besides (a batch's guess: the row it bound before).
	explicit ibMemoryRow(size_t count, size_t room = 0);
	// …and the cells given, in one block.
	explicit ibMemoryRow(const std::vector<ibMemoryCell>& cells);

	size_t size() const noexcept;
	bool empty() const noexcept { return size() == 0; }
	ibMemoryCell operator[](size_t i) const;
	// The bytes it takes.
	size_t Weight() const noexcept { return m_bytes.GetDataLen(); }

	// A field's cell: its bytes put after the cells there are, its place written. Each field is set once — a second
	// set leaves the bytes of the first behind, unread.
	void Set(size_t i, const ibMemoryCell& cell);
	void Set(size_t i, bool value);
	void Set(size_t i, const ibNumber& value);
	void Set(size_t i, const ibDateTime& value);
	void Set(size_t i, const ibString& value);
	void Set(size_t i, const void* data, size_t len);

private:
	ibMemoryBuffer m_bytes;   // none — no fields
};

// ⭐ ROWS, SHARED BY WHAT READS THEM — ibMemoryBuffer's rule, for a table's rows: a copy is a count, and a write into rows
// another still holds writes into a copy of its own first. So a statement that only hands a table on hands on its rows,
// held (`SELECT * FROM (SELECT t.f AS j0_f … FROM t) AS oes_composed`, the read of every composite, copied 132 402 rows
// of 36 cells: 6.3 s of query 7, 2026-11-18); a DROP of a table whose result is still open lets go of a count; and a
// transaction's snapshot of the space is a count per table.
//
// A holder is made only under the space's lock (a statement copies one; a result does not copy its own), and let go of
// anywhere: a write that finds the count stale finds it too high, and copies once more than it had to — never less.
class ibMemoryRows
{
public:
	ibMemoryRows() noexcept = default;
	explicit ibMemoryRows(std::vector<ibMemoryRow> rows);

	std::vector<ibMemoryRow>::const_iterator begin() const { return Rows().begin(); }
	std::vector<ibMemoryRow>::const_iterator end() const { return Rows().end(); }
	size_t size() const { return Rows().size(); }
	bool empty() const { return Rows().empty(); }
	const ibMemoryRow& operator[](size_t i) const { return Rows()[i]; }
	const ibMemoryRow& back() const { return Rows().back(); }

	// The rows to write into — this holder's alone first: a copy of them when another still reads them.
	std::vector<ibMemoryRow>& GetWriteRows();
	// …and handed on as a vector: moved out when this holder is the only one, copied otherwise.
	std::vector<ibMemoryRow> TakeRows();

private:
	const std::vector<ibMemoryRow>& Rows() const;

	std::shared_ptr<std::vector<ibMemoryRow>> m_rows;   // none: no rows
};

// ⭐ A TABLE AS THE BASE IN MEMORY HOLDS IT — its fields by name and its rows of cells, in the storage layout the
// statements address (the physical _N / _S / _RRRef fields a provider lays a value out into, not the columns of the
// metadata). One shape for the base's tables, for what the interpreter hands back from a statement, and for the cursor
// that walks it (ibDatabaseResultSetMemory).
struct ibMemoryTable
{
	std::vector<ibString>     m_fields;
	ibMemoryRows              m_rows;
	std::vector<ibMemoryCell> m_defaults;   // a base's table: per field, what a row written without it holds (its DEFAULT)
};

class ibQueryInterpreter
{
public:
	explicit ibQueryInterpreter(ibDatabaseLayerMemory& base);

	ibMemoryTable Execute(const ibQueryIR& ir, const std::vector<ibValue>& params);
	int Execute(const ibDdlStatement& ddl);
	int Execute(const ibDmlStatement& dml, const std::vector<ibValue>& params);
	// A write that hands back the rows it wrote (ibReturning), as ibDatabaseQueryBuilder::ExecuteReturning.
	ibMemoryTable ExecuteReturning(const ibDmlStatement& dml, const std::vector<ibValue>& params);
	// ⭐ ROWS THAT ARE CELLS ALREADY — `rows.m_fields` the fields they fill, in their order, as an INSERT lists its
	// columns: appended as the INSERT of them would append them, with no node to evaluate (ibBatchInsert over a base in
	// memory). Answers the rows written.
	int Insert(const ibString& table, ibMemoryTable rows);

private:
	ibDatabaseLayerMemory& m_base;
};

// The rows a statement the interpreter ran gave, walked as every driver's rows are walked — so ibQueryResult and the
// column codec read them by the same field names and the same typed reads, and cannot tell this base from one that
// speaks SQL. Each typed read answers what SQLite answers for the value the cell holds.
class ibDatabaseResultSetMemory : public ibDatabaseResultSet
{
public:
	explicit ibDatabaseResultSetMemory(ibMemoryTable table);

	virtual bool Next() override;
	virtual void Close() override;

	virtual int LookupField(const ibString& strField) override;

	virtual int GetResultInt(int nField) override;
	virtual ibString GetResultString(int nField) override;
	virtual long long GetResultLong(int nField) override;
	virtual bool GetResultBool(int nField) override;
	virtual ibDateTime GetResultDate(int nField) override;
	virtual void* GetResultBlob(int nField, ibMemoryBuffer& buffer) override;
	virtual double GetResultDouble(int nField) override;
	virtual ibNumber GetResultNumber(int nField) override;
	virtual bool IsFieldNull(int nField) override;

	virtual ibResultSetMetaData* GetMetaData() override;

private:
	// The cell of the current row a 1-based field names; NULL past the row or before the first Next.
	ibMemoryCell Cell(int nField) const;

	ibMemoryTable  m_table;
	long long      m_row = -1;
	StringToIntMap m_fieldLookupMap;
};

class ibResultSetMetaDataMemory : public ibResultSetMetaData
{
public:
	explicit ibResultSetMetaDataMemory(const ibMemoryTable& table);

	virtual int GetColumnType(int i) override;
	virtual int GetColumnSize(int i) override;
	virtual ibString GetColumnName(int i) override;
	virtual int GetColumnCount() override;

private:
	const ibMemoryTable& m_table;
};

#endif // __DATABASE_QUERY_INTERPRETER_H__
