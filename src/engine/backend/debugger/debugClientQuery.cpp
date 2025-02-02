////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part 
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/appData.h" 

///////////////////////////////////////////////////////////////////////////////////////

#define dbg_table wxT("_dbg")

///////////////////////////////////////////////////////////////////////////////////////

bool CDebuggerClient::TableAlreadyCreated()
{
	return db_query->TableExists(dbg_table);
}

bool CDebuggerClient::CreateBreakpointDatabase()
{
	if (!db_query->TableExists(dbg_table)) {
		int retCode = db_query->RunQuery("create table %s("
			"moduleName VARCHAR(128) NOT NULL,"
			"moduleLine INTEGER NOT NULL,"
			"PRIMARY KEY(moduleName, moduleLine));", dbg_table);

		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) return false;	
		retCode = db_query->RunQuery("create index dbg_index on %s(moduleName, moduleLine);", dbg_table);	
		if (retCode == DATABASE_LAYER_QUERY_RESULT_ERROR) return false;	
	}
	return db_query->IsOpen();
}

///////////////////////////////////////////////////////////////////////////////////////

//db support 
void CDebuggerClient::LoadBreakpointCollection()
{
	if (m_listBreakpoint.size() > 0) m_listBreakpoint.clear();
	
	IDatabaseResultSet* resultSetDebug = db_query->RunQueryWithResults("SELECT * FROM %s", dbg_table);
	wxASSERT(resultSetDebug);
	while (resultSetDebug->Next()) {
		m_listBreakpoint[resultSetDebug->GetResultString("moduleName")][resultSetDebug->GetResultInt("moduleLine")] = 0;
	}
	resultSetDebug->Close();
}

bool CDebuggerClient::ToggleBreakpointInDB(const wxString& moduleName, unsigned int line)
{
	bool successful = true;
	IPreparedStatement* preparedStatement = nullptr;	
	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
		preparedStatement = db_query->PrepareStatement("INSERT INTO %s(moduleName, moduleLine) VALUES('" + moduleName + "', " + stringUtils::IntToStr(line) + ") ON CONFLICT (moduleName, moduleLine) DO UPDATE SET moduleName = excluded.moduleName, moduleLine = excluded.moduleLine; ", dbg_table);
	else
		preparedStatement = db_query->PrepareStatement("UPDATE OR INSERT INTO %s(moduleName, moduleLine) VALUES('" + moduleName + "', " + stringUtils::IntToStr(line) + ") MATCHING (moduleName, moduleLine); ", dbg_table);
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in ToggleBreakpointInDB"); successful = false;
	}
	db_query->CloseStatement(preparedStatement);
	return successful;
}

bool CDebuggerClient::RemoveBreakpointInDB(const wxString& moduleName, unsigned int line)
{
	bool successful = true;
	IPreparedStatement* preparedStatement = db_query->PrepareStatement("DELETE FROM %s WHERE moduleName = '%s' AND moduleLine = %i;", dbg_table, moduleName, line);
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveBreakpointInDB"); successful = false;
	}
	if (db_query->RunQuery("DELETE FROM %s WHERE moduleName = '%s' AND moduleLine = %i", dbg_table, moduleName, line) == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveBreakpointInDB: "); successful = false;
	}
	db_query->CloseStatement(preparedStatement);
	return successful;
}

bool CDebuggerClient::OffsetBreakpointInDB(const wxString& moduleName, unsigned int lineFrom, int offset)
{
	bool successful = true;
	IPreparedStatement* preparedStatement = nullptr;
	if (db_query->GetDatabaseLayerType() == DATABASELAYER_POSTGRESQL)
		preparedStatement = db_query->PrepareStatement("DELETE FROM %s WHERE moduleName = '" + moduleName + "' AND moduleLine = " + stringUtils::IntToStr(lineFrom) + ";"
			"INSERT INTO %s(moduleName, moduleLine) VALUES('" + moduleName + "', " + stringUtils::IntToStr(lineFrom + offset) + ") ON CONFLICT (moduleName, moduleLine) DO UPDATE SET moduleName = excluded.moduleName, moduleLine = excluded.moduleLine; ", dbg_table, dbg_table);
	else
		preparedStatement = db_query->PrepareStatement("DELETE FROM %s WHERE moduleName = '" + moduleName + "' AND moduleLine = " + stringUtils::IntToStr(lineFrom) + ";"
			"UPDATE OR INSERT INTO %s (moduleName, moduleLine) VALUES('" + moduleName + "', " + stringUtils::IntToStr(lineFrom + offset) + ") MATCHING (moduleName, moduleLine); ", dbg_table, dbg_table);

	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in OffsetBreakpointInDB"); successful = false;
	}
	db_query->CloseStatement(preparedStatement);
	return successful;
}

bool CDebuggerClient::RemoveAllBreakpointInDB()
{
	bool successful = true;
	IPreparedStatement* preparedStatement = db_query->PrepareStatement("DELETE FROM %s;", dbg_table);
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveAllBreakpointInDB"); successful = false;
	}
	return successful;
}