#ifndef _FRMCLIENT_BACKEND_QUERY_LOWERING_H__
#define _FRMCLIENT_BACKEND_QUERY_LOWERING_H__

// THE DESKTOP'S LOWERING, AS FAR AS THE QUERY CONSTRUCTOR RUNS IT HERE (backend/query/queryLowering.h) — the same
// class. The folded columns are the written expressions' (AggregatedColumns, the desktop's code). Whether the names
// resolve (CheckNames), and which projections a GROUP BY lacks (UngroupedProjections), are the engine's lowering run
// over the configuration's tables: the server runs them on the whole text (request Check, 27), asked through the
// window's ibQueryConstructorModel — the configuration the question is for travels with the question.

#include "frmclient/backend/backend_core.h"   // FRMCLIENT_API
#include "core/query/queryAST.h"

#include <vector>

class FRMCLIENT_API ibQueryLowering
{
public:
	// The columns the query already folds — those inside an aggregate call.
	static std::vector<ibQueryAstExprPtr> AggregatedColumns(const ibQuerySelect& ast);
};

#endif
