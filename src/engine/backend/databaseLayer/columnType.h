#ifndef __COLUMN_TYPE_H__
#define __COLUMN_TYPE_H__

// Shared CANONICAL COLUMN TYPE — a pure leaf data type (no logic, no deps beyond wxString) in the LOW
// tier (databaseLayer) so BOTH L2 and L3 include it downward, with no layer reaching across:
//   * L2 — ibDdlColumn carries an ibColumnType; the dialect dictionary's TYPE-MAP renders it per DBMS.
//   * L3 — the column-layout tier's ibColumnSlot carries one (the slot/role themselves live in
//          columnLayout.h, an L3 concept L2 never touches).
// (Extracted from databaseQueryBuilder.h so the layout tier no longer pulls the whole L2 builder for a type.)

#include "backend/backend.h"

// The canonical column type — DBMS-neutral; the dialect dictionary's TYPE-MAP closes the per-DBMS forks
// (BLOB vs BYTEA, DATE vs TIMESTAMP, boolean-as-SMALLINT).
enum class ibCanonicalKind
{
	Boolean,
	Integer,
	BigInt,    // 64-bit integer
	Number,    // decimal(precision, scale)
	String,    // varchar(length) / char(length) when m_fixed
	Date,      // date / datetime / time — discriminated by m_datePrec
	Blob,
	Binary,    // fixed-length bytes (length) — indexable, unlike Blob
	Guid       // reference / unique id
};

// Sub-precision of a Date column — closes the DATE / TIMESTAMP / TIME fork via the
// dialect dictionary instead of branching in business code.
enum class ibDatePrec { Date, DateTime, Time };

struct ibColumnType
{
	ibCanonicalKind m_kind      = ibCanonicalKind::String;
	int             m_length    = 0;                       // String / Binary
	int             m_precision = 0;                       // Number
	int             m_scale     = 0;                        // Number
	ibDatePrec      m_datePrec  = ibDatePrec::DateTime;     // Date (default = legacy TIMESTAMP)
	bool            m_fixed     = false;                    // String: CHAR(n) instead of VARCHAR(n)
};

inline ibColumnType ibTypeBoolean()                 { ibColumnType t; t.m_kind = ibCanonicalKind::Boolean; return t; }
inline ibColumnType ibTypeInteger()                 { ibColumnType t; t.m_kind = ibCanonicalKind::Integer; return t; }
inline ibColumnType ibTypeBigInt()                  { ibColumnType t; t.m_kind = ibCanonicalKind::BigInt;  return t; }
inline ibColumnType ibTypeNumber(int prec, int scl) { ibColumnType t; t.m_kind = ibCanonicalKind::Number; t.m_precision = prec; t.m_scale = scl; return t; }
inline ibColumnType ibTypeString(int len)           { ibColumnType t; t.m_kind = ibCanonicalKind::String; t.m_length = len; return t; }
inline ibColumnType ibTypeChar(int len)             { ibColumnType t; t.m_kind = ibCanonicalKind::String; t.m_length = len; t.m_fixed = true; return t; }
inline ibColumnType ibTypeDate(ibDatePrec prec = ibDatePrec::DateTime) { ibColumnType t; t.m_kind = ibCanonicalKind::Date; t.m_datePrec = prec; return t; }
inline ibColumnType ibTypeBlob()                    { ibColumnType t; t.m_kind = ibCanonicalKind::Blob; return t; }
inline ibColumnType ibTypeBinary(int len)           { ibColumnType t; t.m_kind = ibCanonicalKind::Binary; t.m_length = len; return t; }
inline ibColumnType ibTypeGuid()                    { ibColumnType t; t.m_kind = ibCanonicalKind::Guid; return t; }

#endif // !__COLUMN_TYPE_H__
