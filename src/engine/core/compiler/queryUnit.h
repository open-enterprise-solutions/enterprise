#ifndef _QUERYUNIT_H__
#define _QUERYUNIT_H__

#include "core/compiler/value.h"

class CQueryUnit
{
	struct queryLexem_t {

		//тип лексемы:
		short       m_nType;

		//содержание лексемы:
		short	    m_nData;		//номер служебного слова(KEYWORD) или символ разделитель(DELIMITER)
		CValue	    m_vData;		//значение, если это константа или реальное им€ идентификатора
		wxString	m_sData;		//или им€ идентификатора (переменной, функции и пр.)

		unsigned int m_nNumberLine;	  //номер строки исходного текста (дл€ точек останова)
		unsigned int m_nNumberString;	  //номер исходного текста (дл€ вывода ошибок)

	public:

		// онструктор: 
		queryLexem_t() :
			m_nType(0),
			m_nData(0),
			m_nNumberString(0),
			m_nNumberLine(0) {
		}
	};


	wxString queryText;
	std::map<wxString, CValue> paParams;

public:

	CQueryUnit();
	CQueryUnit(const wxString& queryText);

	void SetQueryText(const wxString& queryText);
	wxString GetQueryText();

	void SetQueryParam(const wxString& sParamName, CValue cParam);
	CValue GetQueryParam(const wxString& sParamName);

	void Execute();

protected:

	void Reset();
};

#endif