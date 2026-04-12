////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part 
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/databaseLayer/databaseErrorCodes.h"

#include "backend/appData.h" 

///////////////////////////////////////////////////////////////////////////////////////

#define dbg_table wxT("sys_dbg")

///////////////////////////////////////////////////////////////////////////////////////

bool ibDebuggerClient::TableAlreadyCreated()
{
	return db_query->TableExists(dbg_table);
}

bool ibDebuggerClient::CreateBreakpointDatabase()
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
void ibDebuggerClient::LoadBreakpointCollection(const wxString& strModuleName)
{
	m_listBreakpoint[strModuleName].clear();

	ibDatabaseResultSet* databaseResultSet = db_query->RunQueryWithResults("SELECT * FROM %s WHERE moduleName = '%s'", dbg_table, strModuleName);
	wxASSERT(databaseResultSet);
	while (databaseResultSet->Next()) {
		m_listBreakpoint[strModuleName][databaseResultSet->GetResultInt("moduleLine")] = 0;
	}

	db_query->CloseResultSet(databaseResultSet);
}

bool ibDebuggerClient::ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line)
{
	bool successful = true;
	ibPreparedStatement* preparedStatement = nullptr;	
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		preparedStatement = db_query->PrepareStatement("INSERT INTO %s(moduleName, moduleLine) VALUES('" + strModuleName + "', " + stringUtils::IntToStr(line) + ") ON CONFLICT (moduleName, moduleLine) DO UPDATE SET moduleName = excluded.moduleName, moduleLine = excluded.moduleLine; ", dbg_table);
	else
		preparedStatement = db_query->PrepareStatement("UPDATE OR INSERT INTO %s(moduleName, moduleLine) VALUES('" + strModuleName + "', " + stringUtils::IntToStr(line) + ") MATCHING (moduleName, moduleLine); ", dbg_table);
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in ToggleBreakpointInDB"); successful = false;
	}
	db_query->CloseStatement(preparedStatement);
	return successful;
}

bool ibDebuggerClient::RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line)
{
	bool successful = true;
	ibPreparedStatement* preparedStatement = db_query->PrepareStatement("DELETE FROM %s WHERE moduleName = '%s' AND moduleLine = %i;", dbg_table, strModuleName, line);
	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveBreakpointInDB"); successful = false;
	}
	if (db_query->RunQuery("DELETE FROM %s WHERE moduleName = '%s' AND moduleLine = %i", dbg_table, strModuleName, line) == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in RemoveBreakpointInDB: "); successful = false;
	}
	db_query->CloseStatement(preparedStatement);
	return successful;
}

bool ibDebuggerClient::OffsetBreakpointInDB(const wxString& strModuleName, unsigned int lineFrom, int offset)
{
	bool successful = true;
	ibPreparedStatement* preparedStatement = nullptr;
	if (db_query->GetDatabaseLayerType() != DATABASELAYER_FIREBIRD)
		preparedStatement = db_query->PrepareStatement("DELETE FROM %s WHERE moduleName = '" + strModuleName + "' AND moduleLine = " + stringUtils::IntToStr(lineFrom) + ";"
			"INSERT INTO %s(moduleName, moduleLine) VALUES('" + strModuleName + "', " + stringUtils::IntToStr(lineFrom + offset) + ") ON CONFLICT (moduleName, moduleLine) DO UPDATE SET moduleName = excluded.moduleName, moduleLine = excluded.moduleLine; ", dbg_table, dbg_table);
	else
		preparedStatement = db_query->PrepareStatement("DELETE FROM %s WHERE moduleName = '" + strModuleName + "' AND moduleLine = " + stringUtils::IntToStr(lineFrom) + ";"
			"UPDATE OR INSERT INTO %s (moduleName, moduleLine) VALUES('" + strModuleName + "', " + stringUtils::IntToStr(lineFrom + offset) + ") MATCHING (moduleName, moduleLine); ", dbg_table, dbg_table);

	wxASSERT(preparedStatement);
	if (preparedStatement->RunQuery() == DATABASE_LAYER_QUERY_RESULT_ERROR) {
		wxASSERT_MSG(false, "error in OffsetBreakpointInDB"); successful = false;
	}
	db_query->CloseStatement(preparedStatement);
	return successful;
}

bool ibDebuggerClient::RemoveAllBreakpointInDB()
{
	db_query->RunQuery("DELETE FROM %s;", dbg_table);
	return true;
}