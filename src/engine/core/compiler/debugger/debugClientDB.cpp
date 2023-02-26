////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part 
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include <3rdparty/databaseLayer/databaseLayer.h>
#include <3rdparty/databaseLayer/databaseErrorCodes.h>
#include "utils/stringUtils.h"
#include "appData.h" 

//db support 
void CDebuggerClient::LoadBreakpoints()
{
	if (m_breakpoints.size() > 0) {
		m_breakpoints.clear();
	}

	//Debug points
	if (!databaseLayer->TableExists(GetDebugPointTableName())) {
		databaseLayer->RunQuery("CREATE TABLE %s("
			"moduleName VARCHAR(128) NOT NULL,"
			"moduleLine INTEGER NOT NULL,"
			"PRIMARY KEY(moduleName, moduleLine));", GetDebugPointTableName());

		databaseLayer->RunQuery("CREATE INDEX %s_INDEX ON %s("
			"moduleName,"
			"moduleLine);", GetDebugPointTableName(), GetDebugPointTableName());
	}
	else {
		PreparedStatement* preparedStatement = databaseLayer->PrepareStatement("SELECT * FROM %s; ", GetDebugPointTableName());
		wxASSERT(preparedStatement);
		DatabaseResultSet* resultSetDebug = preparedStatement->RunQueryWithResults();
		wxASSERT(resultSetDebug);
		while (resultSetDebug->Next()) m_breakpoints[resultSetDebug->GetResultString("moduleName")][resultSetDebug->GetResultInt("moduleLine")] = 0;
		databaseLayer->CloseStatement(preparedStatement);
		resultSetDebug->Close();
	}
}

bool CDebuggerClient::ToggleBreakpointInDB(const wxString& sModuleName, unsigned int line)
{
	bool successful = true;
	PreparedStatement* preparedStatement = NULL;
	if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
		preparedStatement = databaseLayer->PrepareStatement("INSERT INTO %s(moduleName, moduleLine) VALUES('" + sModuleName + "', " + StringUtils::IntToStr(line) + ") ON CONFLICT (moduleName, moduleLine) DO UPDATE SET moduleName = excluded.moduleName, moduleLine = excluded.moduleLine; ", GetDebugPointTableName());
	else
		preparedStatement = databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s(moduleName, moduleLine) VALUES('" + sModuleName + "', " + StringUtils::IntToStr(line) + ") MATCHING (moduleName, moduleLine); ", GetDebugPointTableName());

	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in ToggleBreakpointInDB"); successful = false;
	}
	databaseLayer->CloseStatement(preparedStatement);
	return successful;
}

bool CDebuggerClient::RemoveBreakpointInDB(const wxString& sModuleName, unsigned int line)
{
	bool successful = true;
	PreparedStatement* preparedStatement = databaseLayer->PrepareStatement("DELETE FROM %s WHERE moduleName = '%s' AND moduleLine = %i;", GetDebugPointTableName(), sModuleName, line);
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveBreakpointInDB"); successful = false;
	}
	if (databaseLayer->RunQuery("DELETE FROM %s WHERE moduleName = '%s' AND moduleLine = %i", GetDebugPointTableName(), sModuleName, line) == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveBreakpointInDB: "); successful = false;
	}
	databaseLayer->CloseStatement(preparedStatement);
	return successful;
}

bool CDebuggerClient::OffsetBreakpointInDB(const wxString& sModuleName, unsigned int lineFrom, int offset)
{
	bool successful = true;
	PreparedStatement* preparedStatement = NULL;
	if (databaseLayer->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
		preparedStatement = databaseLayer->PrepareStatement("DELETE FROM %s WHERE moduleName = '" + sModuleName + "' AND moduleLine = " + StringUtils::IntToStr(lineFrom) + ";"
			"INSERT INTO %s(moduleName, moduleLine) VALUES('" + sModuleName + "', " + StringUtils::IntToStr(lineFrom + offset) + ") ON CONFLICT (moduleName, moduleLine) DO UPDATE SET moduleName = excluded.moduleName, moduleLine = excluded.moduleLine; ", GetDebugPointTableName(), GetDebugPointTableName());
	else
		preparedStatement = databaseLayer->PrepareStatement("DELETE FROM %s WHERE moduleName = '" + sModuleName + "' AND moduleLine = " + StringUtils::IntToStr(lineFrom) + ";"
			"UPDATE OR INSERT INTO %s (moduleName, moduleLine) VALUES('" + sModuleName + "', " + StringUtils::IntToStr(lineFrom + offset) + ") MATCHING (moduleName, moduleLine); ", GetDebugPointTableName(), GetDebugPointTableName());
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in OffsetBreakpointInDB"); successful = false;
	}
	databaseLayer->CloseStatement(preparedStatement);
	return successful;
}

bool CDebuggerClient::RemoveAllBreakPointsInDB()
{
	bool successful = true;
	PreparedStatement* preparedStatement = databaseLayer->PrepareStatement("DELETE FROM %s;", GetDebugPointTableName());
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveAllBreakPointsInDB"); successful = false;
	}
	return successful;
}