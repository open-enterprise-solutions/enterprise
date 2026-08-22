#include "databaseResultSet.h"
#include "resultSetMetaData.h"

// ctor()
ibDatabaseResultSet::ibDatabaseResultSet()
	: ibDatabaseErrorReporter()
{
}

// dtor()
ibDatabaseResultSet::~ibDatabaseResultSet()
{
	//wxPrintf(_("~ibDatabaseResultSet()\n"));
	CloseMetaData();

	// Whoever was keeping books on this one stops now — see the note on the declaration.
	if (m_owner != nullptr)
		m_owner->ForgetResultSet(this);
}

int ibDatabaseResultSet::GetResultInt(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultInt(nIndex);
	}
	return -1;
}

wxString ibDatabaseResultSet::GetResultString(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultString(nIndex);
	}
	return wxEmptyString;
}

long long ibDatabaseResultSet::GetResultLong(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultLong(nIndex);
	}
	return -1;
}

bool ibDatabaseResultSet::GetResultBool(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultBool(nIndex);
	}
	return false;
}

wxDateTime ibDatabaseResultSet::GetResultDate(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultDate(nIndex);
	}
	
	return wxDefaultDateTime;
}

void* ibDatabaseResultSet::GetResultBlob(const wxString& strField, wxMemoryBuffer& buffer)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultBlob(nIndex, buffer);
	}
	return nullptr;
}

double ibDatabaseResultSet::GetResultDouble(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultDouble(nIndex);
	}
	return -1;
}

ibNumber ibDatabaseResultSet::GetResultNumber(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return GetResultNumber(nIndex);
	}
	return -1;
}

bool ibDatabaseResultSet::IsFieldNull(const wxString& strField)
{
	int nIndex = LookupField(strField);
	if (nIndex != -1)
	{
		return IsFieldNull(nIndex);
	}
	return true;
}

void ibDatabaseResultSet::CloseMetaData()
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

bool ibDatabaseResultSet::CloseMetaData(ibResultSetMetaData* pMetaData)
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

