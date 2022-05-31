#include "popupWnd.h"

//----------------------------------------------------------------------
// wxSTCPopupBase and wxSTCPopupWindow

#ifdef __WXOSX_COCOA__

wxSTCPopupBase::wxSTCPopupBase(wxWindow* parent) :wxNonOwnedWindow()
{
	m_nativeWin = CreateFloatingWindow(this);
	wxNonOwnedWindow::Create(parent, m_nativeWin);
	m_stc = wxDynamicCast(parent, wxStyledTextCtrl);
	m_isShown = false;
	m_cursorSetByPopup = false;
	m_prevCursor = wxSTC_CURSORNORMAL;

	Bind(wxEVT_ENTER_WINDOW, &wxSTCPopupBase::OnMouseEnter, this);
	Bind(wxEVT_LEAVE_WINDOW, &wxSTCPopupBase::OnMouseLeave, this);

	if (m_stc)
		m_stc->Bind(wxEVT_DESTROY, &wxSTCPopupBase::OnParentDestroy, this);
}

wxSTCPopupBase::~wxSTCPopupBase()
{
	UnsubclassWin();
	CloseFloatingWindow(m_nativeWin);

	if (m_stc)
	{
		m_stc->Unbind(wxEVT_DESTROY, &wxSTCPopupBase::OnParentDestroy, this);
		RestoreSTCCursor();
	}
}

bool wxSTCPopupBase::Show(bool show)
{
	if (!wxWindowBase::Show(show))
		return false;

	if (show)
	{
		ShowFloatingWindow(m_nativeWin);

		if (GetRect().Contains(::wxMouseState().GetPosition()))
			SetSTCCursor(wxSTC_CURSORARROW);
	}
	else
	{
		HideFloatingWindow(m_nativeWin);
		RestoreSTCCursor();
	}

	return true;
}

void wxSTCPopupBase::DoSetSize(int x, int y, int width, int ht, int flags)
{
	wxSize oldSize = GetSize();
	wxNonOwnedWindow::DoSetSize(x, y, width, ht, flags);

	if (oldSize != GetSize())
		SendSizeEvent();
}

void wxSTCPopupBase::SetSTCCursor(int cursor)
{
	if (m_stc)
	{
		m_cursorSetByPopup = true;
		m_prevCursor = m_stc->GetSTCCursor();
		m_stc->SetSTCCursor(cursor);
	}
}

void wxSTCPopupBase::RestoreSTCCursor()
{
	if (m_stc != NULL && m_cursorSetByPopup)
		m_stc->SetSTCCursor(m_prevCursor);

	m_cursorSetByPopup = false;
	m_prevCursor = wxSTC_CURSORNORMAL;
}

void wxSTCPopupBase::OnMouseEnter(wxMouseEvent& WXUNUSED(event))
{
	SetSTCCursor(wxSTC_CURSORARROW);
}

void wxSTCPopupBase::OnMouseLeave(wxMouseEvent& WXUNUSED(event))
{
	RestoreSTCCursor();
}

void wxSTCPopupBase::OnParentDestroy(wxWindowDestroyEvent& WXUNUSED(event))
{
	m_stc = NULL;
}

#elif wxUSE_POPUPWIN

wxSTCPopupBase::wxSTCPopupBase(wxWindow* parent)
	: wxPopupWindow(parent, wxPU_CONTAINS_CONTROLS)
{
}

#ifdef __WXGTK__

wxSTCPopupBase::~wxSTCPopupBase()
{
	wxRect rect = GetRect();
	GetParent()->ScreenToClient(&(rect.x), &(rect.y));
	GetParent()->Refresh(false, &rect);
}

#elif defined(__WXMSW__)

