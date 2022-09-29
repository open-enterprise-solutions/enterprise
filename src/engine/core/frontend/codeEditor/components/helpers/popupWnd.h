#ifndef _POPUPWND_H__
#define _POPUPWND_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "listBoxWnd.h"
#include "listBoxVisualData.h"

//----------------------------------------------------------------------
// COESPopupWindow

#if defined(__WXOSX_COCOA__) || defined(__WXMSW__) || defined(__WXGTK__)
#define wxOES_POPUP_IS_CUSTOM 1
#else
#define wxOES_POPUP_IS_CUSTOM 0
#endif

// Define the base class used for COESPopupWindow.
#ifdef __WXOSX_COCOA__

#include <wx/nonownedwnd.h>
#define wxOES_POPUP_IS_FRAME 0

class COESPopupBase :public wxNonOwnedWindow
{
public:
	COESPopupBase(wxWindow*);
	virtual ~COESPopupBase();
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
#define wxOES_POPUP_IS_FRAME 0

class COESPopupBase : public wxPopupWindow
{
public:
	COESPopupBase(wxWindow*);
#ifdef __WXGTK__
	virtual ~COESPopupBase();
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

class COESPopupBase :public wxFrame
{
public:
	COESPopupBase(wxWindow*);
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

class COESPopupWindow : public COESPopupBase
{
public:
	COESPopupWindow(wxWindow*);
	virtual ~COESPopupWindow();
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

// A popup window to place the COESListBox upon
class COESListBoxWin : public COESPopupWindow
{
	COESListBox *m_listBox;


public:

	COESListBoxWin(wxWindow*, ÑListBoxVisualData *, int);
	COESListBox *GetListBox() const {
		return m_listBox; 
	}

protected:

	void OnPaint(wxPaintEvent&);

private:

	ÑListBoxVisualData* m_visualData;
};


#endif 