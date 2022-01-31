#ifndef _POPUPWND_H__
#define _POPUPWND_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "listBoxWnd.h"
#include "listBoxVisualData.h"

//----------------------------------------------------------------------
// wxSTCPopupWindow

#if defined(__WXOSX_COCOA__) || defined(__WXMSW__) || defined(__WXGTK__)
#define wxSTC_POPUP_IS_CUSTOM 1
#else
#define wxSTC_POPUP_IS_CUSTOM 0
#endif

// Define the base class used for wxSTCPopupWindow.
#ifdef __WXOSX_COCOA__

#include <wx/nonownedwnd.h>
#define wxSTC_POPUP_IS_FRAME 0

class wxSTCPopupBase :public wxNonOwnedWindow
{
public:
	wxSTCPopupBase(wxWindow*);
	virtual ~wxSTCPopupBase();
	virtual bool Show(bool show = true) override;

protected:
	virtual void DoSetSize(int, int, int, int, int) override;
	void SetSTCCursor(int);
	void RestoreSTCCursor();
	void OnMouseEnter(wxMouseEvent&);
	void OnMouseLeave(wxMouseEvent&);
	void OnParentDestroy(wxWindowDestroyEvent& event);

private:
	WX_NSWindow       m_nativeWin;
	wxStyledTextCtrl* m_stc;
	bool              m_cursorSetByPopup;
	int               m_prevCursor;
};

#elif wxUSE_POPUPWIN

#include <wx/popupwin.h>
#define wxSTC_POPUP_IS_FRAME 0

class wxSTCPopupBase : public wxPopupWindow
{
public:
	wxSTCPopupBase(wxWindow*);
#ifdef __WXGTK__
	virtual ~wxSTCPopupBase();
#elif defined(__WXMSW__)
	virtual bool Show(bool show = true) override;
	virtual bool MSWHandleMessage(WXLRESULT *result, WXUINT message,
		WXWPARAM wParam, WXLPARAM lParam)
		override;
#endif
};

#else

#include <wx/valueForm.h>
#define wxSTC_POPUP_IS_FRAME 1

class wxSTCPopupBase :public wxFrame
{
public:
	wxSTCPopupBase(wxWindow*);
#ifdef __WXMSW__
	virtual bool Show(bool show = true) override;
	virtual bool MSWHandleMessage(WXLRESULT *result, WXUINT message,
		WXWPARAM wParam, WXLPARAM lParam)
		override;
#elif !wxSTC_POPUP_IS_CUSTOM
	virtual bool Show(bool show = true) override;
	void ActivateParent();
#endif
};

#endif // __WXOSX_COCOA__


class wxSTCPopupWindow : public wxSTCPopupBase
{
public:
	wxSTCPopupWindow(wxWindow*);
	virtual ~wxSTCPopupWindow();
	virtual bool Destroy() override;
	virtual bool AcceptsFocus() const override;

protected:
	virtual void DoSetSize(int x, int y, int width, int height,
		int sizeFlags = wxSIZE_AUTO) override;
	void OnParentMove(wxMoveEvent& event);
#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!wxSTC_POPUP_IS_FRAME)
	void OnIconize(wxIconizeEvent& event);
#elif !wxSTC_POPUP_IS_CUSTOM
	void OnFocus(wxFocusEvent& event);
#endif

private:
	wxPoint   m_lastKnownPosition;
	wxWindow* m_tlw;
};

// A popup window to place the wxSTCListBox upon
class wxSTCListBoxWin : public wxSTCPopupWindow
{
	wxSTCListBox *m_listBox;


public:

	wxSTCListBoxWin(wxWindow*, ÑListBoxVisualData *, int);
	wxSTCListBox *GetListBox() { return m_listBox; }

protected:

	void OnPaint(wxPaintEvent&);

private:

	ÑListBoxVisualData* m_visualData;
};


#endif 