// Do not activate the window when it is shown.
bool wxSTCPopupBase::Show(bool show)
{
	if (show) {
		// Check if the window is changing from hidden to shown.
		bool changingVisibility = wxWindowBase::Show(true);

		if (changingVisibility)
		{
			HWND hWnd = reinterpret_cast<HWND>(GetHandle());

			//add drop shadow 
			::SetClassLong(hWnd, GCL_STYLE, ::GetClassLong(hWnd, GCL_STYLE) | CS_DROPSHADOW);

			if (GetName() == wxT("wxSTCCallTip"))
				::AnimateWindow(hWnd, 25, AW_BLEND);
			else
				::ShowWindow(hWnd, SW_SHOWNA);

			::SetWindowPos(hWnd, HWND_NOTOPMOST, 0, 0, 0, 0,
				SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
		}

		return changingVisibility;
	}
	else {
		return wxPopupWindow::Show(false);
	}
}

// Do not activate in response to mouse clicks on this window.
bool wxSTCPopupBase::MSWHandleMessage(WXLRESULT *res, WXUINT msg,
	WXWPARAM wParam, WXLPARAM lParam)
{
	if (msg == WM_MOUSEACTIVATE)
	{
		*res = MA_NOACTIVATE;
		return true;
	}
	else
		return wxPopupWindow::MSWHandleMessage(res, msg, wParam, lParam);
}

#endif // __WXGTK__

#else

wxSTCPopupBase::wxSTCPopupBase(wxWindow* parent)
	:wxFrame(parent, wxID_ANY, wxEmptyString,
		wxDefaultPosition, wxDefaultSize,
		wxFRAME_FLOAT_ON_PARENT | wxBORDER_NONE)
{
#if defined(__WXGTK__)
	gtk_window_set_accept_focus(GTK_WINDOW(this->GetHandle()), FALSE);
#endif
}

#ifdef __WXMSW__

// Use ShowWithoutActivating instead of show.
bool wxSTCPopupBase::Show(bool show) override
{
	if (show)
	{
		if (IsShown())
			return false;
		else
		{
			ShowWithoutActivating();
			return true;
		}
	}
	else
		return wxFrame::Show(false);
}

// Do not activate in response to mouse clicks on this window.
bool wxSTCPopupBase::MSWHandleMessage(WXLRESULT *res, WXUINT msg,
	WXWPARAM wParam, WXLPARAM lParam) override
{
	if (msg == WM_MOUSEACTIVATE)
	{
		*res = MA_NOACTIVATE;
		return true;
	}
	else
		return wxFrame::MSWHandleMessage(res, msg, wParam, lParam);
}

#elif !wxSTC_POPUP_IS_CUSTOM

void wxSTCPopupBase::ActivateParent()
{
	// Although we're a valueForm, we always want the parent to be active,
	// so raise it whenever we get shown, focused, etc.
	wxTopLevelWindow *valueForm = wxDynamicCast(
		wxGetTopLevelParent(GetParent()), wxTopLevelWindow);
	if (valueForm)
		valueForm->Raise();
}

bool wxSTCPopupBase::Show(bool show)
{
	bool rv = wxFrame::Show(show);
	if (rv && show)
		ActivateParent();

#ifdef __WXMAC__
	GetParent()->Refresh(false);
#endif
}

#endif

#endif // __WXOSX_COCOA__

wxSTCPopupWindow::wxSTCPopupWindow(wxWindow* parent)
	: wxSTCPopupBase(parent), m_lastKnownPosition(wxDefaultPosition)
{
#if !wxSTC_POPUP_IS_CUSTOM
	Bind(wxEVT_SET_FOCUS, &wxSTCPopupWindow::OnFocus, this);
#endif

	m_tlw = wxDynamicCast(wxGetTopLevelParent(parent), wxTopLevelWindow);
	if (m_tlw)
	{
		m_tlw->Bind(wxEVT_MOVE, &wxSTCPopupWindow::OnParentMove, this);
#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!wxSTC_POPUP_IS_FRAME)
		m_tlw->Bind(wxEVT_ICONIZE, &wxSTCPopupWindow::OnIconize, this);
#endif
	}
}

wxSTCPopupWindow::~wxSTCPopupWindow()
{
	if (m_tlw)
	{
		m_tlw->Unbind(wxEVT_MOVE, &wxSTCPopupWindow::OnParentMove, this);
#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!wxSTC_POPUP_IS_FRAME)
		m_tlw->Unbind(wxEVT_ICONIZE, &wxSTCPopupWindow::OnIconize, this);
#endif
	}
}

bool wxSTCPopupWindow::Destroy()
{
#if defined(__WXMAC__) && wxSTC_POPUP_IS_FRAME && !wxSTC_POPUP_IS_CUSTOM
	// The bottom edge of this window is not getting properly
	// refreshed upon deletion, so help it out...
	wxWindow* p = GetParent();
	wxRect r(GetPosition(), GetSize());
	r.SetHeight(r.GetHeight() + 1);
	p->Refresh(false, &r);
#endif

	if (!wxPendingDelete.Member(this))
		wxPendingDelete.Append(this);

	return true;
}

bool wxSTCPopupWindow::AcceptsFocus() const
{
	return false;
}

void wxSTCPopupWindow::DoSetSize(int x, int y, int width, int height, int flags)
{
	m_lastKnownPosition = wxPoint(x, y);

	// convert coords to screen coords since we're a top-level window
	if (x != wxDefaultCoord)
		GetParent()->ClientToScreen(&x, NULL);

	if (y != wxDefaultCoord)
		GetParent()->ClientToScreen(NULL, &y);

	wxSTCPopupBase::DoSetSize(x, y, width, height, flags);
}

void wxSTCPopupWindow::OnParentMove(wxMoveEvent& event)
{
	if (m_lastKnownPosition.IsFullySpecified())
		SetPosition(m_lastKnownPosition);
	event.Skip();
}

#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__) && !wxSTC_POPUP_IS_FRAME)

void wxSTCPopupWindow::OnIconize(wxIconizeEvent& event)
{
	Show(!event.IsIconized());
}

#elif !wxSTC_POPUP_IS_CUSTOM

void wxSTCPopupWindow::OnFocus(wxFocusEvent& event)
{
#if wxSTC_POPUP_IS_FRAME
	ActivateParent();
#endif

	GetParent()->SetFocus();
	event.Skip();
}

#endif // __WXOSX_COCOA__

wxSTCListBoxWin::wxSTCListBoxWin(wxWindow* parent, ÑListBoxVisualData *visualData, int h)
	: wxSTCPopupWindow(parent), m_visualData(visualData)
{
	m_listBox = new wxSTCListBox(this, m_visualData, h);

	// Use the background of this window to form a valueForm around the listbox
	// except on macos where the native Scintilla popup has no valueForm.
#ifdef __WXOSX_COCOA__
	const int borderThickness = 0;
#else
	const int borderThickness = FromDIP(1);
#endif

	wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
	bSizer->Add(m_listBox, 1, wxEXPAND | wxALL, borderThickness);
	SetSizer(bSizer);

	m_listBox->SetContainerBorderSize(borderThickness);

	// When drawing highlighting in listctrl style with wxRendererNative on MSW,
	// the colours used seem to be based on the background of the parent window.
	// So manually paint this window to give it the border colour instead of
	// setting the background colour.
	Bind(wxEVT_PAINT, &wxSTCListBoxWin::OnPaint, this);

	SetBackgroundStyle(wxBG_STYLE_PAINT);

	m_listBox->Clear();
}

void wxSTCListBoxWin::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
	wxPaintDC dc(this);
	dc.SetBackground(m_visualData->GetBorderColour());
	dc.Clear();
}