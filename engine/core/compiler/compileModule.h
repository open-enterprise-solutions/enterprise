#ifndef _COMPILEMODULE_H__
#define _COMPILEMODULE_H__

#include "translateModule.h"

typedef std::vector<CValue> CDefValueList;
typedef std::vector<bool> CDefBoolList;
typedef std::vector<int> CDefIntList;

struct SParam
{
	wxLongLong_t m_nArray;
	wxLongLong_t m_nIndex;

	wxString m_sName;//имя переменной 
	wxString m_sType;//тип переменной в англ. нотации (в случае явной типизации)

	SParam() : m_nArray(0), m_nIndex(0) {}
};

struct CVariable
{
	bool m_bExport;
	bool m_bContext;
	bool m_bTempVar;

	unsigned int m_nNumber;

	wxString m_sName;       //Имя переменной
	wxString m_sType;       //тип значени
	wxString m_sContextVar; //имя контекстной переменной
	wxString m_sRealName;   //Имя переменной реальное

	CVariable() : m_bExport(false), m_bContext(false), m_bTempVar(false), m_nNumber(0) {};
	CVariable(const wxString &sVariableName) : m_sName(sVariableName), m_bExport(false), m_bContext(false), m_bTempVar(false), m_nNumber(0) {};
};

struct CFunction;

//хранение одного шага программы
struct CByte
{
	short m_nOper;                //код инструкции
	unsigned int m_nNumberString;	//номер исходного текста (для вывода ошибок)
	unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

	//параметры для инструкции:
	SParam m_param1;
	SParam m_param2;
	SParam m_param3;
	SParam m_param4; // - используется для оптимизации

	wxString m_sModuleName; //имя модуля (т.к. возможны include подключения из разных модулей)
	wxString m_sDocPath; // уникальный путь к документу 
	wxString m_sFileName; // путь к файлу (если внешняя обработка) 

	CByte() : m_param1(), m_param2(), m_param3(), m_param4(),
		m_nOper(0), m_nNumberString(0), m_nNumberLine(0) {}
};

class CCompileModule;

//класс БАЙТ-КОДА
struct CByteCode
{
	//Атрибуты:
	bool m_bCompile;		//признак успешной компиляции

	std::vector <CByte>	m_aCodeList;//исполняемый код модуля
	std::vector <CValue> m_aConstList;//список констант модуля

	std::map<wxString, unsigned int> m_aVarList; //список переменных модуля
	std::map<wxString, unsigned int> m_aFuncList; //список функций модуля

	std::map<wxString, unsigned int> m_aExportVarList; //список экспортных переменных модуля
	std::map<wxString, unsigned int> m_aExportFuncList; //список экспортных функций модуля

	unsigned int m_nVarCount;		//количество локальных переменных в модуле
	unsigned int m_nStartModule;	//начало позиции старта модуля

	wxString m_sModuleName;//имя исполняемого модуля, которому принадлежит байт-код

	CByteCode *m_pParent;	//Родительский байт-код (для проверки)
	CCompileModule *m_pModule;

	//список внешних и контекстных переменных
	std::vector<CValue *> m_aExternValues;

	CByteCode() { 
		Reset();
	};

	void SetModule(CCompileModule *pSetModule) { m_pModule = pSetModule; };

	void Reset()
	{
		m_nStartModule = 0;
		m_nVarCount = 0;

		m_sModuleName = wxEmptyString;

		m_bCompile = false;

		m_pParent = NULL;
		m_pModule = NULL;

		m_aCodeList.clear();
		m_aConstList.clear();

		m_aVarList.clear();
		m_aFuncList.clear();
		m_aExportVarList.clear();
		m_aExportFuncList.clear();

		m_aExternValues.clear();
	}
};

struct SLabel
{
	wxString m_sName;
	int		m_nLine;
	int		m_nError;
};

class CCompileModule;

//*******************************************************************
//*                Класс: контекст компиляции                       *
//*******************************************************************
struct CCompileContext
{
	CCompileModule *m_compileModule;
	CCompileContext *m_parentContext;//родительский контекст

	CFunction *m_functionContext;

	//ПЕРЕМЕННЫЕ
	std::map <wxString, CVariable> m_cVariables;

	int m_nTempVar;//номер текущей временной переменной
	int m_nFindLocalInParent;//признак поиска переменных в родителе (на один уровень), в остальных случаях в родителях ищутся только экспортные переменные)

	//ФУНКЦИИ И ПРОЦЕДУРЫ
	std::map<wxString, CFunction *>	m_cFunctions;//список встретившихся определений функций

