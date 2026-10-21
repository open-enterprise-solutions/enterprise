#ifndef _FRMCLIENT_BACKEND_QUERY_LOWERING_H__
#define _FRMCLIENT_BACKEND_QUERY_LOWERING_H__

// THE DESKTOP'S LOWERING, AS FAR AS THE QUERY CONSTRUCTOR RUNS IT ON THE CLIENT (backend/query/queryLowering.h): the
// folded columns, read off the written expressions. CheckNames and UngroupedProjections resolve against the
// configuration and are the server's (ibQueryConstructorModel, request Check).

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
