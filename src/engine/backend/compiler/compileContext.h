#ifndef _COMPILE_CONTEXT__H_
#define _COMPILE_CONTEXT__H_

#include "backend/compiler/byteCode.h"

//function properties:
enum {
	RETURN_NONE = 0,//no return (module code)
	RETURN_PROCEDURE,//return from procedure
	RETURN_FUNCTION,//return from function
	RETURN_BLOCK,//return from context
};

//variable flags (specified with a negative value in the nArray attribute of the bytecode)
enum {
	DEF_VAR_SKIP = -1,// missing parameter
	DEF_VAR_DEFAULT = -2,//default parameter
	DEF_VAR_TEMP = -3,//flag of a temporary local variable
	DEF_VAR_NORET = -7,//function (procedure) does not return values
	DEF_VAR_CONST = 1000,//loading constants
};

enum {
	CODE_VBS = ibProgramSyntax::syntax_vbs,
	CODE_CES = ibProgramSyntax::syntax_ces
};

struct ibCompileContext {

#pragma region __context_unit_h__

	//variable definition
	struct ibVariable
	{
		ibVariable() : m_bExport(false), m_bContext(false), m_bTempVar(false), m_numVariable(0) {}
		ibVariable(const wxString& strVariableName) : m_bExport(false), m_bContext(false), m_bTempVar(false), m_numVariable(0), m_strName(strVariableName) {}

		bool m_bExport;
		bool m_bContext;
		bool m_bTempVar;
		unsigned int m_numVariable;
		wxString m_strName; // Variable name
		wxString m_strType; // Value type
		wxString m_strRealName; // Real variable name
		wxString m_strContext; //name of the context variable
	};

	//function definition
	struct ibFunction
	{
		struct ibParamVariable
		{
			ibParamVariable() : m_bByRef(false) {
				m_puValue.m_numArray = -1;
				m_puValue.m_numIndex = -1;
			}

			bool m_bByRef;
			wxString m_strName; // Variable name
			wxString m_strType; // Value type
			ibParamUnit m_puValue; // Default value
		};

		ibFunction(const wxString& strFuncName, ibCompileContext* compileContext = nullptr) :
			m_bExport(false),
			m_bContext(false),
			m_strName(strFuncName),
			m_compileContext(compileContext),
			m_lVarCount(0), m_nStart(0), m_nFinish(0), m_numLine(0)
		{
			//Set current context 
			if (compileContext != nullptr)
				m_compileContext->m_functionContext = this;
		}

		~ibFunction()
		{
			//Delete the subordinate context (each function has its own list of labels and local variables)
			if (m_compileContext)
				delete m_compileContext;
		}

		bool m_bExport, m_bContext;

		wxString m_strRealName; //Function name
		wxString m_strName; //Function name in uppercase
		wxString m_strType; //type (in English notation), if it is a typed function
		wxString m_strContext; //name of the context variable

		ibCompileContext* m_compileContext;//compilation context

		unsigned int m_lVarCount;// number of local variables
		unsigned int m_nStart;// starting position in bytecode array
		unsigned int m_nFinish;//final position in bytecode array

		//for IntelliSense
		unsigned int m_numLine; //source line number (for breakpoints)

		wxString m_strShortDescription;//includes the entire line after the Function (Procedure) keyword
		wxString m_strLongDescription;//includes the entire merged (i.e. without empty lines) comment block before the function (procedure) definition

		std::vector<ibParamVariable> m_listParam;
	};

	//label definition
	struct ibLabel
	{
		int		 m_numLine = 0;
		int		 m_numError = 0;
		wxString m_strName;
	};

#pragma endregion

	ibCompileContext(ibCompileCode* compileCode) :
		m_compileModule(compileCode), m_parentContext(nullptr), m_functionContext(nullptr),
		m_numTempVar(0), m_numFindLocalInParent(1), m_numReturn(0), m_numDoNumber(0) {
	}

	ibCompileContext(ibCompileContext* compileContext) :
		m_compileModule(nullptr), m_parentContext(compileContext), m_functionContext(nullptr),
		m_numTempVar(0), m_numFindLocalInParent(1), m_numReturn(0), m_numDoNumber(0) {
	}

	~ibCompileContext() {}

	//Create new context 
	ibCompileContext* CreateContext(short numReturn)
	{
		ibCompileContext* compileContext = new ibCompileContext(this);

		compileContext->m_numReturn = numReturn;
		compileContext->m_compileModule = m_compileModule;

		return compileContext;
	}

