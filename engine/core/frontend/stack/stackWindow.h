#if !defined(_STACKBAR_H__)
#define _STACKBAR_H__

#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>

#include "compiler/debugger/debugEvent.h"

class CStackWindow;
class CMemoryReader;

#define stackWindow           (CStackWindow::Get())

class CStackWindow : public wxPanel
{
	wxListCtrl *m_treeCtrl;
	static CStackWindow *s_instance;

	CStackWindow(wxWindow *parent, int id = wxID_ANY);

public:

	static CStackWindow* Get();

	virtual ~CStackWindow();

	void ClearAndCreate();
	void SetStack(CMemoryReader &commandReader);

public:

	class wxStackEvent : public wxEvent {
		struct stack_info {
			wxString m_moduleName;
			unsigned int m_moduleLine;
		};

		std::vector<stack_info> m_stackData;

	public:

		wxStackEvent();

		void AppendStack(const wxString moduleName, unsigned int moduleLine) {
			m_stackData.emplace_back(stack_info{ moduleName, moduleLine });
		}

		unsigned int GetStackCount() const { return m_stackData.size(); }

		wxString GetModuleName(unsigned int idx) const {
			if (idx > m_stackData.size()) return wxEmptyString;
			return m_stackData[idx].m_moduleName;
		}

		wxString GetModuleLine(unsigned int idx) const {
			if (idx > m_stackData.size()) return wxEmptyString;
			return std::to_string(m_stackData[idx].m_moduleLine);
		}

		virtual wxEvent *Clone() const { return new wxStackEvent(*this); }
	};


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
	void OnDebugEvent(wxDebugEvent &event);
	void OnStackEvent(wxStackEvent &event);
	void OnItemSelected(wxListEvent &event);

	/**
	* Called when the window changes size.
	 */
	void OnSize(wxSizeEvent& event);

private:

	float m_columnSize[s_numColumns];

	wxDECLARE_EVENT_TABLE();
};

#endif // !defined(_STACKBAR_H__)
