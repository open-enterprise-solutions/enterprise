#ifndef _DEBUGGER_DEFS_H__
#define _DEBUGGER_DEFS_H__

enum MessageType
{
	MessageType_Normal = 0,
	MessageType_Warning = 1,
	MessageType_Error = 2,
};

enum CodeState
{
	CodeState_Normal = 0,    // The code is normal.
	CodeState_Unavailable = 1,    // The code for the script was not available.
	CodeState_Binary = 2,    // The code was loaded as a binary/compiled file
};

enum EventId
{
	EventId_SessionStart = 1,   // Sent when the backend is ready to have its initialize function called
	EventId_SessionEnd = 2,    // This is used internally and shouldn't be sent.
	EventId_EnterLoop = 3,     // When enter to loop in enterprise mode 
	EventId_LeaveLoop = 4,      // When leave from loop in enterprise mode 

	EventId_SetToolTip = 5,      // When activate tooltip 
	EventId_StartAutocomplete = 6,      // When activate autocomplete 
	EventId_ShowAutocomplete = 7,      // When activate autocomplete 
	EventId_SetData = 8,      // When set data 

	EventId_MessageFromEnterprise = 9,      // Message from enterprise
};

enum CommandId
{
	CommandId_VerifyConnection = 1, // verify configuration before connection
	CommandId_SetConnectionType = 2, // return if connection not passed verify 

	CommandId_StartSession = 3, // If verify is ok then start debugging 
	CommandId_Continue = 4,    // Continues execution until the next break point.
	CommandId_StepOver = 5,    // Steps to the next line, not entering any functions.
	CommandId_StepInto = 6,    // Steps to the next line, entering any functions.
	CommandId_ToggleBreakpoint = 7,    // Toggles a breakpoint on a line on and off.
	CommandId_RemoveBreakpoint = 8,    // Remove a breakpoint on a line on and off.
	CommandId_Pause = 9,    // Instructs the debugger to break on the next line of script code.
	CommandId_Detach = 10,    // Detaches the debugger from the process.
	CommandId_Destroy = 11,    // Destroy the enterprise from the process.

	CommandId_PatchInsertLine = 12,   // Adds a new line of code.
	CommandId_PatchDeleteLine = 13,   // Deletes a line of code.

	CommandId_PatchComplete = 14,   // Refresh patch line for dataProcessors

	CommandId_DeleteAllBreakpoints = 15,// Instructs the backend to clear all breakpoints set

	CommandId_EnterLoop = 16,// Instructs the backend to enter debug loop
	CommandId_LeaveLoop = 17,// Instructs the backend to leave debug loop

	CommandId_AddExpression = 18, // Get variables from enterprise 
	CommandId_ExpandExpression = 19, // Expand variable from enterprise 
	CommandId_RemoveExpression = 20, // Remove variable from enterprise 

	CommandId_GetArrayBreakpoint = 22, // Wait breakpoits from designer
	CommandId_SetArrayBreakpoint = 23, // Set breakpoits to enterprise

	CommandId_SetStack = 24, // Set up stack in designer 
	CommandId_SetLocalVariables = 25, // Set up local variables to designer 
	CommandId_SetExpressions = 26, //Set up all expressions

	CommandId_EvalToolTip = 27,
	CommandId_EvalAutocomplete = 28,

	CommandId_MessageFromServer = 29 // When catch error in enterprise mode
};

enum ConnectionType {

	ConnectionType_Scanner,
	ConnectionType_Waiter,
	ConnectionType_Debugger,

	ConnectionType_Unknown = 100
};

/////////////////////////////////////////////////////////////////////////////////////////////

struct debugData_t {
	wxString m_fileName;
	wxString m_moduleName;
};

struct debugLineData_t : public debugData_t {
	unsigned int m_line;
};

struct debugExpressionData_t : public debugData_t {
	wxString m_expression;
};

struct debugAutoCompleteData_t : public debugExpressionData_t {

	struct debugVariableData_t {
		wxString m_variableName;
	};

	std::vector<debugVariableData_t> m_arrVar;

