#ifndef _QUERYUNIT_H__
#define _QUERYUNIT_H__

#include "compiler/value.h"

class CQueryUnit
{
	wxString queryText; 
	std::map<wxString, CValue> aParams;

public:

	CQueryUnit(); 
	CQueryUnit(const wxString &queryText);

	void SetQueryText(const wxString &queryText);
	wxString GetQueryText(); 

	void SetQueryParam(const wxString &sParamName, CValue cParam);
	CValue GetQueryParam(const wxString &sParamName);

	void Execute(); 

protected:

	void Reset();

};

#endif