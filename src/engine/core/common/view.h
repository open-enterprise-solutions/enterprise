#ifndef _VIEW_H_
#define _VIEW_H_

#include <wx/docview.h>

class CView : public wxView
{
public:

	CView() : wxView() {}

	virtual void CreateMenuBar() = 0;
	virtual void DestroyMenuBar() = 0; 

	//////////////////////////////////////////

	virtual void OnActivateView(bool activate,
		wxView *activeView,
		wxView *deactiveView)
	{
		if (activeView == this)
			CreateMenuBar();
		else
			DestroyMenuBar();

		wxView::OnActivateView(activate, activeView, deactiveView); 
	}
	virtual void OnClosingDocument() 
	{
		DestroyMenuBar(); 
	}
};

#endif // !_VIEW_H_