	short m_nReturn;//режим обработки оператора RETURN : RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION
	wxString m_sCurFuncName;//имя текущей компилируемой функции (для обработки варианта вызова рекурсивной функции)

	//ЦИКЛЫ
	//Служебные атрибуты
	unsigned short m_nDoNumber;//номер вложенного цикла

	std::map<unsigned short, CDefIntList *> aContinueList;//адреса операторов Continue
	std::map<unsigned short, CDefIntList *> aBreakList;//адреса операторов Break

	//Установка адресов перехода для команд Continue и Break
	void StartDoList();
	void FinishDoList(CByteCode	&cByteCode, int nGotoContinue, int nGotoBreak);

	//МЕТКИ
	std::map<wxString, unsigned int> m_cLabelsDef;	//объявления
	std::vector <SLabel> m_cLabels;	//список встретившихся переходов на метки

	void DoLabels();

	void SetModule(CCompileModule *module) { m_compileModule = module; }
	void SetFunction(CFunction *function) { m_functionContext = function; }

	SParam AddVariable(const wxString &sName, const wxString &sType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);
	SParam GetVariable(const wxString &sName, bool bFindInParent = true, bool bCheckError = false, bool bContext = false, bool bTempVar = false);

	bool FindVariable(const wxString &sName, wxString &sContextVariable = wxString(), bool bContext = false);
	bool FindFunction(const wxString &sName, wxString &sContextVariable = wxString(), bool bContext = false);

	CCompileContext(CCompileContext *hSetParent = NULL) : m_parentContext(hSetParent),
		m_nDoNumber(0), m_nReturn(0), m_nTempVar(0), m_nFindLocalInParent(1),
		m_compileModule(NULL), m_functionContext(NULL)
	{
	};

	~CCompileContext();
};

struct CParamVariable
{
	bool m_bByRef;

	wxString m_sName;//Имя переменной
	wxString m_sType;//тип значения

	SParam 	m_vData;//Значение по умолчанию

	CParamVariable() : m_bByRef(false) {
		m_vData.m_nArray = -1;
		m_vData.m_nIndex = -1;
	};
};

//определение функции
struct CFunction
{
	bool m_bExport;
	bool m_bContext;

	wxString m_sRealName; //Имя функции
	wxString m_sName; //Имя функции в верхнем регистре
	wxString m_sType;	 //тип (в англ. нотации), если это типизированная функция

	CCompileContext *m_pContext;//конекст компиляции

	unsigned int m_nVarCount;//число локальных переменных
	unsigned int m_nStart;//стартовая позиция в массиве байт-кодов
	unsigned int m_nFinish;//конечная позиция в массиве байт-кодов

	SParam m_realRetValue;//для хранения переменной при реальном вызове

	//для IntelliSense
	unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

	wxString m_sShortDescription;//включает в себя всю строку после ключевого слова Функция(Процедура)
	wxString m_sLongDescription;//включает в себя весь слитный (т.е.е буз пустых строк) блок комментарий до определения функции (процедуры)

	wxString m_sContextVar; //имя конткстной переменной

	std::vector<CParamVariable> m_aParamList;

	CFunction(const wxString &sFuncName, CCompileContext *pSetContext = NULL) : m_sName(sFuncName), m_pContext(pSetContext),
		m_bExport(false), m_bContext(false), m_nVarCount(0), m_nStart(0), m_nFinish(0), m_nNumberLine(0)
	{
		m_realRetValue.m_nArray = 0;
		m_realRetValue.m_nIndex = 0;
	};

	~CFunction()
	{
		if (m_pContext)
			delete m_pContext; //Удаляем подчиненный контекст (у каждой функции свой список меток и локальных переменных)
	};
};

struct CCallFunction
{
	wxString m_sName;//Имя вызываемой функции
	wxString m_sRealName;//Имя вызываемой функции

	SParam m_sRetValue;//переменная куда должен возвращаться результат выполнения функции
	SParam m_sContextVal;//указатель на переменную Контекст

	unsigned int m_nAddLine;//положение в массиве байт-кодов, где встретился вызов (для случая, когда есть вызов, но функция еще не объявлена)
	unsigned int m_nError;//для выдачи сообщений при ошибках

	unsigned int m_nNumberString;	//номер исходного текста (для вывода ошибок)
	unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

	wxString m_sModuleName;//имя модуля (т.к. возможны include подключения из разных модулей)

