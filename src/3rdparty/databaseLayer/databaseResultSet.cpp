#include "databaseResultSet.h"
#include "resultSetMetaData.h"

// ctor()
DatabaseResultSet::DatabaseResultSet()
	: DatabaseErrorReporter()
{
}

// dtor()
DatabaseResultSet::~DatabaseResultSet()
{
	//wxPrintf(_("~DatabaseResultSet()\n"));
	CloseMetaData();
}

int DatabaseResultSet::GetResultInt(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultInt(nIndex);
	}
	return -1;
}

wxString DatabaseResultSet::GetResultString(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultString(nIndex);
	}
	return wxEmptyString;
}

long long DatabaseResultSet::GetResultLong(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultLong(nIndex);
	}
	return -1;
}

bool DatabaseResultSet::GetResultBool(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultBool(nIndex);
	}
	return false;
}

wxDateTime DatabaseResultSet::GetResultDate(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultDate(nIndex);
	}
	
	return wxDefaultDateTime;
}

void* DatabaseResultSet::GetResultBlob(const wxString& strField, wxMemoryBuffer& buffer)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultBlob(nIndex, buffer);
	}
	return NULL;
}

double DatabaseResultSet::GetResultDouble(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultDouble(nIndex);
	}
	return -1;
}

number_t DatabaseResultSet::GetResultNumber(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultNumber(nIndex);
	}
	return -1;
}

bool DatabaseResultSet::IsFieldNull(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return IsFieldNull(nIndex);
	}
	return true;
}

void DatabaseResultSet::CloseMetaData()
{
	// Iterate through all of the meta data and close them all
	MetaDataHashSet::iterator start = m_MetaData.begin();
	MetaDataHashSet::iterator stop = m_MetaData.end();
	while (start != stop)
	{
		//delete (*start); start++;
		delete(*start++);
	}
	m_MetaData.clear();
}

bool DatabaseResultSet::CloseMetaData(ResultSetMetaData* pMetaData)
{
	if (pMetaData != NULL)
	{
		// Check if we have this meta data in our list
		if (m_MetaData.find(pMetaData) != m_MetaData.end())
		{
			// Remove the meta data pointer from the list and delete the pointer
			delete pMetaData;
			m_MetaData.erase(pMetaData);
			return true;
		}

		// Delete the pointer
		delete pMetaData;
		return true;
	}
	else
	{
		// Return false on NULL pointer
		return false;
	}
}

