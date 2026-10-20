#ifndef _FRMCLIENT_BACKEND_QUERY_LOWERING_H__
#define _FRMCLIENT_BACKEND_QUERY_LOWERING_H__

// THE DESKTOP'S LOWERING, AS FAR AS THE QUERY CONSTRUCTOR ASKS IT (backend/query/queryLowering.h) — the same class, the
// same three questions. The folded columns are the written expressions' (AggregatedColumns, the desktop's code). Whether
// the names resolve, and which projections a GROUP BY lacks, are the engine's lowering run over the configuration's
// tables — none of the three requests (ibView::ibQueryConstructor) answers them: ⚠ NOT HERE YET. Until then they say
// nothing, which is the desktop's own answer for what it cannot verify — silence, never a false error.

#include "frmclient/backend/backend_core.h"   // FRMCLIENT_API
#include "core/query/queryAST.h"

#include <map>
#include <vector>

class ibValue;

class FRMCLIENT_API ibQueryLowering
{
public:
	// Do the names of `package` resolve? Raises ibCoreException with the engine's words when they do not.
	static void CheckNames(const ibQueryPackage& package, const std::map<wxString, ibValue>& params);

	// The projections a grouping query still has to group by; empty when it does not group, is sound, or cannot be
	// verified.
	static std::vector<ibQueryAstExprPtr> UngroupedProjections(const ibQuerySelect& ast,
		const std::map<wxString, ibValue>& params);

	// The columns the query already folds — those inside an aggregate call.
	static std::vector<ibQueryAstExprPtr> AggregatedColumns(const ibQuerySelect& ast);
};

#endif
