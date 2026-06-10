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
	virtual ibValue  Column(const wxString& alias)          const = 0;   // by output name (aggregates)
	// Reconstruct a METADATA-OBJECT column (reference / enum / composite) from its field spread projected
	// under `prefix` (a dot-walk leaf joined as <prefix>_TYPE/_RTRef/_RRRef/…). Reassembles the value the
	// way a normal metadata column reads — vs Column(alias), which reads ONE scalar field. Default: a plain
	// read by the prefix as alias (a RAM backing already holds the reassembled value under that name).
	virtual ibValue  ColumnObject(const wxString& prefix, const ibBackendQueryColumn* /*col*/) const { return Column(prefix); }
};

#endif // __RESULT_SOURCE_H__
