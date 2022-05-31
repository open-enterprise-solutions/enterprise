////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part 
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include "databaseLayer/databaseLayer.h"
#include "databaseLayer/databaseErrorCodes.h"
#include "utils/stringUtils.h"
#include "appData.h" 

////////////////////////////////////////////////////////////////////////////

wxString CDebuggerClient::GetDebugPointTableName()
{
	return wxT("DEBUG_POINTS");
}

////////////////////////////////////////////////////////////////////////////

//db support 
void CDebuggerClient::LoadBreakpoints()
{
	if (m_aBreakpoints.size() > 0) {
		m_aBreakpoints.clear();
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
		PreparedStatement *preparedStatement = databaseLayer->PrepareStatement("SELECT * FROM %s; ", GetDebugPointTableName());
		wxASSERT(preparedStatement);
		DatabaseResultSet *m_resultSetDebug = preparedStatement->RunQueryWithResults();
		wxASSERT(m_resultSetDebug);
		while (m_resultSetDebug->Next()) m_aBreakpoints[m_resultSetDebug->GetResultString("moduleName")][m_resultSetDebug->GetResultInt("moduleLine")] = 0;
		databaseLayer->CloseStatement(preparedStatement);
		m_resultSetDebug->Close();
	}
}

bool CDebuggerClient::ToggleBreakpointInDB(const wxString &sModuleName, unsigned int line)
{
	bool successful = true;
	PreparedStatement *preparedStatement = databaseLayer->PrepareStatement("UPDATE OR INSERT INTO %s(moduleName, moduleLine) VALUES('" + sModuleName + "', " + StringUtils::IntToStr(line) + ") MATCHING (moduleName, moduleLine); ", GetDebugPointTableName());
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in ToggleBreakpointInDB"); successful = false;
	}
	databaseLayer->CloseStatement(preparedStatement);
	return successful;
}

bool CDebuggerClient::RemoveBreakpointInDB(const wxString &sModuleName, unsigned int line)
{
	bool successful = true;
	PreparedStatement *preparedStatement = databaseLayer->PrepareStatement("DELETE FROM %s WHERE moduleName = '%s' AND moduleLine = %i;", GetDebugPointTableName(), sModuleName, line);
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

bool CDebuggerClient::OffsetBreakpointInDB(const wxString &sModuleName, unsigned int lineFrom, int offset)
{
	bool successful = true;
	PreparedStatement *preparedStatement = databaseLayer->PrepareStatement("DELETE FROM %s WHERE moduleName = '" + sModuleName + "' AND moduleLine = " + StringUtils::IntToStr(lineFrom) + ";"
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
	PreparedStatement *preparedStatement = databaseLayer->PrepareStatement("DELETE FROM %s;", GetDebugPointTableName());
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveAllBreakPointsInDB"); successful = false;
	}
	return successful;
}