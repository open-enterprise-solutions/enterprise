#include "databaseResultSet.h"
#include "resultSetMetaData.h"

// ctor()
IDatabaseResultSet::IDatabaseResultSet()
	: CDatabaseErrorReporter()
{
}

// dtor()
IDatabaseResultSet::~IDatabaseResultSet()
{
	//wxPrintf(_("~IDatabaseResultSet()\n"));
	CloseMetaData();
}

int IDatabaseResultSet::GetResultInt(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultInt(nIndex);
	}
	return -1;
}

wxString IDatabaseResultSet::GetResultString(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultString(nIndex);
	}
	return wxEmptyString;
}

long long IDatabaseResultSet::GetResultLong(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultLong(nIndex);
	}
	return -1;
}

bool IDatabaseResultSet::GetResultBool(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultBool(nIndex);
	}
	return false;
}

wxDateTime IDatabaseResultSet::GetResultDate(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultDate(nIndex);
	}
	
	return wxDefaultDateTime;
}

void* IDatabaseResultSet::GetResultBlob(const wxString& strField, wxMemoryBuffer& buffer)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultBlob(nIndex, buffer);
	}
	return nullptr;
}

double IDatabaseResultSet::GetResultDouble(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultDouble(nIndex);
	}
	return -1;
}

number_t IDatabaseResultSet::GetResultNumber(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultNumber(nIndex);
	}
	return -1;
}

bool IDatabaseResultSet::IsFieldNull(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return IsFieldNull(nIndex);
	}
	return true;
}

void IDatabaseResultSet::CloseMetaData()
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

bool IDatabaseResultSet::CloseMetaData(IResultSetMetaData* pMetaData)
{
	if (pMetaData != nullptr)
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
		// Return false on nullptr pointer
		return false;
	}
}

