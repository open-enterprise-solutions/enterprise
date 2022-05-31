#include "resultSetMetaData.h"
#include "utils/stringUtils.h"

int ResultSetMetaData::FindColumnByName(const wxString &colName)
{
	for (int i = 1; i <= GetColumnCount(); i++)
	{
		if (StringUtils::CompareString(colName, GetColumnName(i)))
			return i;
	}

	return wxNOT_FOUND;
}
