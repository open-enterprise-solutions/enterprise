#ifndef _AUTOCOMPLETE_COMPILE_H__
#define _AUTOCOMPLETE_COMPILE_H__

#include "backend/wrapper/moduleInfo.h"
#include "backend/compiler/compileCode.h"

struct CParamValue {
	wxString m_paramName;//имя переменной 
	wxString m_paramType;//тип переменной в англ. нотации (в случае явной типизации)
	CValue m_paramObject;
};

class CPrecompileModule;
struct CPrecompileFunction;

struct CPrecompileVariable
{
	bool bExport;
	bool bContext;
	bool bTempVar;

	int nNumber;

	wxString strName;//Имя переменной
	wxString strType;//тип значения
	wxString strRealName;

	CValue m_valContext;
	CValue m_valObject;

	CPrecompileVariable() : bExport(false), bContext(false), bTempVar(false), nNumber(0) {};
	CPrecompileVariable(wxString csVarName) : strName(csVarName), bExport(false), bContext(false), bTempVar(false), nNumber(0) {};
};

struct CPrecompileContext
{
	CPrecompileModule* pModule;
	void SetModule(CPrecompileModule* pSetModule) { pModule = pSetModule; }

	CPrecompileContext* pParent;//родительский контекст
	CPrecompileContext* pStopParent;//начало запрещенной области прародителя
	CPrecompileContext* pContinueParent;//начало разрешенной области прародителя
	bool bStaticVariable;		//все переменные статичные

	//ПЕРЕМЕННЫЕ
	std::map <wxString, CPrecompileVariable> cVariables;

	CParamValue GetVariable(const wxString& strVarName, bool bFindInParent = true, bool bCheckError = false, const CValue& valVar = CValue());
	CParamValue AddVariable(const wxString& strVarName, const wxString& varType = wxEmptyString, bool bExport = false, bool bTempVar = false, const CValue& valVar = CValue());
	void SetVariable(const wxString& strVarName, const CValue& valVar);

	bool FindVariable(const wxString& strName, CValue& valContext = CValue(), bool bContext = false);
	bool FindFunction(const wxString& strName, CValue& valContext = CValue(), bool bContext = false);

	void RemoveVariable(const wxString& strName);

	int nTempVar;//номер текущей временной переменной
	int nFindLocalInParent;//признак поиска переменных в родителе (на один уровень), в остальных случаях в родителях ищутся только экспортные переменные)

	//ФУНКЦИИ И ПРОЦЕДУРЫ
	std::map<wxString, CPrecompileFunction*> cFunctions;//список встретившихся определений функций
	int nReturn;//режим обработки оператора RETURN : RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION
	wxString sCurFuncName;//имя текущей компилируемой функции (для обработки варианта вызова рекурсивной функции)

	CPrecompileContext(CPrecompileContext* hSetParent = nullptr) {
		pParent = hSetParent;

		nReturn = 0;
		nFindLocalInParent = 1;
		pModule = nullptr;

		pStopParent = nullptr;
		pContinueParent = nullptr;

		if (hSetParent) {
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
	wxString strRealName;//Имя функции
	wxString strName;//Имя функции в верхнем регистре
	std::vector<CParamValue> aParamList;
	bool bExport;
	bool bContext;
	CPrecompileContext* m_pContext;//конекст компиляции
	int nVarCount;//число локальных переменных
	int nStart;//стартовая позиция в массиве байт-кодов
	int nFinish;//конечная позиция в массиве байт-кодов

	CValue m_valContext;

	CParamValue RealRetValue;//для хранения переменной при реальном вызове
	bool bSysFunction;
	wxString strType;		//тип (в англ. нотации), если это типизированная функция

	//для IntelliSense
	int	nNumberLine;	//номер строки исходного текста (для точек останова)
	wxString strShortDescription;//включает в себя всю строку после ключевого слова Функция(Процедура)
	wxString sLongDescription;//включает в себя весь слитный (т.е.е буз пустых строк) блок комментарий до определения функции (процедуры)

	CPrecompileFunction(const wxString& strFuncName, CPrecompileContext* pSetContext = nullptr)
	{
		strName = strFuncName;
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
class CPrecompileModule : public CTranslateCode
{
	int m_nCurrentCompile;		//текущее положение в массиве лексем

	CMetaObjectModule* m_moduleObject;

	std::map<wxString, unsigned int> m_aHashConstList;

	CPrecompileContext	cContext;
	CPrecompileContext* m_pContext;
	CPrecompileContext* m_pCurrentContext;

	CValue m_valObject;

	unsigned int nLastPosition;

	wxString sLastExpression;
	wxString sLastKeyword;
	wxString sLastParentKeyword;

	bool m_bCalcValue;

	unsigned int m_nCurrentPos;

	friend class CCodeEditor;

public:

	//Основные методы:
	void Clear();//Сброс данных для повторного использования объекта
	void PrepareModuleData();

	CPrecompileModule(CMetaObjectModule* moduleObject);
	virtual ~CPrecompileModule();

	CValue GetComputeValue() const {
		return m_valObject;
	}

	CPrecompileContext* GetContext() {
		cContext.SetModule(this);
		return &cContext;
	};

	CPrecompileContext* GetCurrentContext() const {
		return m_pCurrentContext;
	}

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

	lexem_t PreviewGetLexem();
	lexem_t GetLexem();
	lexem_t GETLexem();
	void GETDelimeter(const wxUniChar &c);

	bool IsNextDelimeter(const wxUniChar &c);
	bool IsNextKeyWord(int nKey);
	void GETKeyWord(int nKey);
	wxString GETIdentifier(bool strRealName = false);
	CValue GETConstant();
	int GetConstString(const wxString& sMethod);

	int IsTypeVar(const wxString& strType = wxEmptyString);
	wxString GetTypeVar(const wxString& strType = wxEmptyString);

	CParamValue GetExpression(int nPriority = 0);

	CParamValue GetCurrentIdentifier(int& nIsSet);
	CParamValue GetCallFunction(const wxString& strName);

	void AddVariable(const wxString& strVarName, const CValue& varVal);

	CParamValue GetVariable(const wxString& strVarName, bool bCheckError = false);
	CParamValue GetVariable();

	void SetVariable(const wxString& strVarName, const CValue& varVal);

	CParamValue FindConst(CValue& vData);
};

#endif 