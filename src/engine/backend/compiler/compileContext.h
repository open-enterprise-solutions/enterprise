#ifndef _COMPILE_CONTEXT__H_
#define _COMPILE_CONTEXT__H_

#include "backend/compiler/byteCode.h"

struct CCompileContext;

struct CVariable {

	bool m_bExport;
	bool m_bContext;
	bool m_bTempVar;
	unsigned int m_nNumber;
	wxString m_strName;       //Имя переменной
	wxString m_strType;       //тип значени
	wxString m_strContextVar; //имя контекстной переменной
	wxString m_strRealName;   //Имя переменной реальное

public:

	CVariable() : m_bExport(false), m_bContext(false), m_bTempVar(false), m_nNumber(0) {};
	CVariable(const wxString& sVariableName) : m_strName(sVariableName), m_bExport(false), m_bContext(false), m_bTempVar(false), m_nNumber(0) {};
};

struct CParamVariable
{
	bool m_bByRef;
	wxString m_strName;   //Имя переменной
	wxString m_strType;   //тип значения
	CParamUnit m_vData; //Значение по умолчанию
public:

	CParamVariable() : m_bByRef(false) {
		m_vData.m_nArray = -1;
		m_vData.m_nIndex = -1;
	};
};

//определение функции
struct CFunction {

	bool m_bExport, m_bContext;

	wxString m_strRealName; //Имя функции
	wxString m_strName; //Имя функции в верхнем регистре
	wxString m_strType;	 //тип (в англ. нотации), если это типизированная функция

	CCompileContext* m_pContext;//конекст компиляции

	unsigned int m_lVarCount;//число локальных переменных
	unsigned int m_nStart;//стартовая позиция в массиве байт-кодов
	unsigned int m_nFinish;//конечная позиция в массиве байт-кодов

	CParamUnit m_realRetValue;//для хранения переменной при реальном вызове

	//для IntelliSense
	unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

	wxString m_strShortDescription;//включает в себя всю строку после ключевого слова Функция(Процедура)
	wxString m_strLongDescription;//включает в себя весь слитный (т.е.е буз пустых строк) блок комментарий до определения функции (процедуры)
	wxString m_strContextVar; //имя конткстной переменной

	std::vector<CParamVariable> m_aParamList;

public:

	CFunction(const wxString& funcName, CCompileContext* compileContext = nullptr);
	~CFunction();
};

struct CLabel {
	wxString m_strName;
	int		m_nLine;
	int		m_nError;
};

struct CCompileContext {

	CFunction* m_functionContext;
	CCompileContext* m_parentContext; //родительский контекст
	CCompileCode* m_compileModule;

	//ПЕРЕМЕННЫЕ
	std::map <wxString, CVariable> m_cVariables;

	int m_nTempVar;//номер текущей временной переменной
	int m_nFindLocalInParent;//признак поиска переменных в родителе (на один уровень), в остальных случаях в родителях ищутся только экспортные переменные)

	//ФУНКЦИИ И ПРОЦЕДУРЫ
	std::map<wxString, CFunction*>	m_cFunctions;//список встретившихся определений функций

	short m_nReturn;//режим обработки оператора RETURN : RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION
	wxString m_strCurFuncName;//имя текущей компилируемой функции (для обработки варианта вызова рекурсивной функции)

	//ЦИКЛЫ
	//Служебные атрибуты
	unsigned short m_nDoNumber;//номер вложенного цикла

	std::map<unsigned short, std::vector<int>*> aContinueList;//адреса операторов Continue
	std::map<unsigned short, std::vector<int>*> aBreakList;//адреса операторов Break

	//МЕТКИ
	std::map<wxString, unsigned int> m_cLabelsDef;	//объявления
	std::vector <CLabel> m_cLabels;	//список встретившихся переходов на метки

public:

	//Установка адресов перехода для команд Continue и Break
	void StartDoList() {
		//создаем списки для команд Continue и Break (в них будут хранится адреса байт кодов, где встретились соответствующие команды)
		m_nDoNumber++;
		aContinueList[m_nDoNumber] = new std::vector<int>();
		aBreakList[m_nDoNumber] = new std::vector<int>();
	}

	//Установка адресов перехода для команд Continue и Break
	void FinishDoList(CByteCode& cByteCode, int gotoContinue, int gotoBreak) {
		std::vector<int>* pListC = (std::vector<int>*)aContinueList[m_nDoNumber];
		std::vector<int>* pListB = (std::vector<int>*)aBreakList[m_nDoNumber];
		if (pListC == 0 || pListB == 0) {
#ifdef DEBUG 
			wxLogDebug("Error (FinishDoList) gotoContinue=%d, gotoBreak=%d\n", gotoContinue, gotoBreak);
			wxLogDebug("m_nDoNumber=%d\n", m_nDoNumber);
#endif 
			m_nDoNumber--;
			return;
		}
		for (unsigned int i = 0; i < pListC->size(); i++) {
			cByteCode.m_aCodeList[*pListC[i].data()].m_param1.m_nIndex = gotoContinue;
		}
		for (unsigned int i = 0; i < pListB->size(); i++) {
			cByteCode.m_aCodeList[*pListB[i].data()].m_param1.m_nIndex = gotoBreak;
		}
		aContinueList.erase(m_nDoNumber);
		aContinueList.erase(m_nDoNumber);
		delete pListC;
		delete pListB;
		m_nDoNumber--;
	}

	void DoLabels();

	void SetModule(CCompileCode* module) {
		m_compileModule = module;
	}

	void SetFunction(CFunction* function) {
		m_functionContext = function;
	}

	CParamUnit AddVariable(const wxString& strVarName, const wxString& strType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);
	CParamUnit GetVariable(const wxString& strVarName, bool bFindInParent = true, bool bCheckError = false, bool bContext = false, bool bTempVar = false);

	bool FindVariable(const wxString& strVarName, CVariable*& foundedVar, bool context = false);
	bool FindFunction(const wxString& funcName, CFunction*& foundedFunc, bool context = false);

	CCompileContext(CCompileContext* hSetParent = nullptr) : m_parentContext(hSetParent),
		m_nDoNumber(0), m_nReturn(0), m_nTempVar(0), m_nFindLocalInParent(1),
		m_compileModule(nullptr), m_functionContext(nullptr) {
	};

	~CCompileContext();
};

#endif