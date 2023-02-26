#ifndef _COMPILEMODULE_H__
#define _COMPILEMODULE_H__

#include "translateModule.h"

struct param_t {
	
	wxLongLong_t m_nArray;
	wxLongLong_t m_nIndex;
	wxString m_sName;//имя переменной 
	wxString m_sType;//тип переменной в англ. нотации (в случае явной типизации)

public:
	param_t() : m_nArray(0), m_nIndex(0) {}
};

struct variable_t {
	
	bool m_bExport;
	bool m_bContext;
	bool m_bTempVar;
	unsigned int m_nNumber;
	wxString m_sName;       //Имя переменной
	wxString m_sType;       //тип значени
	wxString m_sContextVar; //имя контекстной переменной
	wxString m_sRealName;   //Имя переменной реальное

public:

	variable_t() : m_bExport(false), m_bContext(false), m_bTempVar(false), m_nNumber(0) {};
	variable_t(const wxString& sVariableName) : m_sName(sVariableName), m_bExport(false), m_bContext(false), m_bTempVar(false), m_nNumber(0) {};
};

struct function_t;

//хранение одного шага программы
struct byteRaw_t {

	short m_nOper;                //код инструкции
	unsigned int m_nNumberString;	//номер исходного текста (для вывода ошибок)
	unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

	//параметры для инструкции:
	param_t m_param1;
	param_t m_param2;
	param_t m_param3;
	param_t m_param4; // - используется для оптимизации

	wxString m_sModuleName; //имя модуля (т.к. возможны include подключения из разных модулей)
	wxString m_sDocPath; // уникальный путь к документу 
	wxString m_sFileName; // путь к файлу (если внешняя обработка) 

public:

	byteRaw_t() : m_param1(), m_param2(), m_param3(), m_param4(),
		m_nOper(0), m_nNumberString(0), m_nNumberLine(0) {}
};

class CCompileModule;

//класс БАЙТ-КОДА
struct byteCode_t {

	struct byteMethod_t {
		long m_lCodeParamCount;
		long m_lCodeLine;
		bool m_bCodeRet;
	public:
		operator long() const {
			return m_lCodeLine;
		}
	};

	//Атрибуты:
	byteCode_t* m_parent;	//Родительский байт-код (для проверки)
	bool m_bCompile;		//признак успешной компиляции
	CCompileModule* m_compileModule;
	long m_lVarCount;		// количество локальных переменных в модуле
	long m_lStartModule;	// начало позиции старта модуля
	wxString m_sModuleName;//имя исполняемого модуля, которому принадлежит байт-код

	//список внешних и контекстных переменных
	std::vector<CValue*> m_aExternValues;
	std::vector <byteRaw_t>	m_aCodeList;//исполняемый код модуля
	std::vector <CValue> m_aConstList;//список констант модуля
	std::map<wxString, long> m_aVarList; //список переменных модуля	
	std::map<wxString, byteMethod_t> m_aFuncList; //список функций и процедур модуля
	std::map<wxString, long> m_aExportVarList; //список экспортных переменных модуля
	std::map<wxString, byteMethod_t> m_aExportFuncList; //список экспортных функций и процедур модуля

public:

	byteCode_t() {
		Reset();
	};

	void SetModule(CCompileModule* compileModule) {
		m_compileModule = compileModule;
	}

	long FindMethod(const wxString& methodName) const;
	long FindExportMethod(const wxString& methodName) const;
	long FindVariable(const wxString& varName) const;
	long FindExportVariable(const wxString& varName) const;

	long GetNParams(const long lCodeLine) const {
		// if is not initializer then set no return value
		auto foundedFunction = std::find_if(m_aFuncList.begin(), m_aFuncList.end(),
			[lCodeLine](const std::pair<const wxString, byteMethod_t>& pair) {
				return lCodeLine == ((long)pair.second - 1);
			}
		);
		if (foundedFunction != m_aFuncList.end())
			return foundedFunction->second.m_lCodeParamCount;
		return 0;
	}

