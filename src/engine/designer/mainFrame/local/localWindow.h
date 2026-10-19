#if !defined(_LOC_BAR_H__)
#define _LOC_BAR_H__

#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>

#include "frontend/frontend.h"
#include "backend/debugger/debugDefs.h"

#define localWindow ibLocalWindow::GetLocalWindow()

class ibLocalWindow : public wxPanel {
	wxListCtrl* m_treeCtrl;
private:
	ibLocalWindow(wxWindow* parent, int id = wxID_ANY);
	friend class ibFrontendMainFrameDesigner;
public:

	virtual ~ibLocalWindow();

	static ibLocalWindow* GetLocalWindow();

	void ClearAndCreate();
	void SetLocalVariable(const ibLocalWindowData& locData);

protected:

	// The name a narrow column, the value half the width, the type the rest.
	void FitColumns();

	//events
	//void OnDebugEvent(wxDebugEvent& event);
	void OnItemSelected(wxListEvent& event);

	/**
	* Called when the window changes size.
	 */
	void OnSize(wxSizeEvent& event);

private:
	wxDECLARE_EVENT_TABLE();
};

#endif // !defined(_STACKBAR_H__)
