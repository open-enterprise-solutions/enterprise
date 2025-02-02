#if !defined(_LOC_BAR_H__)
#define _LOC_BAR_H__

#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>

#include "frontend/frontend.h"
#include "backend/debugger/debugDefs.h"

#define localWindow CLocalWindow::GetLocalWindow()

class CLocalWindow : public wxPanel {
	wxListCtrl* m_treeCtrl;
private:
	CLocalWindow(wxWindow* parent, int id = wxID_ANY);
	friend class CDocDesignerMDIFrame;
public:

	virtual ~CLocalWindow();

	static CLocalWindow* GetLocalWindow();

	void ClearAndCreate();
	void SetLocalVariable(const localWindowData_t& locData);

protected:

	static const unsigned int s_numColumns = 3;

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
	//void OnDebugEvent(wxDebugEvent& event);
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
