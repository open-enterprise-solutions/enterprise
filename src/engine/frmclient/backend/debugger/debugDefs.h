#ifndef _DESIGNER_THIN_DEBUGGER_DEFS_H__
#define _DESIGNER_THIN_DEBUGGER_DEFS_H__

// THE DEBUGGER'S VOCABULARY — the engine's (backend/debugger/debugDefs.h), what the copied panes are handed: a stack, the
// local variables, the watch's rows. The debugger itself is the server's (its session's ibDebuggerClient); what it
// answers reaches these by the protocol.
//
// ⚠ A COPY of the engine's structures, as every facade here is: the engine's header is the debugger's wire and lives in
// backend.dll. Its place is the core — one vocabulary for both sides — and the move rebuilds most of backend and
// frontend (debugClient.h and debugServer.h include it); it waits for that word (plan: thin-designer-editors.md §8).

#include <vector>

#include <wx/string.h>
#include <wx/treebase.h>   // wxTreeItemId — the watch's rows are the asker's own

#include "core/fileSystem/types.h"   // u64 — a watch row's id, as the engine's

#define _USE_64_BIT_POINT_IN_DEBUGGER 1

enum ConnectionType {

	ConnectionType_Scanner,
	ConnectionType_Waiter,
	ConnectionType_Debugger,

	ConnectionType_Unknown = 100
};

/////////////////////////////////////////////////////////////////////////////////////////////

struct ibStackData {

	struct ibStackRow {
		wxString m_moduleName;
		unsigned int m_moduleLine;
	public:
		ibStackRow(const wxString& strModuleName, unsigned int moduleLine) :
			m_moduleName(strModuleName), m_moduleLine(moduleLine) {
		}
	};

	std::vector<ibStackRow> m_stackData;

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

struct ibLocalWindowData {

	struct ibLocalWindowItem {

		wxString	 m_name;
		wxString	 m_value;
		wxString	 m_type;
		bool		 m_hasAttributes;

		ibLocalWindowItem(const wxString& n, const wxString& v, const wxString& t, bool a) :
			m_name(n), m_value(v), m_type(t), m_hasAttributes(a) {
		}
	};

	std::vector<ibLocalWindowItem> m_listExpression;

public:

	ibLocalWindowData() {}

	void AddLocalVar(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes) {
		m_listExpression.emplace_back(name, value, type, hasAttributes);
	}

	wxString GetName(unsigned int idx) const {
		return m_listExpression[idx].m_name;
	}

	wxString GetValue(unsigned int idx) const {
		return m_listExpression[idx].m_value;
	}
	wxString GetType(unsigned int idx) const {
		return m_listExpression[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_listExpression[idx].m_hasAttributes;
	}

	unsigned int GetVarCount() const {
		return m_listExpression.size();
	}
};

struct ibWatchWindowData {

	wxTreeItemId m_item;

	struct ibWatchWindowItem {

		wxTreeItemId m_item;
		wxString	 m_name;
		wxString	 m_value;
		wxString	 m_type;
		bool		 m_hasAttributes;

		ibWatchWindowItem(const wxString& n, const wxString& v, const wxString& t, bool a, const wxTreeItemId& i) :
			m_item(i), m_name(n), m_value(v), m_type(t), m_hasAttributes(a) {
		}
	};

	std::vector<ibWatchWindowItem> m_listExpression;

	// WHO ASKED FOR THIS — the engine's: `m_item` is a row of the asker's own tree, so an answer is only meaningful to
	// the one that asked.
	wxString m_bridgeId;

public:

	ibWatchWindowData(const wxTreeItemId& item = nullptr) : m_item(item) {}

	const wxString& GetBridgeId() const { return m_bridgeId; }
	void SetBridgeId(const wxString& id) { m_bridgeId = id; }

	wxTreeItemId GetItem() const {
		return m_item;
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes) {
		m_listExpression.emplace_back(name, value, type, hasAttributes, m_item);
	}

	void AddWatch(const wxString& name, const wxString& value, const wxString& type, bool hasAttributes, const wxTreeItemId& item) {
		m_listExpression.emplace_back(name, value, type, hasAttributes, item);
	}

	wxTreeItemId GetItem(unsigned int idx) const {
		return m_listExpression[idx].m_item;
	}

	wxString GetName(unsigned int idx) const {
		return m_listExpression[idx].m_name;
	}

	wxString GetValue(unsigned int idx) const {
		return m_listExpression[idx].m_value;
	}
	wxString GetType(unsigned int idx) const {
		return m_listExpression[idx].m_type;
	}

	bool HasAttributes(unsigned int idx) const {
		return m_listExpression[idx].m_hasAttributes;
	}

	unsigned int GetWatchCount() const {
		return m_listExpression.size();
	}
};

#endif
