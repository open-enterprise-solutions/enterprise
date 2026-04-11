#ifndef _AUTOCOMPLETE_COMPILE_H__
#define _AUTOCOMPLETE_COMPILE_H__

#include "backend/moduleInfo.h"
#include "backend/compiler/compileCode.h"

struct ibParamValue {
	wxString m_paramName;//variable name 
	wxString m_paramType;//variable type in English notation (in case of explicit typing)
	ibValue m_paramObject;
};

class ibPrecompileCode;
struct CPrecompileFunction;

struct CPrecompileVariable
{
	bool bExport;
	bool bContext;
	bool bTempVar;

	int nNumber;

	wxString strName;//��� ����������
	wxString strType;//��� ��������
	wxString strRealName;

	ibValue m_valContext;
	ibValue m_valObject;

	CPrecompileVariable() : bExport(false), bContext(false), bTempVar(false), nNumber(0) {};
	CPrecompileVariable(wxString csVarName) : strName(csVarName), bExport(false), bContext(false), bTempVar(false), nNumber(0) {};
};

struct CPrecompileContext
{
	ibPrecompileCode* pModule;
	void SetModule(ibPrecompileCode* pSetModule) { pModule = pSetModule; }

	CPrecompileContext* pParent;//������������ ��������
	CPrecompileContext* pStopParent;//������ ����������� ������� �����������
	CPrecompileContext* pContinueParent;//������ ����������� ������� �����������
	bool bStaticVariable;		//��� ���������� ���������

	//����������
	std::map <wxString, CPrecompileVariable> cVariables;

	ibParamValue GetVariable(const wxString& strVarName, bool bFindInParent = true, bool bCheckError = false, const ibValue& valVar = ibValue());
	ibParamValue AddVariable(const wxString& strVarName, const wxString& varType = wxEmptyString, bool bExport = false, bool bTempVar = false, const ibValue& valVar = ibValue());
	void SetVariable(const wxString& strVarName, const ibValue& valVar);

	bool FindVariable(const wxString& strName, ibValue valContext = ibValue(), bool bContext = false);
	bool FindFunction(const wxString& strName, ibValue valContext = ibValue(), bool bContext = false);

	void RemoveVariable(const wxString& strName);

	int nTempVar;//����� ������� ��������� ����������
	int nFindLocalInParent;//������� ������ ���������� � �������� (�� ���� �������), � ��������� ������� � ��������� ������ ������ ���������� ����������)

	//������� � ���������
	std::map<wxString, CPrecompileFunction*> cFunctions;//������ ������������� ����������� �������
	int nReturn;//����� ��������� ��������� RETURN : RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION
	wxString sCurFuncName;//��� ������� ������������� ������� (��� ��������� �������� ������ ����������� �������)

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

//����������� �������
struct CPrecompileFunction
{
	wxString strRealName;//��� �������
	wxString strName;//��� ������� � ������� ��������
	std::vector<ibParamValue> aParamList;
	bool bExport;
	bool bContext;
	CPrecompileContext* m_pContext;//������� ����������
	int nVarCount;// number of local variables
	int nStart;// starting position � ������� ����-�����
	int nFinish;//�������� ������� � ������� ����-�����

	ibValue m_valContext;

	ibParamValue RealRetValue;//��� �������� ���������� ��� �������� ������
	bool bSysFunction;
	wxString strType;		//��� (� ����. �������), ���� ��� �������������� �������

	//��� IntelliSense
	int	nNumberLine;	//source line number (for breakpoints)
	wxString strShortDescription;//�������� � ���� ��� ������ ����� ��������� ����� �������(���������)
	wxString sLongDescription;//�������� � ���� ���� ������� (�.�.� ��� ������ �����) ���� ����������� �� ����������� ������� (���������)

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
		if (m_pContext)//������� ����������� �������� (� ������ ������� ���� ������ ����� � ��������� ����������)
			delete m_pContext;
	};
};

//*******************************************************************
//*                         �����: ���-����������                   *
//*******************************************************************
class ibPrecompileCode : public ibTranslateCode
{
	int m_numCurrentCompile;		//������� ��������� � ������� ������

	ibValueMetaObjectModuleBase* m_moduleObject;

	std::map<wxString, unsigned int> m_aHashConstList;

	CPrecompileContext	m_cContext;
	CPrecompileContext* m_pContext;
	CPrecompileContext* m_pCurrentContext;

	ibValue m_valObject;

	unsigned int nLastPosition;

	wxString m_strLastExpression;
	wxString m_strLastKeyword;
	wxString m_strLastParentKeyword;

	bool m_bCalcValue;

	unsigned int m_nCurrentPos;

	friend class ibCodeEditor;

public:

	//�������� ������:
	virtual void Clear();//����� ������ ��� ���������� ������������� �������
	void PrepareModuleData();

	ibPrecompileCode(ibValueMetaObjectModuleBase* moduleObject);
	virtual ~ibPrecompileCode();

	ibValue GetComputeValue() const { return m_valObject; }
	CPrecompileContext* GetContext() {
		m_cContext.SetModule(this);
		return &m_cContext;
	};

	CPrecompileContext* GetCurrentContext() const { return m_pCurrentContext; }

	bool Compile();

	bool PrepareLexem();
#ifdef UTF8_LEXEM_TRANSLATE
	void PrepareLexem(unsigned int line, int offsetLine, const int& str_length, const int& str_utf8_length);
#else 
	void PrepareLexem(unsigned int line, int offsetLine, const int& str_length);
#endif

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

	ibLexem PreviewGetLexem();
	ibLexem GetLexem();
	ibLexem GETLexem();
	void GETDelimeter(const wxUniChar& c);

	bool IsNextDelimeter(const wxUniChar& c);
	bool IsNextKeyWord(int nKey);
	void GETKeyWord(int nKey);
	wxString GETIdentifier(bool strRealName = false);
	ibValue GETConstant();
	int GetConstString(const wxString& sMethod);

	int IsTypeVar(const wxString& strType = wxEmptyString);
	wxString GetTypeVar(const wxString& strType = wxEmptyString);

	ibParamValue GetExpression(int nPriority = 0);

	ibParamValue GetCurrentIdentifier(int& nIsSet);
	ibParamValue GetCallFunction(const wxString& strName);

	void AddVariable(const wxString& strVarName, const ibValue& varVal);

	ibParamValue GetVariable(const wxString& strVarName, bool bCheckError = false);
	ibParamValue GetVariable();

	void SetVariable(const wxString& strVarName, const ibValue& varVal);

	ibParamValue FindConst(ibValue& vData);
};

#endif 