	bool HasRetVal(const long lCodeLine) const {
		// if is not initializer then set no return value
		auto foundedFunction = std::find_if(m_aFuncList.begin(), m_aFuncList.end(),
			[lCodeLine](const std::pair<const wxString, byteMethod_t>& pair) {
				return pair.second.m_bCodeRet && lCodeLine == ((long)pair.second - 1);
			}
		);
		return foundedFunction != m_aFuncList.end();
	}

	void Reset() {
		m_lStartModule = 0;
		m_lVarCount = 0;
		m_sModuleName = wxEmptyString;
		m_bCompile = false;
		m_parent = NULL;
		m_compileModule = NULL;
		m_aCodeList.clear();
		m_aConstList.clear();
		m_aVarList.clear();
		m_aFuncList.clear();
		m_aExportVarList.clear();
		m_aExportFuncList.clear();
		m_aExternValues.clear();
	}
};

struct label_t {
	wxString m_sName;
	int		m_nLine;
	int		m_nError;
};

class CCompileModule;

//*******************************************************************
//*                Класс: контекст компиляции                       *
//*******************************************************************

typedef std::vector<CValue> CDefValueList;
typedef std::vector<bool> CDefBoolList;
typedef std::vector<int> CDefIntList;

struct compileContext_t {

	function_t* m_functionContext;
	compileContext_t* m_parentContext;//родительский контекст

	CCompileModule* m_compileModule;

	//ПЕРЕМЕННЫЕ
	std::map <wxString, variable_t> m_cVariables;

	int m_nTempVar;//номер текущей временной переменной
	int m_nFindLocalInParent;//признак поиска переменных в родителе (на один уровень), в остальных случаях в родителях ищутся только экспортные переменные)

	//ФУНКЦИИ И ПРОЦЕДУРЫ
	std::map<wxString, function_t*>	m_cFunctions;//список встретившихся определений функций

	short m_nReturn;//режим обработки оператора RETURN : RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION
	wxString m_sCurFuncName;//имя текущей компилируемой функции (для обработки варианта вызова рекурсивной функции)

	//ЦИКЛЫ
	//Служебные атрибуты
	unsigned short m_nDoNumber;//номер вложенного цикла

	std::map<unsigned short, CDefIntList*> aContinueList;//адреса операторов Continue
	std::map<unsigned short, CDefIntList*> aBreakList;//адреса операторов Break

	//МЕТКИ
	std::map<wxString, unsigned int> m_cLabelsDef;	//объявления
	std::vector <label_t> m_cLabels;	//список встретившихся переходов на метки

public:

	//Установка адресов перехода для команд Continue и Break
	void StartDoList() {
		//создаем списки для команд Continue и Break (в них будут хранится адреса байт кодов, где встретились соответствующие команды)
		m_nDoNumber++;
		aContinueList[m_nDoNumber] = new CDefIntList();
		aBreakList[m_nDoNumber] = new CDefIntList();
	}

	//Установка адресов перехода для команд Continue и Break
	void FinishDoList(byteCode_t& cByteCode, int nGotoContinue, int nGotoBreak) {
		CDefIntList* pListC = (CDefIntList*)aContinueList[m_nDoNumber];
		CDefIntList* pListB = (CDefIntList*)aBreakList[m_nDoNumber];
		if (pListC == 0 || pListB == 0) {
#ifdef _DEBUG 
			wxLogDebug("Error (FinishDoList) nGotoContinue=%d, nGotoBreak=%d\n", nGotoContinue, nGotoBreak);
			wxLogDebug("m_nDoNumber=%d\n", m_nDoNumber);
#endif 
			m_nDoNumber--;
			return;
		}
		for (unsigned int i = 0; i < pListC->size(); i++)
			cByteCode.m_aCodeList[*pListC[i].data()].m_param1.m_nIndex = nGotoContinue;

		for (unsigned int i = 0; i < pListB->size(); i++)
			cByteCode.m_aCodeList[*pListB[i].data()].m_param1.m_nIndex = nGotoBreak;
		aContinueList.erase(m_nDoNumber);
		aContinueList.erase(m_nDoNumber);
		delete pListC;
		delete pListB;
		m_nDoNumber--;
	}

