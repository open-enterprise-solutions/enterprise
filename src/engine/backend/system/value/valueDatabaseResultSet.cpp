#include "valueDatabase.h"
#include "backend/databaseLayer/databaseLayer.h"
#include "backend/appData.h"
#include "backend/session/session.h"


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
	helper.AppendFunc(wxT("Next"), wxT("Next()"));

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
		pvarRetValue = m_resultSet->Next();
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
