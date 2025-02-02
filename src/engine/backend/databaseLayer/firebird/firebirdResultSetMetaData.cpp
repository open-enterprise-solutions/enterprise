#include "firebirdResultSetMetaData.h"

// ctor
CFirebirdResultSetMetaData::CFirebirdResultSetMetaData(XSQLDA* pFields)
{
	m_pFields = pFields;
}

int CFirebirdResultSetMetaData::GetColumnType(int i)
{
	int returnType = COLUMN_UNKNOWN;
	XSQLVAR* pVar = GetVariable(i);
	if (pVar)
	{
		short nType = pVar->sqltype & ~1;
		switch (nType)
		{
		case SQL_SHORT:
		case SQL_INT64:
		case SQL_INT128:
		case SQL_LONG:
			if (pVar->sqlscale == 0)
				returnType = COLUMN_INTEGER;
			else
				returnType = COLUMN_DOUBLE;
			break;
		case SQL_TEXT:
		case SQL_VARYING:
			returnType = COLUMN_STRING;
			break;
		case SQL_FLOAT:
		case SQL_DOUBLE:
			returnType = COLUMN_DOUBLE;
			break;
		case SQL_BLOB:
			returnType = COLUMN_BLOB;
			break;
		case SQL_TYPE_DATE:
		case SQL_TYPE_TIME:
		case SQL_TIMESTAMP:
			returnType = COLUMN_DATE;
			break;
		default:
			returnType = COLUMN_UNKNOWN;
			break;
		};
	}
	return returnType;
}

int CFirebirdResultSetMetaData::GetColumnSize(int i)
{
	XSQLVAR* pVar = GetVariable(i);
	if (pVar)
	{
		return pVar->sqllen;
	}
	return -1;
}

wxString CFirebirdResultSetMetaData::GetColumnName(int i)
{
	XSQLVAR* pVar = GetVariable(i);
	if (pVar)
	{
		wxString columnName = ConvertFromUnicodeStream(pVar->synonymname);
		return columnName;
	}
	return wxEmptyString;
}

int CFirebirdResultSetMetaData::GetColumnCount()
{
	return m_pFields->sqln;
}

XSQLVAR* CFirebirdResultSetMetaData::GetVariable(int nField)
{
	XSQLVAR* pVar = &(m_pFields->sqlvar[nField - 1]);
	return pVar;
}