	void DoLabels();

	void SetModule(CCompileModule* module) {
		m_compileModule = module;
	}

	void SetFunction(function_t* function) {
		m_functionContext = function;
	}

	param_t AddVariable(const wxString& varName, const wxString& sType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);
	param_t GetVariable(const wxString& varName, bool bFindInParent = true, bool bCheckError = false, bool bContext = false, bool bTempVar = false);

	bool FindVariable(const wxString& varName, variable_t*& foundedVar, bool context = false);
	bool FindFunction(const wxString& funcName, function_t*& foundedFunc, bool context = false);

	compileContext_t(compileContext_t* hSetParent = NULL) : m_parentContext(hSetParent),
		m_nDoNumber(0), m_nReturn(0), m_nTempVar(0), m_nFindLocalInParent(1),
		m_compileModule(NULL), m_functionContext(NULL) {
	};

	~compileContext_t();
};

struct paramVariable_t
{
	bool m_bByRef;

	wxString m_sName;//Имя переменной
	wxString m_sType;//тип значения

	param_t  m_vData;//Значение по умолчанию

public:

	paramVariable_t() : m_bByRef(false) {
		m_vData.m_nArray = -1;
		m_vData.m_nIndex = -1;
	};
};

//определение функции
struct function_t {

	bool m_bExport, m_bContext;

	wxString m_sRealName; //Имя функции
	wxString m_sName; //Имя функции в верхнем регистре
	wxString m_sType;	 //тип (в англ. нотации), если это типизированная функция

	compileContext_t* m_pContext;//конекст компиляции

	unsigned int m_lVarCount;//число локальных переменных
	unsigned int m_nStart;//стартовая позиция в массиве байт-кодов
	unsigned int m_nFinish;//конечная позиция в массиве байт-кодов

	param_t m_realRetValue;//для хранения переменной при реальном вызове

	//для IntelliSense
	unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

	wxString m_sShortDescription;//включает в себя всю строку после ключевого слова Функция(Процедура)
	wxString m_sLongDescription;//включает в себя весь слитный (т.е.е буз пустых строк) блок комментарий до определения функции (процедуры)
	wxString m_sContextVar; //имя конткстной переменной

	std::vector<paramVariable_t> m_aParamList;

public:
	
	function_t(const wxString& funcName, compileContext_t* compileContext = NULL) : m_sName(funcName), m_pContext(compileContext),
		m_bExport(false), m_bContext(false), m_lVarCount(0), m_nStart(0), m_nFinish(0), m_nNumberLine(0) {
		m_realRetValue.m_nArray = 0;
		m_realRetValue.m_nIndex = 0;
	};

	~function_t() {
		if (m_pContext != NULL)
			delete m_pContext; //Удаляем подчиненный контекст (у каждой функции свой список меток и локальных переменных)
	};
};

struct сallFunction_t {

	wxString m_sName;//Имя вызываемой функции
	wxString m_sRealName;//Имя вызываемой функции

	param_t m_sRetValue;//переменная куда должен возвращаться результат выполнения функции
	param_t m_sContextVal;//указатель на переменную Контекст

	unsigned int m_nAddLine;//положение в массиве байт-кодов, где встретился вызов (для случая, когда есть вызов, но функция еще не объявлена)
	unsigned int m_nError;//для выдачи сообщений при ошибках

	unsigned int m_nNumberString;	//номер исходного текста (для вывода ошибок)
	unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

	wxString m_sModuleName;//имя модуля (т.к. возможны include подключения из разных модулей)

	std::vector<param_t> m_aParamList;//список передаваемых параметров (список перменных, если значение не задано, то д.б. (-1,-1))
};

class CMetaModuleObject;

//*******************************************************************
//*                         Класс: компилятор                       *
//*******************************************************************

