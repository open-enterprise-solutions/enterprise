#include "resultSetMetaData.h"

int ibResultSetMetaData::FindColumnByName(const ibString &colName)
{
	for (int i = 1; i <= GetColumnCount(); i++)
	{
		if (stringUtils::IsSameAs(colName, GetColumnName(i)))
			return i;
	}

	return -1;
}
