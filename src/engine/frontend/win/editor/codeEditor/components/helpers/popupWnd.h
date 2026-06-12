#ifndef __IB_POPUP_WND_H__
#define __IB_POPUP_WND_H__

#include <wx/wx.h>
#include <wx/stc/stc.h>

#include "listBoxWnd.h"
#include "listBoxVisualData.h"

//----------------------------------------------------------------------
// ibCodeEditorPopupWindow

#if defined(__WXOSX_COCOA__) || defined(__WXMSW__) || defined(__WXGTK__)
#define wxOES_POPUP_IS_CUSTOM 1
#else
#define wxOES_POPUP_IS_CUSTOM 0
#endif

// Define the base class used for ibCodeEditorPopupWindow.
#if wxUSE_POPUPWIN

#include <wx/popupwin.h>
#define wxOES_POPUP_IS_FRAME 0

class ibCodeEditorPopupBase : public wxPopupWindow
{
public:
	ibCodeEditorPopupBase(wxWindow*);
#ifdef __WXGTK__
	virtual ~ibCodeEditorPopupBase();
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

class ibCodeEditorPopupBase :public wxFrame
{
public:
	ibCodeEditorPopupBase(wxWindow*);
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

class ibCodeEditorPopupWindow : public ibCodeEditorPopupBase
{
public:
	ibCodeEditorPopupWindow(wxWindow*);
	virtual ~ibCodeEditorPopupWindow();
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

// A popup window to place the ibCodeEditorListBox upon
class ibCodeEditorListBoxWin : public ibCodeEditorPopupWindow
{
	ibCodeEditorListBox *m_listBox;


public:

	ibCodeEditorListBoxWin(wxWindow*, ibListBoxVisualData *, int);
	ibCodeEditorListBox *GetListBox() const {
		return m_listBox; 
	}

protected:

	void OnPaint(wxPaintEvent&);

private:

	ibListBoxVisualData* m_visualData;
};


#endif 