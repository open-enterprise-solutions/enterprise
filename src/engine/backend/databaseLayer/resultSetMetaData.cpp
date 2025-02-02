#include "resultSetMetaData.h"


int IResultSetMetaData::FindColumnByName(const wxString &colName)
{
	for (int i = 1; i <= GetColumnCount(); i++)
	{
		if (stringUtils::CompareString(colName, GetColumnName(i)))
			return i;
	}

	return wxNOT_FOUND;
}
