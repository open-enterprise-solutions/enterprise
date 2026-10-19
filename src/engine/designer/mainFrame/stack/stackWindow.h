#if !defined(_STACKBAR_H__)
#define _STACKBAR_H__

#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>

#include "frontend/frontend.h"
#include "backend/debugger/debugDefs.h"

#define stackWindow ibStackWindow::GetStackWindow()

class ibStackWindow : public wxPanel {
	wxListCtrl* m_treeCtrl;
private:
	ibStackWindow(wxWindow* parent, int id = wxID_ANY);
	friend class ibFrontendMainFrameDesigner;
public:

	virtual ~ibStackWindow();

	static ibStackWindow* GetStackWindow();

	void ClearAndCreate();
	void SetStack(const ibStackData& stackData);

protected:

	// The line a narrow column, the module the rest of the width.
	void FitColumns();

	//events
	void OnItemSelected(wxListEvent& event);

	/**
	* Called when the window changes size.
	 */
	void OnSize(wxSizeEvent& event);

private:
	wxDECLARE_EVENT_TABLE();
};

#endif // !defined(_STACKBAR_H__)
