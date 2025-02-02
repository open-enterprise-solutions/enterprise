#ifndef _COMPILE_CODE_H__
#define _COMPILE_CODE_H__

#include "translateCode.h"
#include "compileContext.h"

//*******************************************************************
//*                         Класс: компилятор                       *
//*******************************************************************

class BACKEND_API CCompileCode : public CTranslateCode {

	friend class CProcUnit;

	struct CCallFunction {

		wxString m_strName;//Имя вызываемой функции
		wxString m_strRealName;//Имя вызываемой функции

		CParamUnit m_sRetValue;//переменная куда должен возвращаться результат выполнения функции
		CParamUnit m_sContextVal;//указатель на переменную Контекст

		unsigned int m_nAddLine;//положение в массиве байт-кодов, где встретился вызов (для случая, когда есть вызов, но функция еще не объявлена)
		unsigned int m_nError;//для выдачи сообщений при ошибках

		unsigned int m_nNumberString;	//номер исходного текста (для вывода ошибок)
		unsigned int m_nNumberLine;	//номер строки исходного текста (для точек останова)

		wxString m_strModuleName;//имя модуля (т.к. возможны include подключения из разных модулей)

		std::vector<CParamUnit> m_aParamList;//список передаваемых параметров (список перменных, если значение не задано, то д.б. (-1,-1))
	};

	friend struct CCompileContext;

public:

	CCompileCode();
	CCompileCode(const wxString& strModuleName, const wxString& strDocPath, bool onlyFunction = false);
	CCompileCode(const wxString& strFileName);

	virtual ~CCompileCode();

	//Основные методы:
	void Reset();//Сброс данных для повторного использования объекта

	void PrepareModuleData();

	void AddVariable(const wxString& strName, const CValue& value);//поддержка внешних переменных
	void AddVariable(const wxString& strName, CValue* pValue);//поддержка внешних переменных

	void AddContextVariable(const wxString& strName, const CValue& value);
	void AddContextVariable(const wxString& strName, CValue* pValue);

	void RemoveVariable(const wxString& strName);

	virtual void SetParent(CCompileCode* parent); //Установка родительского модуля и запрещенного макс. прародителя
	virtual CCompileCode* GetParent() const {
		return m_parent;
	}

	bool Compile(const wxString& strCode); //Компиляция модуля из мета-объекта

	//атрибуты:
	bool m_onlyFunction; // true - only functions and export functions 

	CCompileCode* m_parent;//родительский модуль (т.е. по отношению к текущему выполняет роль глобального модуля)

	//текущий контекст переменных, функций и меток
	CCompileContext m_cContext, * m_pContext;
	CByteCode		m_cByteCode;        //выходной массив байт-кодов для исполнения виртуальной машиной

	int				m_nCurrentCompile;		//текущее положение в массиве лексем
	bool			m_bExpressionOnly;		//только вычисление выражений (нет новых заданий функций)
	bool			m_changeCode;

	//соответствие внешних переменных
	std::map<wxString, CValue*> m_aExternValues;

	//соответствие контекстных переменных
	std::map<wxString, CValue*> m_aContextValues;

protected:

	std::map<wxString, unsigned int> m_aHashConstList;
	std::vector <CCallFunction*> m_apCallFunctions;	//список встретившихся вызовов процедур и функций

public:

	static void InitializeCompileModule();

	CCompileContext* GetContext() {
		m_cContext.SetModule(this);
		return &m_cContext;
	};

private:
	//методы вывода ошибок при компиляции:
	void SetError(int nErr, const wxString& strError = wxEmptyString);
	void SetError(int nErr, const wxUniChar &c);
public:

	virtual void ProcessError(const wxString& strFileName,
		const wxString& strModuleName, const wxString& strDocPath,
		unsigned int currPos, unsigned int currLine,
		const wxString& strCodeLineError, int codeError, const wxString& strErrorDesc
	) const;

	CParamUnit GetExpression(int priority = 0);

protected:

	lexem_t PreviewGetLexem();
	lexem_t GetLexem();
	lexem_t GETLexem();
	void GETDelimeter(const wxUniChar &c);

	bool IsDelimeter(const wxUniChar &c);
	bool IsKeyWord(int nKey);

	bool IsNextDelimeter(const wxUniChar &c);
	bool IsNextKeyWord(int nKey);

	void GETKeyWord(int nKey);

	wxString GETIdentifier(bool strRealName = false);
	CValue GETConstant();

	void AddLineInfo(CByteUnit& code);

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

	CParamUnit GetCallFunction(const wxString& strName);
	CParamUnit GetCurrentIdentifier(int& nIsSet);

	CParamUnit FindConst(const CValue& vData);

	bool AddCallFunction(CCallFunction* pRealCall);
	CFunction* GetFunction(const wxString& strName, int* pNumber = nullptr);

	bool IsTypeVar(const wxString& sVariable = wxEmptyString);
	wxString GetTypeVar(const wxString& sVariable = wxEmptyString);
	void AddTypeSet(const CParamUnit& sVariable);

	int GetConstString(const wxString& sMethod);

protected:

	virtual CParamUnit AddVariable(const wxString& strName,
		const wxString& strType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);
	virtual CParamUnit GetVariable();
	virtual CParamUnit GetVariable(const wxString& strName, bool bCheckError = false, bool bLoadFromContext = false);
};

#endif 