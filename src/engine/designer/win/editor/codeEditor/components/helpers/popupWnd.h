#ifndef _POPUPWND_H__
#define _POPUPWND_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "listBoxWnd.h"
#include "listBoxVisualData.h"

//----------------------------------------------------------------------
// ibOESPopupWindow

#if defined(__WXOSX_COCOA__) || defined(__WXMSW__) || defined(__WXGTK__)
#define wxOES_POPUP_IS_CUSTOM 1
#else
#define wxOES_POPUP_IS_CUSTOM 0
#endif

// Define the base class used for ibOESPopupWindow.
#if wxUSE_POPUPWIN

#include <wx/popupwin.h>
#define wxOES_POPUP_IS_FRAME 0

class ibOESPopupBase : public wxPopupWindow
{
public:
	ibOESPopupBase(wxWindow*);
#ifdef __WXGTK__
	virtual ~ibOESPopupBase();
#elif defined(__WXMSW__)
	virtual bool Show(bool show = true) override;
	virtual bool MSWHandleMessage(WXLRESULT *result, WXUINT message,
		WXWPARAM wParam, WXLPARAM lParam)
		override;
#endif
};

#else

#include <wx/valueForm.h>
#define wxOES_POPUP_IS_FRAME 1

class ibOESPopupBase :public wxFrame
{
public:
	ibOESPopupBase(wxWindow*);
#ifdef __WXMSW__
	virtual bool Show(bool show = true) override;
	virtual bool MSWHandleMessage(WXLRESULT *result, WXUINT message,
		WXWPARAM wParam, WXLPARAM lParam)
		override;
#elif !wxOES_POPUP_IS_CUSTOM
	virtual bool Show(bool show = true) override;
	void ActivateParent();
#endif
};

#endif // __WXOSX_COCOA__

class ibOESPopupWindow : public ibOESPopupBase
{
public:
	ibOESPopupWindow(wxWindow*);
	virtual ~ibOESPopupWindow();
	virtual bool Destroy() override;
	virtual bool AcceptsFocus() const override;

protected:
	virtual void DoSetSize(int x, int y, int width, int height,
		int sizeFlags = wxSIZE_AUTO) override;
	void OnParentMove(wxMoveEvent& event);
#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!wxOES_POPUP_IS_FRAME)
	void OnIconize(wxIconizeEvent& event);
#elif !wxOES_POPUP_IS_CUSTOM
	void OnFocus(wxFocusEvent& event);
#endif

private:
	wxPoint   m_lastKnownPosition;
	wxWindow* m_tlw;
};

// A popup window to place the ibListBox upon
class ibOESListBoxWin : public ibOESPopupWindow
{
	ibListBox *m_listBox;


public:

	ibOESListBoxWin(wxWindow*, ibListBoxVisualData *, int);
	ibListBox *GetListBox() const {
		return m_listBox; 
	}

protected:

	void OnPaint(wxPaintEvent&);

private:

	ibListBoxVisualData* m_visualData;
};


#endif 