	//Setting jump addresses for Continue and Break commands
	void StartLoopList() {

		//create lists for Continue and Break commands (they will store the addresses of byte codes where the corresponding commands were encountered)
		m_numDoNumber++;
		m_listContinue[m_numDoNumber] = new std::vector<int>();
		m_listBreak[m_numDoNumber] = new std::vector<int>();
	}

	//Setting jump addresses for Continue and Break commands
	void FinishLoopList(ibByteCode& cByteCode, int gotoContinue, int gotoBreak) {
		std::vector<int>* pListC = m_listContinue[m_numDoNumber];
		std::vector<int>* pListB = m_listBreak[m_numDoNumber];
		if (pListC == 0 || pListB == 0) {
#ifdef DEBUG 
			wxLogDebug(wxT("Error (FinishLoopList) gotoContinue=%d, gotoBreak=%d\n"), gotoContinue, gotoBreak);
			wxLogDebug(wxT("m_numDoNumber=%d\n"), m_numDoNumber);
#endif 
			m_numDoNumber--;
			return;
		}
		for (unsigned int i = 0; i < pListC->size(); i++) {
			cByteCode.m_listCode[*pListC[i].data()].m_param1.m_numIndex = gotoContinue;
		}
		for (unsigned int i = 0; i < pListB->size(); i++) {
			cByteCode.m_listCode[*pListB[i].data()].m_param1.m_numIndex = gotoBreak;
		}
		m_listContinue.erase(m_numDoNumber);
		m_listContinue.erase(m_numDoNumber);
		delete pListC;
		delete pListB;
		m_numDoNumber--;
	}

	void CreateLabels();

	ibParamUnit CreateVariable(const wxString& strPrefix = wxT("@temp_"));
	ibParamUnit AddVariable(const wxString& strVarName, const wxString& strType = wxEmptyString, bool bExport = false, bool bContext = false, bool bTempVar = false);
	ibParamUnit GetVariable(const wxString& strVarName, bool bFindInParent = true, bool bCheckError = false, bool bContext = false, bool bTempVar = false);

	void PushVariable(const wxString& strVarName, const wxString& strContextVar, unsigned int numVariable,
		const wxString& typeVar = wxEmptyString, bool exportVar = true, bool contextVar = true, bool tempVar = false);
	void PushFunction(const wxString& strFuncName, const wxString& strContextVar, const wxString& strShortDescription, unsigned int numFunction,
		bool hasRetVal = true, int argCount = 0);

	bool FindVariable(const wxString& strVarName, std::shared_ptr<ibVariable>& foundedVar, bool context = false);
	bool FindFunction(const wxString& strFuncName, std::shared_ptr<ibFunction>& foundedFunc, bool context = false);

	//Reset compile context
	void Reset() {

		m_numDoNumber = 0;
		m_numReturn = 0;
		m_numTempVar = 0;

		m_numFindLocalInParent = 1;

		m_listContinue.clear();
		m_listBreak.clear();

		m_listLabel.clear();
		m_listLabelDef.clear();

		// clear functions & variables 
		m_listVariable.clear();
		m_listFunction.clear();
	}

	ibCompileCode* m_compileModule;
	ibCompileContext* m_parentContext; //parent context

	//current context 
	ibFunction* m_functionContext;

	//VARIABLES
	std::map<wxString, std::shared_ptr<ibVariable>> m_listVariable;

	int m_numTempVar;//current temporary variable number
	int m_numFindLocalInParent;//flag for searching variables in the parent (one level up), in other cases only export variables are searched in parents)

	//FUNCTIONS AND PROCEDURES
	std::map<wxString, std::shared_ptr<ibFunction>> m_listFunction; //list of encountered function definitions

	short m_numReturn;//RETURN operator processing mode: RETURN_NONE,RETURN_PROCEDURE,RETURN_FUNCTION

	//LOOPS
	//Service attributes
	unsigned short m_numDoNumber;//nested loop number

	std::map<unsigned short, std::vector<int>*> m_listContinue;//addresses of Continue operators
	std::map<unsigned short, std::vector<int>*> m_listBreak;//addresses of Break operators

	//LABELS
	std::map<wxString, unsigned int> m_listLabelDef; //declarations
	std::vector<std::shared_ptr<ibLabel>> m_listLabel; //list of encountered transitions to labels
};

#endif