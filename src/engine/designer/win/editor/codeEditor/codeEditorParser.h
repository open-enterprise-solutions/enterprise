#ifndef _AUTOCOMPLETE_PARSER_H__ibModuleElementInfo
#define _AUTOCOMPLETE_PARSER_H__

#include "codeEditor.h"

enum ibContentType
{
	eVariable = 0,
	eExportVariable,
	eProcedure,
	eExportProcedure,
	eFunction,
	eExportFunction,

	eEmpty
};

struct ibModuleElement
{
	ibModuleElement() : 
		eType(eEmpty), nImage(0), nLineStart(-1), nLineEnd(-1) {};

	wxString strName;//имя элемента
	wxString strShortDescription;//тип объекта

	int nImage;//номер картинки
	int nLineStart;//номер строки кода, где находится элемент
	int nLineEnd;//номер строки кода, где находится элемент

	wxString sModuleName;//Имя модуля
	ibContentType eType;
};

class ibParserModule : public ibTranslateCode
{
	int m_numCurrentCompile;//текущее положение в массиве лексем
	std::vector<ibModuleElement> m_aContentModule;

protected:

	ibLexem PreviewGetLexem();
	ibLexem GetLexem();
	ibLexem GETLexem();
	void GETDelimeter(const wxUniChar &c);

	bool IsNextDelimeter(const wxUniChar &c);
	bool IsNextKeyWord(int nKey);
	void GETKeyWord(int nKey);
	wxString GETIdentifier(bool strRealName = false);
	ibValue GETConstant();

public:

	ibParserModule();
	bool ParseModule(const wxString &sModule);

	//all data
	std::vector<ibModuleElement> &GetAllContent() { return m_aContentModule; }
	//variables
	wxArrayString GetVariables(bool bOnlyExport = true);
	//functions & procedures 
	wxArrayString GetFunctions(bool bOnlyExport = true);
	wxArrayString GetProcedures(bool bOnlyExport = true);
};

#endif 