	std::vector<SParam> m_aParamList;//список передаваемых параметров (список перменных, если значение не задано, то д.б. (-1,-1))
};

class CMetaModuleObject;

//*******************************************************************
//*                         Класс: компилятор                       *
//*******************************************************************

class CCompileModule : public CTranslateModule
{
	friend class CProcUnit;

private:

	bool Recompile(); //Перекомпиляция текущего модуля из мета-объекта

public:

	CCompileModule();
	CCompileModule(CMetaModuleObject *moduleObject, bool commonModule = false);

	virtual ~CCompileModule();

	//Основные методы:
	void Reset();//Сброс данных для повторного использования объекта

	void PrepareModuleData();

	void AddVariable(const wxString &sName, const CValue &value);//поддержка внешних переменных
	void AddVariable(const wxString &sName, CValue *pValue);//поддержка внешних переменных

	void AddContextVariable(const wxString &sName, const CValue &value);
	void AddContextVariable(const wxString &sName, CValue *pValue);

	void RemoveVariable(const wxString &sName);

	void SetParent(CCompileModule *pSetParent);//Установка родительского модуля и запрещенного макс. прародителя
	CCompileModule *GetParent() const { return m_pParent; }

	bool Compile(); //Компиляция модуля из мета-объекта

	//атрибуты:
	CCompileModule *m_pParent;//родительский модуль (т.е. по отношению к текущему выполняет роль глобального модуля)

	bool m_bCommonModule; // true - only functions and export functions 

	//текущий контекст переменных, функций и меток
	CCompileContext	m_cContext;
	CCompileContext	*m_pContext;

	CByteCode m_cByteCode;        //выходной массив байт-кодов для исполнения виртуальной машиной

	int m_nCurrentCompile;		//текущее положение в массиве лексем

	bool m_bExpressionOnly;		//только вычисление выражений (нет новых заданий функций)
	bool m_bNeedRecompile;

	//соответствие внешних переменных
	std::map<wxString, CValue *> m_aExternValues;
	//соответствие контекстных переменных
	std::map<wxString, CValue *> m_aContextValues;

protected:

	CMetaModuleObject *m_moduleObject;

	std::map<wxString, unsigned int> m_aHashConstList;
	std::vector <CCallFunction*> m_apCallFunctions;	//список встретившихся вызовов процедур и функций

	int m_nLastNumberLine;

public:

	static void InitializeCompileModule();

	CCompileContext *GetContext()
	{
		m_cContext.SetModule(this);
		return &m_cContext;
	};

	CMetaModuleObject *GetModuleObject() const { return m_moduleObject; }

	//методы вывода ошибок при компиляции:
	void SetError(int nErr, const wxString &sError = wxEmptyString);
	void SetError(int nErr, char c);

	SParam GetExpression(int nPriority = 0);

protected:

	CLexem PreviewGetLexem();
	CLexem GetLexem();
	CLexem GETLexem();
	void GETDelimeter(char c);

	bool IsDelimeter(char c);
	bool IsKeyWord(int nKey);

	bool IsNextDelimeter(char c);
	bool IsNextKeyWord(int nKey);

	void GETKeyWord(int nKey);

	wxString GETIdentifier(bool realName = false);
	CValue GETConstant();

	void AddLineInfo(CByte &code);

	bool CompileModule();

	bool CompileFunction();
	bool CompileDeclaration();

	bool CompileBlock();

	bool CompileNewObject();
	bool CompileGoto();
	bool CompileIf();
	bool CompileWhile();
	bool CompileFor();
	bool CompileForeach();

	SParam GetCallFunction(const wxString &sName);
	SParam GetCurrentIdentifier(int &nIsSet);

	SParam FindConst(CValue &vData);

	bool AddCallFunction(CCallFunction* pRealCall);
	CFunction *GetFunction(const wxString &sName, int *pNumber = NULL);

	bool IsTypeVar(const wxString &sVariable = wxEmptyString);
	wxString GetTypeVar(const wxString &sVariable = wxEmptyString);
	void AddTypeSet(const SParam &sVariable);

	int GetConstString(const wxString &sMethod);

protected:

	virtual SParam AddVariable(const wxString &sName, const wxString &sType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);

	virtual SParam GetVariable();
	virtual SParam GetVariable(const wxString &sName, bool bCheckError = false, bool bLoadFromContext = false);
};

class CGlobalCompileModule : public CCompileModule {
public:
	CGlobalCompileModule(CMetaModuleObject *moduleObject) : CCompileModule(moduleObject, false) {}
};

#endif 