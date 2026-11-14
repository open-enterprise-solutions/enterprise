#include "valueDatabase.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"
#include "backend/backend_exception.h"


enum
{
	eNext,
};

ibValueResultSet::ibValueResultSet(ibDatabaseResultSet* resultSet)
	: ibValueDynamicMembers(ibValueTypes::TYPE_VALUE), m_resultSet(resultSet)
{
	m_members.Bind(this, &ibValueResultSet::FillMembers);
}

ibValueResultSet::~ibValueResultSet()
{
	if (m_resultSet != nullptr)
		ses_query->CloseResultSet(m_resultSet);
}

void ibValueResultSet::FillMembers(ibMemberTable& helper) const
{
	helper.AppendFunc(ibT("Next"), ibT("Next()"));

	if (m_resultSet != nullptr) {
		ibResultSetMetaData* resultSetMetaData = m_resultSet->GetMetaData();
		if (resultSetMetaData == nullptr)
			return;
		for (int idx = 1; idx <= resultSetMetaData->GetColumnCount(); idx++)
			helper.AppendProp(resultSetMetaData->GetColumnName(idx), true, false, idx);
	}
}

bool ibValueResultSet::SetPropVal(const long lPropNum, const ibValue& varPropVal)
{
	return false;
}

bool ibValueResultSet::GetPropVal(const long lPropNum, ibValue& pvarPropVal)
{
	// ⚠ A FIELD IS READ ON A ROW. Before the first Next(), or after the one that answered False, the driver's row
	// buffer holds what the allocation left there — a text field's length read from it wrote past the buffer, and
	// a script that read `rs.A` before `rs.Next()` took the whole application down (2026-11-14).
	if (m_resultSet == nullptr || !m_onRow)
		ibBackendCoreException::Error(_("the result set stands on no row: read a field after Next() answers True"));

	ibResultSetMetaData* resultSetMetaData = m_resultSet->GetMetaData();
	if (resultSetMetaData != nullptr) {

		const int columnFld = m_members.GetPropData(lPropNum);
		const int columnType = resultSetMetaData->GetColumnType(columnFld);
		
		if (columnType == ibResultSetMetaData::COLUMN_NULL)
			pvarPropVal = ibValue::CreateObject(g_valueNullCLSID);
		else if (columnType == ibResultSetMetaData::COLUMN_BOOL)
			pvarPropVal = m_resultSet->GetResultBool(columnFld);
		else if (columnType == ibResultSetMetaData::COLUMN_INTEGER || columnType == ibResultSetMetaData::COLUMN_DOUBLE)
			pvarPropVal = m_resultSet->GetResultNumber(columnFld);
		else if (columnType == ibResultSetMetaData::COLUMN_DATE)
			pvarPropVal = m_resultSet->GetResultDate(columnFld);
		else if (columnType == ibResultSetMetaData::COLUMN_STRING)
			pvarPropVal = m_resultSet->GetResultString(columnFld);

		return true;
	}

	return false;
}

bool ibValueResultSet::CallAsFunc(const long lMethodNum, ibValue& pvarRetValue, ibValue** paParams, const long lSizeArray) //function call
{
	if (m_resultSet != nullptr && lMethodNum == eNext) {
		m_onRow = false;   // a fetch that throws leaves no row either
		m_onRow = m_resultSet->Next();
		pvarRetValue = m_onRow;
		return true;
	}

	return false;
}

bool ibValueResultSet::CallAsProc(const long lMethodNum, ibValue** paParams, const long lSizeArray) //procudre call
{
	return false;
}

//**********************************************************************
//*                       Runtime register                             *
//**********************************************************************

SYSTEM_TYPE_REGISTER(ibValueResultSet, "DatabaseResultSet", system_to_clsid("VL_DBRS"));
