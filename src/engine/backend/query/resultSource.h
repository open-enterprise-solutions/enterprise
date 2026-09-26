#ifndef __RESULT_SOURCE_H__
#define __RESULT_SOURCE_H__

// ibDataResultSource — the polymorphic BACKING of an ibDataQueryResult selection: a physical
// DB cursor OR a computed RAM table, NEVER mixed. The selection forwards to it and never
// branches on which. The two implementations live in their own TUs — ibDbResultSource (the DB
// cursor materialisation) in dbTableProvider.cpp, ibRamTableResultSource (the composed / computed
// RAM table) in queryProvider.cpp — so this tiny shared header is the only thing both need. It
// names no L2 / metaobject type: pure abstraction over columns + values. (docs §22.4d)

#include "queryable.h"   // ibBackendQueryColumn (Value param) + ibValue / wxString

class ibDataResultSource {
public:
	virtual ~ibDataResultSource() = default;
	virtual bool     Next()                                       = 0;
	// A column of the current row. The row-key (uuid) is no special case — it is read as a RAW
	// column here too (the DB source reads it straight off the cursor by RawType; a RAM source
	// has no row-key, so it yields empty). No separate GuidString accessor. (docs §22.4d)
	virtual ibValue  Value(const ibBackendQueryColumn* col) const = 0;
	// ⭐⭐ AN OUTPUT BY ITS NAME — ONE DOOR, AND THE COLUMN SAYS HOW IT WAS PUT THERE. `col` null means the
	// output is one scalar field (an aggregate, an arithmetic); `col` given means it was projected as that
	// column's FIELD SPREAD under this name as a prefix (`<name>_TYPE` / `_RTRef` / `_RRRef` / …), because a
	// reference or an enum cannot ride one field, and the value is reassembled from them.
	//
	// 🛑 IT WAS TWO DOORS, AND THE SECOND HAD A DEFAULT — "read it by the prefix as a plain name", which is
	// true of a backing whose values are already reassembled and FALSE of a cursor's fields. So a source
	// could implement one and inherit the other, and the inherited answer was silence: the same query read
	// a value on one road and an empty cell on the other, with nothing anywhere to say which road it took
	// (measured 2026-09-24, a CASE over a reference). One door, no default, and every backing has to say
	// what it does about both shapes.
	virtual ibValue  Column(const wxString& name, const ibBackendQueryColumn* col = nullptr) const = 0;
	// ⭐ A BACKING THAT ALREADY IS A TABLE hands the table over, to a caller that was about to copy every
	// row of it cell by cell into a table of its own: the rows are moved out of it. Only before the first
	// Next(); null — a cursor's answer, and the default — means the rows are read one by one.
	virtual class ibQueryRamTable* Table() { return nullptr; }
};

#endif // __RESULT_SOURCE_H__