class CCompileModule : public CTranslateModule {
	friend class CProcUnit;
private:
	bool Recompile(); //Перекомпиляция текущего модуля из мета-объекта
public:

	CCompileModule();
	CCompileModule(CMetaModuleObject* moduleObject, bool commonModule = false);
	virtual ~CCompileModule();

	//Основные методы:
	void Reset();//Сброс данных для повторного использования объекта

	void PrepareModuleData();

	void AddVariable(const wxString& sName, const CValue& value);//поддержка внешних переменных
	void AddVariable(const wxString& sName, CValue* pValue);//поддержка внешних переменных

	void AddContextVariable(const wxString& sName, const CValue& value);
	void AddContextVariable(const wxString& sName, CValue* pValue);

	void RemoveVariable(const wxString& sName);

	void SetParent(CCompileModule* pSetParent); //Установка родительского модуля и запрещенного макс. прародителя

	CCompileModule* GetParent() const {
		return m_parent;
	}

	bool Compile(); //Компиляция модуля из мета-объекта

	//атрибуты:
	bool m_bCommonModule; // true - only functions and export functions 

	CCompileModule* m_parent;//родительский модуль (т.е. по отношению к текущему выполняет роль глобального модуля)

	//текущий контекст переменных, функций и меток
	compileContext_t m_cContext, * m_pContext;
	byteCode_t m_cByteCode;        //выходной массив байт-кодов для исполнения виртуальной машиной

	int m_nCurrentCompile;		//текущее положение в массиве лексем

	bool m_bExpressionOnly;		//только вычисление выражений (нет новых заданий функций)
	bool m_bNeedRecompile;

	//соответствие внешних переменных
	std::map<wxString, CValue*> m_aExternValues;
	//соответствие контекстных переменных
	std::map<wxString, CValue*> m_aContextValues;

protected:

	CMetaModuleObject* m_moduleObject;

	std::map<wxString, unsigned int> m_aHashConstList;
	std::vector <сallFunction_t*> m_apCallFunctions;	//список встретившихся вызовов процедур и функций

	int m_nLastNumberLine;

public:

	static void InitializeCompileModule();

	compileContext_t* GetContext() {
		m_cContext.SetModule(this);
		return &m_cContext;
	};

	CMetaModuleObject* GetModuleObject() const {
		return m_moduleObject;
	}

	//методы вывода ошибок при компиляции:
	void SetError(int nErr, const wxString& sError = wxEmptyString);
	void SetError(int nErr, char c);

	param_t GetExpression(int nPriority = 0);

protected:

	lexem_t PreviewGetLexem();
	lexem_t GetLexem();
	lexem_t GETLexem();
	void GETDelimeter(char c);

	bool IsDelimeter(char c);
	bool IsKeyWord(int nKey);

	bool IsNextDelimeter(char c);
	bool IsNextKeyWord(int nKey);

	void GETKeyWord(int nKey);

	wxString GETIdentifier(bool realName = false);
	CValue GETConstant();

	void AddLineInfo(byteRaw_t& code);

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

	param_t GetCallFunction(const wxString& sName);
	param_t GetCurrentIdentifier(int& nIsSet);

	param_t FindConst(CValue& vData);

	bool AddCallFunction(сallFunction_t* pRealCall);
	function_t* GetFunction(const wxString& sName, int* pNumber = NULL);

	bool IsTypeVar(const wxString& sVariable = wxEmptyString);
	wxString GetTypeVar(const wxString& sVariable = wxEmptyString);
	void AddTypeSet(const param_t& sVariable);

	int GetConstString(const wxString& sMethod);

protected:

	virtual param_t AddVariable(const wxString& sName,
		const wxString& sType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);
	virtual param_t GetVariable();
	virtual param_t GetVariable(const wxString& sName, bool bCheckError = false, bool bLoadFromContext = false);
};

class CGlobalCompileModule : public CCompileModule {
public:
	CGlobalCompileModule(CMetaModuleObject* moduleObject) :
		CCompileModule(moduleObject, false) {
	}
};

#endif 