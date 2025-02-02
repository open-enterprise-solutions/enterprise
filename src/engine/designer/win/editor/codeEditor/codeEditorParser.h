#ifndef _AUTOCOMPLETE_PARSER_H__CModuleElementInfo
#define _AUTOCOMPLETE_PARSER_H__

#include "codeEditor.h"

enum eContentType
{
	eVariable = 0,
	eExportVariable,
	eProcedure,
	eExportProcedure,
	eFunction,
	eExportFunction,

	eEmpty
};

struct moduleElement_t
{
	moduleElement_t() : 
		eType(eEmpty), nImage(0), nLineStart(-1), nLineEnd(-1) {};

	wxString strName;//имя элемента
	wxString strShortDescription;//тип объекта

	int nImage;//номер картинки
	int nLineStart;//номер строки кода, где находится элемент
	int nLineEnd;//номер строки кода, где находится элемент

	wxString sModuleName;//Имя модуля
	eContentType eType;
};

class CParserModule : public CTranslateCode
{
	int m_nCurrentCompile;//текущее положение в массиве лексем
	std::vector<moduleElement_t> m_aContentModule;

protected:

	lexem_t PreviewGetLexem();
	lexem_t GetLexem();
	lexem_t GETLexem();
	void GETDelimeter(const wxUniChar &c);

	bool IsNextDelimeter(const wxUniChar &c);
	bool IsNextKeyWord(int nKey);
	void GETKeyWord(int nKey);
	wxString GETIdentifier(bool strRealName = false);
	CValue GETConstant();

public:

	CParserModule();
	bool ParseModule(const wxString &sModule);

	//all data
	std::vector<moduleElement_t> &GetAllContent() { return m_aContentModule; }
	//variables
	wxArrayString GetVariables(bool bOnlyExport = true);
	//functions & procedures 
	wxArrayString GetFunctions(bool bOnlyExport = true);
	wxArrayString GetProcedures(bool bOnlyExport = true);
};

#endif 