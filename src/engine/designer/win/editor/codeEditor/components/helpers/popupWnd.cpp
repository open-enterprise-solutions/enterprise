#include "popupWnd.h"

//----------------------------------------------------------------------
// ibOESPopupBase and ibOESPopupWindow

#if wxUSE_POPUPWIN

ibOESPopupBase::ibOESPopupBase(wxWindow* parent)
	: wxPopupWindow(parent, wxPU_CONTAINS_CONTROLS)
{
}

#ifdef __WXGTK__

ibOESPopupBase::~ibOESPopupBase()
{
	wxRect rect = GetRect();
	GetParent()->ScreenToClient(&(rect.x), &(rect.y));
	GetParent()->Refresh(false, &rect);
}

#elif defined(__WXMSW__)

// Do not activate the window when it is shown.
bool ibOESPopupBase::Show(bool show)
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
bool ibOESPopupBase::MSWHandleMessage(WXLRESULT *res, WXUINT msg,
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

ibOESPopupBase::ibOESPopupBase(wxWindow* parent)
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
bool ibOESPopupBase::Show(bool show) override
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
bool ibOESPopupBase::MSWHandleMessage(WXLRESULT *res, WXUINT msg,
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

#elif !wxOES_POPUP_IS_CUSTOM

void ibOESPopupBase::ActivateParent()
{
	// Although we're a valueForm, we always want the parent to be active,
	// so raise it whenever we get shown, focused, etc.
	wxTopLevelWindow *valueForm = wxDynamicCast(
		wxGetTopLevelParent(GetParent()), wxTopLevelWindow);
	if (valueForm)
		valueForm->Raise();
}

bool ibOESPopupBase::Show(bool show)
{
	bool rv = wxFrame::Show(show);
	if (rv && show)
		ActivateParent();

#ifdef __WXOSX__
	GetParent()->Refresh(false);
#endif
}

#endif

#endif // __WXOSX_COCOA__

ibOESPopupWindow::ibOESPopupWindow(wxWindow* parent)
	: ibOESPopupBase(parent), m_lastKnownPosition(wxDefaultPosition)
{
#if !wxOES_POPUP_IS_CUSTOM
	Bind(wxEVT_SET_FOCUS, &ibOESPopupWindow::OnFocus, this);
#endif

	m_tlw = wxDynamicCast(wxGetTopLevelParent(parent), wxTopLevelWindow);
	if (m_tlw)
	{
		m_tlw->Bind(wxEVT_MOVE, &ibOESPopupWindow::OnParentMove, this);
#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!wxOES_POPUP_IS_FRAME)
		m_tlw->Bind(wxEVT_ICONIZE, &ibOESPopupWindow::OnIconize, this);
#endif
	}
}

ibOESPopupWindow::~ibOESPopupWindow()
{
	if (m_tlw)
	{
		m_tlw->Unbind(wxEVT_MOVE, &ibOESPopupWindow::OnParentMove, this);
#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__)&&!wxOES_POPUP_IS_FRAME)
		m_tlw->Unbind(wxEVT_ICONIZE, &ibOESPopupWindow::OnIconize, this);
#endif
	}
}

bool ibOESPopupWindow::Destroy()
{
#if defined(__WXOSX__) && wxOES_POPUP_IS_FRAME && !wxOES_POPUP_IS_CUSTOM
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

bool ibOESPopupWindow::AcceptsFocus() const
{
	return false;
}

void ibOESPopupWindow::DoSetSize(int x, int y, int width, int height, int flags)
{
	m_lastKnownPosition = wxPoint(x, y);

	// convert coords to screen coords since we're a top-level window
	if (x != wxDefaultCoord)
		GetParent()->ClientToScreen(&x, nullptr);

	if (y != wxDefaultCoord)
		GetParent()->ClientToScreen(nullptr, &y);

	ibOESPopupBase::DoSetSize(x, y, width, height, flags);
}

void ibOESPopupWindow::OnParentMove(wxMoveEvent& event)
{
	if (m_lastKnownPosition.IsFullySpecified())
		SetPosition(m_lastKnownPosition);
	event.Skip();
}

#if defined(__WXOSX_COCOA__) || (defined(__WXGTK__) && !wxOES_POPUP_IS_FRAME)

void ibOESPopupWindow::OnIconize(wxIconizeEvent& event)
{
	Show(!event.IsIconized());
}

#elif !wxOES_POPUP_IS_CUSTOM

void ibOESPopupWindow::OnFocus(wxFocusEvent& event)
{
#if wxOES_POPUP_IS_FRAME
	ActivateParent();
#endif

	GetParent()->SetFocus();
	event.Skip();
}

#endif // __WXOSX_COCOA__

ibOESListBoxWin::ibOESListBoxWin(wxWindow* parent, ibListBoxVisualData *visualData, int h)
	: ibOESPopupWindow(parent), m_visualData(visualData)
{
	m_listBox = new ibListBox(this, m_visualData, h);

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
	Bind(wxEVT_PAINT, &ibOESListBoxWin::OnPaint, this);

	SetBackgroundStyle(wxBG_STYLE_PAINT);

	m_listBox->Clear();
}

void ibOESListBoxWin::OnPaint(wxPaintEvent& WXUNUSED(evt))
{
	wxPaintDC dc(this);
	dc.SetBackground(m_visualData->GetBorderColour());
	dc.Clear();
}