////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : debugger - client part
////////////////////////////////////////////////////////////////////////////

#include "debugClient.h"
#include "backend/databaseLayer/databaseQueryBuilder.h"   // L2 door: the whole DAO rides this — no raw ibDatabaseLayer / result set

///////////////////////////////////////////////////////////////////////////////////////

#define dbg_table wxT("sys_dbg")

///////////////////////////////////////////////////////////////////////////////////////

bool ibDebuggerClient::TableAlreadyCreated()
{
	ibDatabaseQueryBuilder q;
	return q.TableExists(dbg_table);
}

bool ibDebuggerClient::CreateBreakpointDatabase()
{
	ibDatabaseQueryBuilder q;
	if (!q.TableExists(dbg_table)) {
		// (moduleName, moduleLine) is the breakpoint identity. The L2 CreateTable renderer
		// only carries inline single-column PRIMARY KEY, so the composite uniqueness rides
		// as a UNIQUE index — which is also exactly what the UPSERT match target needs.
		try {
			q.Execute(ibCreateTable(dbg_table, {
				{ wxT("moduleName"),    ibTypeString(128),  /*notNull*/true,  /*pk*/false, wxEmptyString },
				{ wxT("moduleLine"),    ibTypeInteger(),    /*notNull*/true,  /*pk*/false, wxEmptyString },
				{ wxT("lineCondition"), ibTypeString(1024), /*notNull*/false, /*pk*/false, wxEmptyString },
			}));
			q.Execute(ibCreateIndex(dbg_table, wxT("dbg_index"),
				{ wxT("moduleName"), wxT("moduleLine") }, /*unique*/true));
		}
		catch (...) { return false; }
	}
	else {
		// A BASE FROM BEFORE CONDITIONS (2026-09-11) has the table without the column - added in place, the
		// way sys_session grows (MigrateTableSession). Its rows read back with no condition: they stop always,
		// which is what they did.
		bool hasCondition = false;
		for (const wxString& column : q.GetColumns(dbg_table))
			if (column.CmpNoCase(wxT("lineCondition")) == 0)
				hasCondition = true;
		if (!hasCondition) {
			try { ibDatabaseQueryBuilder qa; qa.Execute(ibAddColumn(dbg_table, { wxT("lineCondition"), ibTypeString(1024), false, false, wxEmptyString })); }
			catch (...) { /* best-effort, as the session table's: without it a condition is not kept past the session */ }
		}
	}
	return q.IsOpen();
}

///////////////////////////////////////////////////////////////////////////////////////

//db support
void ibDebuggerClient::LoadBreakpointCollection(const wxString& strModuleName)
{
	m_listBreakpoint[strModuleName].clear();

	// SELECT moduleLine, lineCondition FROM sys_dbg WHERE moduleName = <strModuleName>
	try {
		ibDatabaseQueryBuilder q;
		ibQueryIR ir(
			ibProject(
				ibFilter(
					ibScan(dbg_table),
					ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("moduleName")),
					        ibConst(ibValue(strModuleName)))),
				{ { ibCol(wxT("moduleLine")), wxEmptyString }, { ibCol(wxT("lineCondition")), wxEmptyString } }));

		ibQueryResult res = q.ExecuteIR(ir);   // RAII: closes cursor + statement
		while (res.Next())
			m_listBreakpoint[strModuleName][res.GetResultInt(wxT("moduleLine"))] =
				ibBreakpoint{ 0, res.GetResultString(wxT("lineCondition")) };
	}
	catch (...) { /* no breakpoint table yet / passive scope — leave the collection empty */ }
}

bool ibDebuggerClient::ToggleBreakpointInDB(const wxString& strModuleName, unsigned int line, const wxString& condition)
{
	// One UPSERT — the L2 door renders ON CONFLICT (SQLite/PG) vs UPDATE OR INSERT … MATCHING (FB)
	// from the match keys, so the per-driver fork that used to live here is gone. The same row takes a new
	// condition: (module, line) is the identity, the condition only a property of it.
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibUpsert(dbg_table, {
			{ wxT("moduleName"),    ibConst(ibValue(strModuleName)) },
			{ wxT("moduleLine"),    ibConst(ibValue(line)) },
			{ wxT("lineCondition"), ibConst(ibValue(condition)) },
		}, { wxT("moduleName"), wxT("moduleLine") }));
	}
	catch (...) { wxASSERT_MSG(false, "error in ToggleBreakpointInDB"); return false; }
	return true;
}

bool ibDebuggerClient::RemoveBreakpointInDB(const wxString& strModuleName, unsigned int line)
{
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(dbg_table,
			ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("moduleName")), ibConst(ibValue(strModuleName))),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("moduleLine")), ibConst(ibValue(line))))));
	}
	catch (...) { wxASSERT_MSG(false, "error in RemoveBreakpointInDB"); return false; }
	return true;
}

bool ibDebuggerClient::OffsetBreakpointInDB(const wxString& strModuleName, unsigned int lineFrom, int offset, const wxString& condition)
{
	// Move a breakpoint from lineFrom to lineFrom+offset: delete the old row, upsert the new one - with its
	// condition, which moves with the line.
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(dbg_table,
			ibBinOp(ibQueryBinOp::And,
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("moduleName")), ibConst(ibValue(strModuleName))),
				ibBinOp(ibQueryBinOp::Eq, ibCol(wxT("moduleLine")), ibConst(ibValue(lineFrom))))));
		q.Execute(ibUpsert(dbg_table, {
			{ wxT("moduleName"),    ibConst(ibValue(strModuleName)) },
			{ wxT("moduleLine"),    ibConst(ibValue(static_cast<unsigned int>(lineFrom + offset))) },
			{ wxT("lineCondition"), ibConst(ibValue(condition)) },
		}, { wxT("moduleName"), wxT("moduleLine") }));
	}
	catch (...) { wxASSERT_MSG(false, "error in OffsetBreakpointInDB"); return false; }
	return true;
}

bool ibDebuggerClient::RemoveAllBreakpointInDB()
{
	try {
		ibDatabaseQueryBuilder q;
		q.Execute(ibDelete(dbg_table));   // no WHERE = all rows
	}
	catch (...) { /* best-effort */ }
	return true;
}
