#include "databaseLayer.h"
#include "databaseErrorCodes.h"
#include "databaseLayerException.h"

// ctor()
IDatabaseLayer::IDatabaseLayer()
	: CDatabaseErrorReporter()
{
}

// dtor()
IDatabaseLayer::~IDatabaseLayer()
{
	CloseResultSets();
	CloseStatements();
}

#if !wxUSE_UTF8_LOCALE_ONLY
int IDatabaseLayer::DoRunQueryWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQuery(strQuery, true);
}

IDatabaseResultSet* IDatabaseLayer::DoRunQueryWithResultsWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQueryWithResults(strQuery);
}

IPreparedStatement* IDatabaseLayer::DoPrepareStatementWchar(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoPrepareStatement(strQuery);
}
#endif

#if wxUSE_UNICODE_UTF8
int IDatabaseLayer::DoRunQueryUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQuery(strQuery, true);
}

IDatabaseResultSet* IDatabaseLayer::DoRunQueryWithResultsUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoRunQueryWithResults(strQuery);
}

IPreparedStatement* IDatabaseLayer::DoPrepareStatementUtf8(const wxChar* format, ...)
{
	va_list args;
	va_start(args, format);
	wxString strQuery;

	strQuery.PrintfV(format, args);
	va_end(args);

	return DoPrepareStatement(strQuery);
}
#endif

void IDatabaseLayer::CloseResultSets()
{
	// Iterate through all of the result sets and close them all
	DatabaseResultSetHashSet::iterator start = m_ResultSets.begin();
	DatabaseResultSetHashSet::iterator stop = m_ResultSets.end();
	while (start != stop)
	{
		wxLogDebug(_("ResultSet NOT closed and cleaned up by the IDatabaseLayer dtor"));
		delete(*start++);
	}
	m_ResultSets.clear();
}

void IDatabaseLayer::CloseStatements()
{
	// Iterate through all of the statements and close them all
	DatabaseStatementHashSet::iterator start = m_Statements.begin();
	DatabaseStatementHashSet::iterator stop = m_Statements.end();
	while (start != stop)
	{
		wxLogDebug(_("IPreparedStatement NOT closed and cleaned up by the IDatabaseLayer dtor"));
		//delete (*start); start++;
		delete(*start++);
	}
	m_Statements.clear();
}

bool IDatabaseLayer::CloseResultSet(IDatabaseResultSet*& pResultSet)
{
	if (pResultSet != nullptr)
	{
		// Check if we have this result set in our list
		if (m_ResultSets.find(pResultSet) != m_ResultSets.end())
		{
			// Remove the result set pointer from the list and delete the pointer
			m_ResultSets.erase(pResultSet); wxDELETE(pResultSet);
			return true;
		}

		// If not then iterate through all of the statements and see
		//  if any of them have the result set in their lists
		DatabaseStatementHashSet::iterator it;
		for (it = m_Statements.begin(); it != m_Statements.end(); ++it)
		{
			// If the statement knows about the result set then it will close the 
			//  result set and return true, otherwise it will return false
			IPreparedStatement* pStatement = *it;
			if (pStatement != nullptr)
			{
				if (pStatement->CloseResultSet(pResultSet))
				{
					return true;
				}
			}
		}

		// If we don't know about the result set and the statements don't
		//  know about it, the just delete it
		wxDELETE(pResultSet);
		return true;
	}
	else
	{
		// Return false on nullptr pointer
		return false;
	}

}

bool IDatabaseLayer::CloseStatement(IPreparedStatement*& pStatement)
{
	if (pStatement != nullptr)
	{
		// See if we know about this pointer, if so then remove it from the list
		if (m_Statements.find(pStatement) != m_Statements.end()) {
			// Remove the statement pointer from the list and delete the pointer
			m_Statements.erase(pStatement); wxDELETE(pStatement);
			return true;
		}

		// Otherwise just delete it
		wxDELETE(pStatement);
		return true;
	}
	else
	{
		// Return false on nullptr pointer
		return false;
	}
}


int IDatabaseLayer::GetSingleResultInt(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultInt(strSQL, &variant, bRequireUniqueResult);
}

