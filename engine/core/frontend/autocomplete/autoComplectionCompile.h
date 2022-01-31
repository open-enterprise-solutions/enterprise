#ifndef _AUTOCOMPLETE_COMPILE_H__
#define _AUTOCOMPLETE_COMPILE_H__

#include "common/moduleInfo.h"
#include "compiler/compilemodule.h"

class CPrecompileModule;
struct CPrecompileFunction;

struct SParamValue
{
	wxString sName;//имя переменной 
	wxString sType;//тип переменной в англ. нотации (в случае явной типизации)

	CValue vObject;
};

struct CPrecompileVariable
{
	bool bExport;
	bool bContext;
	bool bTempVar;

	int nNumber;

	wxString sName;//Имя переменной
	wxString sType;//тип значения
	wxString sRealName;

	CValue vContext;
	CValue vObject;

	CPrecompileVariable() : bExport(false), bContext(false), bTempVar(false), nNumber(0) {};
	CPrecompileVariable(wxString csVarName) : sName(csVarName), bExport(false), bContext(false), bTempVar(false), nNumber(0) {};
};

struct CPrecompileContext
{
	CPrecompileModule *pModule;
	void SetModule(CPrecompileModule *pSetModule) { pModule = pSetModule; };

	CPrecompileContext *pParent;//родительский контекст
	CPrecompileContext *pStopParent;//начало запрещенной области прародителя
	CPrecompileContext *pContinueParent;//начало разрешенной области прародителя
	bool bStaticVariable;		//все переменные статичные

	//ПЕРЕМЕННЫЕ
	std::map <wxString, CPrecompileVariable> cVariables;

	SParamValue GetVariable(const wxString &sName, bool bFindInParent = true, bool bCheckError = false, CValue Value = CValue());
	SParamValue AddVariable(const wxString &sName, const wxString &sType = wxEmptyString, bool bExport = false, bool bTempVar = false, CValue Value = CValue());

	bool FindVariable(const wxString &sName, CValue &vContext = CValue(), bool bContext = false);
	bool FindFunction(const wxString &sName, CValue &vContext = CValue(), bool bContext = false);

	void RemoveVariable(const wxString &sName);

	int nTempVar;//номер текущей временной переменной
	int nFindLocalInParent;//признак поиска переменных в родителе (на один уровень), в остальных случаях в родителях ищутся только экспортные переменные)

	//ФУНКЦИИ И ПРОЦЕДУРЫ
	std::map<wxString, CPrecompileFunction *> cFunctions;//список встретившихся определений функций
	int nReturn;//режим обработки оператора RETURN : RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION
	wxString sCurFuncName;//имя текущей компилируемой функции (для обработки варианта вызова рекурсивной функции)

	CPrecompileContext(CPrecompileContext *hSetParent = NULL)
	{
		pParent = hSetParent;

		nReturn = 0;
		nFindLocalInParent = 1;
		pModule = NULL;

		pStopParent = NULL;
		pContinueParent = NULL;

		if (hSetParent)
		{
			pStopParent = hSetParent->pStopParent;
			pContinueParent = hSetParent->pContinueParent;
		}

		nTempVar = 0;
		bStaticVariable = false;

	};

	~CPrecompileContext();
};

//определение функции
struct CPrecompileFunction
{
	wxString sRealName;//Имя функции
	wxString sName;//Имя функции в верхнем регистре
	std::vector<SParamValue> aParamList;
	bool bExport;
	bool bContext;
	CPrecompileContext *m_pContext;//конекст компиляции
	int nVarCount;//число локальных переменных
	int nStart;//стартовая позиция в массиве байт-кодов
	int nFinish;//конечная позиция в массиве байт-кодов

	CValue vContext;

	SParamValue RealRetValue;//для хранения переменной при реальном вызове
	bool bSysFunction;
	wxString sType;		//тип (в англ. нотации), если это типизированная функция

	//для IntelliSense
	int	nNumberLine;	//номер строки исходного текста (для точек останова)
	wxString sShortDescription;//включает в себя всю строку после ключевого слова Функция(Процедура)
	wxString sLongDescription;//включает в себя весь слитный (т.е.е буз пустых строк) блок комментарий до определения функции (процедуры)

	CPrecompileFunction(const wxString &sFuncName, CPrecompileContext *pSetContext = NULL)
	{
		sName = sFuncName;
		m_pContext = pSetContext;
		bExport = false;
		bContext = false;
		nVarCount = 0;
		nStart = 0;
		nFinish = 0;
		bSysFunction = false;
		nNumberLine = -1;
	};

	~CPrecompileFunction()
	{
		if (m_pContext)//Удаляем подчиненный контекст (у каждой функции свой список меток и локальных переменных)
			delete m_pContext;
	};
};

//*******************************************************************
//*                         Класс: пре-компилятор                   *
//*******************************************************************
class CPrecompileModule : public CTranslateModule
{
	int m_nCurrentCompile;		//текущее положение в массиве лексем

	CMetaModuleObject *m_moduleObject;

	std::map<wxString, unsigned int> m_aHashConstList;

	CPrecompileContext	cContext;
	CPrecompileContext	*m_pContext;
	CPrecompileContext  *m_pCurrentContext;

	CValue vObject;

	unsigned int nLastPosition;

	wxString sLastExpression;
	wxString sLastKeyword;
	wxString sLastParentKeyword;

	bool m_bCalcValue;

	unsigned int m_nCurrentPos;

	friend class CAutocomplectionCtrl;

public:

	//Основные методы:
	void Clear();//Сброс данных для повторного использования объекта
	void PrepareModuleData();

	CPrecompileModule(CMetaModuleObject *moduleObject);
	virtual ~CPrecompileModule();

	CValue GetComputeValue() { return vObject; }
	
	CPrecompileContext *GetContext()
	{
		cContext.SetModule(this);
		return &cContext;
	};

	CPrecompileContext *GetCurrentContext() const { return m_pCurrentContext; }

	bool Compile();

	bool PrepareLexem(); 
	void PatchLexem(unsigned int line, int offsetLine, unsigned int offsetString, unsigned int modFlags);

protected:

	bool CompileFunction();
	bool CompileDeclaration();

	bool CompileBlock();

	bool CompileNewObject();
	bool CompileGoto();
	bool CompileIf();
	bool CompileWhile();
	bool CompileFor();
	bool CompileForeach();

protected:

	bool CompileModule();

	CLexem PreviewGetLexem();
	CLexem GetLexem();
	CLexem GETLexem();
	void GETDelimeter(char c);

	bool IsNextDelimeter(char c);
	bool IsNextKeyWord(int nKey);
	void GETKeyWord(int nKey);
	wxString GETIdentifier(bool realName = false);
	CValue GETConstant();
	int GetConstString(const wxString &sMethod);

	int IsTypeVar(const wxString &sType = wxEmptyString);
	wxString GetTypeVar(const wxString &sType = wxEmptyString);

	SParamValue GetExpression(int nPriority = 0);

	SParamValue GetCurrentIdentifier(int &nIsSet);
	SParamValue GetCallFunction(const wxString &sName);

	void AddVariable(const wxString &sName, CValue Value);

	SParamValue GetVariable(const wxString &sName, bool bCheckError = false);
	SParamValue GetVariable();

	SParamValue FindConst(CValue &vData);
};

#endif 