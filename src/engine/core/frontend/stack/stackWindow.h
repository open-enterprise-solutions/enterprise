#if !defined(_STACKBAR_H__)
#define _STACKBAR_H__

#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>

#include "core/compiler/debugger/debugEvent.h"

struct stackData_t {

	struct stackRow_t {
		wxString m_moduleName;
		unsigned int m_moduleLine;
	public:
		stackRow_t(const wxString& moduleName, unsigned int moduleLine) :
			m_moduleName(moduleName), m_moduleLine(moduleLine) {
		}
	};

	std::vector<stackRow_t> m_stackData;

public:

	void AppendStack(const wxString& moduleName, unsigned int moduleLine) {
		m_stackData.emplace_back(
			moduleName, moduleLine
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

#define stackWindow CStackWindow::GetStackWindow()

class CStackWindow : public wxPanel {
	wxListCtrl* m_treeCtrl;
private:
	CStackWindow(wxWindow* parent, int id = wxID_ANY);
	friend class wxAuiDocDesignerMDIFrame;
public:

	virtual ~CStackWindow();

	static CStackWindow* GetStackWindow();

	void ClearAndCreate();
	void SetStack(const stackData_t& stackData);

protected:

	static const unsigned int s_numColumns = 2;

	/**
	 * Updates the variable that stores the proprotion of the first column
	 * relative to the width of the control.
	 */
	void UpdateColumnSizes();

	/**
	* Gets the size of each of the columns in pixels.
	*/
	void GetColumnSizes(int totalSize, int columnSize[s_numColumns]) const;

	//events 
	void OnDebugEvent(wxDebugEvent& event);
	void OnItemSelected(wxListEvent& event);

	/**
	* Called when the window changes size.
	 */
	void OnSize(wxSizeEvent& event);

private:
	float m_columnSize[s_numColumns];
	wxDECLARE_EVENT_TABLE();
};

#endif // !defined(_STACKBAR_H__)