int IDatabaseLayer::GetSingleResultInt(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultInt(strSQL, &variant, bRequireUniqueResult);
}

int IDatabaseLayer::GetSingleResultInt(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	int value = -1;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultInt(field->GetString());
				else
					value = pResult->GetResultInt(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = -1;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

wxString IDatabaseLayer::GetSingleResultString(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultString(strSQL, &variant, bRequireUniqueResult);
}

wxString IDatabaseLayer::GetSingleResultString(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultString(strSQL, &variant, bRequireUniqueResult);
}

wxString IDatabaseLayer::GetSingleResultString(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	wxString value = wxEmptyString;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = wxEmptyString;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultString(field->GetString());
				else
					value = pResult->GetResultString(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = wxEmptyString;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

long IDatabaseLayer::GetSingleResultLong(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultLong(strSQL, &variant, bRequireUniqueResult);
}

long IDatabaseLayer::GetSingleResultLong(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultLong(strSQL, &variant, bRequireUniqueResult);
}

long IDatabaseLayer::GetSingleResultLong(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	long value = -1;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultLong(field->GetString());
				else
					value = pResult->GetResultLong(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = -1;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

bool IDatabaseLayer::GetSingleResultBool(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultBool(strSQL, &variant, bRequireUniqueResult);
}

bool IDatabaseLayer::GetSingleResultBool(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultBool(strSQL, &variant, bRequireUniqueResult);
}

bool IDatabaseLayer::GetSingleResultBool(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	bool value = false;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = false;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultBool(field->GetString());
				else
					value = pResult->GetResultBool(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = false;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

wxDateTime IDatabaseLayer::GetSingleResultDate(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultDate(strSQL, &variant, bRequireUniqueResult);
}

wxDateTime IDatabaseLayer::GetSingleResultDate(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultDate(strSQL, &variant, bRequireUniqueResult);
}

wxDateTime IDatabaseLayer::GetSingleResultDate(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	wxDateTime value = wxDefaultDateTime;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = wxDefaultDateTime;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultDate(field->GetString());
				else
					value = pResult->GetResultDate(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = wxDefaultDateTime;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

void* IDatabaseLayer::GetSingleResultBlob(const wxString& strSQL, int nField, wxMemoryBuffer& buffer, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultBlob(strSQL, &variant, buffer, bRequireUniqueResult);
}

void* IDatabaseLayer::GetSingleResultBlob(const wxString& strSQL, const wxString& strField, wxMemoryBuffer& buffer, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultBlob(strSQL, &variant, buffer, bRequireUniqueResult);
}

void* IDatabaseLayer::GetSingleResultBlob(const wxString& strSQL, const wxVariant* field, wxMemoryBuffer& buffer, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	void* value = nullptr;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = nullptr;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultBlob(field->GetString(), buffer);
				else
					value = pResult->GetResultBlob(field->GetLong(), buffer);
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = nullptr;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

double IDatabaseLayer::GetSingleResultDouble(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultDouble(strSQL, &variant, bRequireUniqueResult);
}

double IDatabaseLayer::GetSingleResultDouble(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultDouble(strSQL, &variant, bRequireUniqueResult);
}

double IDatabaseLayer::GetSingleResultDouble(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	double value = -1;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultDouble(field->GetString());
				else
					value = pResult->GetResultDouble(field->GetLong());
				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = -1;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

number_t IDatabaseLayer::GetSingleResultNumber(const wxString& strSQL, int nField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant((long)nField);
	return GetSingleResultNumber(strSQL, &variant, bRequireUniqueResult);
}

number_t IDatabaseLayer::GetSingleResultNumber(const wxString& strSQL, const wxString& strField, bool bRequireUniqueResult /*= true*/)
{
	wxVariant variant(strField);
	return GetSingleResultNumber(strSQL, &variant, bRequireUniqueResult);
}

number_t IDatabaseLayer::GetSingleResultNumber(const wxString& strSQL, const wxVariant* field, bool bRequireUniqueResult /*= true*/)
{
	bool valueRetrievedFlag = false;
	number_t value = -1;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (valueRetrievedFlag)
			{
				// Close the result set, reset the value and throw an exception
				CloseResultSet(pResult);
				pResult = nullptr;
				value = -1;
				SetErrorCode(DATABASE_LAYER_NON_UNIQUE_RESULTSET);
				SetErrorMessage(wxT("A non-unique result was returned."));
				ThrowDatabaseException();
				return value;
			}
			else
			{
				if (field->IsType(_("string")))
					value = pResult->GetResultNumber(field->GetString());
				else
					value = pResult->GetResultNumber(field->GetLong());

				valueRetrievedFlag = true;

				// If the user isn't concerned about returning a unique result,
				//  then just exit after the first record is found
				if (!bRequireUniqueResult)
					break;
			}
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	// Make sure that a value was retrieved from the database
	if (!valueRetrievedFlag)
	{
		value = -1;
		SetErrorCode(DATABASE_LAYER_NO_ROWS_FOUND);
		SetErrorMessage(wxT("No result was returned."));
		ThrowDatabaseException();
		return value;
	}

	return value;
}

wxArrayInt IDatabaseLayer::GetResultsArrayInt(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayInt(strSQL, &variant);
}

wxArrayInt IDatabaseLayer::GetResultsArrayInt(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayInt(strSQL, &variant);
}

wxArrayInt IDatabaseLayer::GetResultsArrayInt(const wxString& strSQL, const wxVariant* field)
{
	wxArrayInt returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(_("string")))
				returnArray.Add(pResult->GetResultInt(field->GetString()));
			else
				returnArray.Add(pResult->GetResultInt(field->GetLong()));
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

wxArrayString IDatabaseLayer::GetResultsArrayString(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayString(strSQL, &variant);
}

wxArrayString IDatabaseLayer::GetResultsArrayString(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayString(strSQL, &variant);
}

wxArrayString IDatabaseLayer::GetResultsArrayString(const wxString& strSQL, const wxVariant* field)
{
	wxArrayString returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(_("string")))
				returnArray.Add(pResult->GetResultString(field->GetString()));
			else
				returnArray.Add(pResult->GetResultString(field->GetLong()));
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

wxArrayLong IDatabaseLayer::GetResultsArrayLong(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayLong(strSQL, &variant);
}

wxArrayLong IDatabaseLayer::GetResultsArrayLong(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayLong(strSQL, &variant);
}

wxArrayLong IDatabaseLayer::GetResultsArrayLong(const wxString& strSQL, const wxVariant* field)
{
	wxArrayLong returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(_("string")))
				returnArray.Add(pResult->GetResultLong(field->GetString()));
			else
				returnArray.Add(pResult->GetResultLong(field->GetLong()));
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}

#if wxCHECK_VERSION(2, 7, 0)
wxArrayDouble IDatabaseLayer::GetResultsArrayDouble(const wxString& strSQL, int nField)
{
	wxVariant variant((long)nField);
	return GetResultsArrayDouble(strSQL, &variant);
}

wxArrayDouble IDatabaseLayer::GetResultsArrayDouble(const wxString& strSQL, const wxString& strField)
{
	wxVariant variant(strField);
	return GetResultsArrayDouble(strSQL, &variant);
}

wxArrayDouble IDatabaseLayer::GetResultsArrayDouble(const wxString& strSQL, const wxVariant* field)
{
	wxArrayDouble returnArray;

	IDatabaseResultSet* pResult = nullptr;
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	try
	{
#endif
		pResult = ExecuteQuery(strSQL);

		while (pResult->Next())
		{
			if (field->IsType(_("string")))
				returnArray.Add(pResult->GetResultDouble(field->GetString()));
			else
				returnArray.Add(pResult->GetResultDouble(field->GetLong()));
		}
#if _USE_DATABASE_LAYER_EXCEPTIONS == 1
	}
	catch (DatabaseLayerException& e)
	{
		if (pResult != nullptr)
		{
			CloseResultSet(pResult);
			pResult = nullptr;
		}

		throw e;
	}
#endif

	if (pResult != nullptr)
	{
		CloseResultSet(pResult);
		pResult = nullptr;
	}

	return returnArray;
}
#endif

