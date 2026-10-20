#include "queryLowering.h"

#include "frmclient/backend/compiler/value.h"

// ⚠ NOT HERE YET — the names resolve against the configuration (checked on the client against request Fields, query-constructor-doors.md). Unverifiable is silence.
void ibQueryLowering::CheckNames(const ibQueryPackage& /*package*/, const std::map<wxString, ibValue>& /*params*/)
{
}

// ⚠ NOT HERE YET — computed on the client once request Fields answers types (query-constructor-doors.md).
std::vector<ibQueryAstExprPtr> ibQueryLowering::UngroupedProjections(const ibQuerySelect& /*ast*/,
	const std::map<wxString, ibValue>& /*params*/)
{
	return {};
}

// ⚠ NOT HERE YET — the desktop reads the written expressions only (CollectAggregated), so this one could
// move to core instead of becoming a request.
std::vector<ibQueryAstExprPtr> ibQueryLowering::AggregatedColumns(const ibQuerySelect& /*ast*/)
{
	return {};
}