	struct debugMethodData_t {
		wxString m_methodName;
		wxString m_methodHelper;
		bool m_methodRet;
	};

	std::vector<debugMethodData_t> m_arrMeth;

public:

	wxString m_keyword;
	int      m_currentPos;
};

///////////////////////////////////////////////////////////////////////////////////////////// 

struct stackData_t {

	struct stackRow_t {
		wxString m_moduleName;
		unsigned int m_moduleLine;
	public:
		stackRow_t(const wxString& strModuleName, unsigned int moduleLine) :
			m_moduleName(strModuleName), m_moduleLine(moduleLine) {
		}
	};

	std::vector<stackRow_t> m_stackData;

public:

	void AppendStack(const wxString& strModuleName, unsigned int moduleLine) {
		m_stackData.emplace_back(
			strModuleName, moduleLine
		);
	}

	unsigned int GetStackCount() const {
		return m_stackData.size();
	}

	wxString GetModuleName(unsigned int idx) const {
		if (idx > m_stackData.size()) return wxEmptyString;
		return m_stackData[idx].m_moduleName;
	}

	wxString GetModuleLine(unsigned int idx) const {
		if (idx > m_stackData.size()) return wxEmptyString;
		return std::to_string(m_stackData[idx].m_moduleLine);
	}
};

/////////////////////////////////////////////////////////////////////////////////////////////

#include <wx/treectrl.h>

struct localWindowData_t {

	struct localWindowItem_t {

		wxString	 m_name;
		wxString	 m_value;
		wxString	 m_type;
		bool		 m_hasAttributes;

		localWindowItem_t(const wxString& n, const wxString& v, const wxString& t, bool a) :
			m_name(n), m_value(v), m_type(t), m_hasAttributes(a) {
		}
	};

	std::vector<localWindowItem_t> m_expressions;

public:

	localWindowData_t() {}

	void AddLocalVar(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes) {
		m_expressions.emplace_back(name, value, type, hasAttributes);
	}

	wxString GetName(unsigned int idx) const {
		return m_expressions[idx].m_name;
	}

	wxString GetValue(unsigned int idx) const {
		return m_expressions[idx].m_value;
	}
	wxString GetType(unsigned int idx) const {
		return m_expressions[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_expressions[idx].m_hasAttributes;
	}

	unsigned int GetVarCount() const {
		return m_expressions.size();
	}
};

struct watchWindowData_t {

	wxTreeItemId m_item;

	struct watchWindowItem_t {

		wxTreeItemId m_item;
		wxString	 m_name;
		wxString	 m_value;
		wxString	 m_type;
		bool		 m_hasAttributes;

		watchWindowItem_t(const wxString& n, const wxString& v, const wxString& t, bool a, const wxTreeItemId& i) :
			m_name(n), m_value(v), m_type(t), m_hasAttributes(a), m_item(i) {
		}
	};

	std::vector<watchWindowItem_t> m_expressions;

public:

	watchWindowData_t(const wxTreeItemId& item = nullptr) : m_item(item) {}

	wxTreeItemId GetItem() const {
		return m_item;
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes) {
		m_expressions.emplace_back(name, value, type, hasAttributes, m_item);
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes, const wxTreeItemId& item) {
		m_expressions.emplace_back(name, value, type, hasAttributes, item);
	}

	wxTreeItemId GetItem(unsigned int idx) const {
		return m_expressions[idx].m_item;
	}

	wxString GetName(unsigned int idx) const {
		return m_expressions[idx].m_name;
	}

	wxString GetValue(unsigned int idx) const {
		return m_expressions[idx].m_value;
	}
	wxString GetType(unsigned int idx) const {
		return m_expressions[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_expressions[idx].m_hasAttributes;
	}

	unsigned int GetWatchCount() const {
		return m_expressions.size();
	}
};

/////////////////////////////////////////////////////////////////////////////////////////////

#define defaultDebuggerPort 1650
#define diapasonDebuggerPort 10
#define defaultHost wxT("localhost")

/////////////////////////////////////////////////////////////////////////////////////////////

#define waitDebuggerTimeout 5

/////////////////////////////////////////////////////////////////////////////////////////////

#endif 