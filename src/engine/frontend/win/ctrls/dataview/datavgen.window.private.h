/////////////////////////////////////////////////////////////////////////////
// Name:        datavgen.window.private.h
// Purpose:     ibDataViewMainWindow — the inner table-area window owned by
//              ibDataViewCtrl. Forward-declared in datavgen.h (so the public
//              surface stays small) and defined here so the code paths split out
//              of datavgen.cpp can still call its methods. The definition keeps
//              its original .cpp-local layout — wxDECLARE_DYNAMIC_CLASS and the
//              event table stay in datavgen.cpp (out-of-line).
//
//              Private to the datavgen translation units. Do not include from a
//              public header.
/////////////////////////////////////////////////////////////////////////////

#ifndef OES_DATAVGEN_WINDOW_PRIVATE_H
#define OES_DATAVGEN_WINDOW_PRIVATE_H

#include "dataview.h"
#ifdef __WXMSW__
#include <wx/app.h>
#endif

class ibDataViewMainWindow : public wxWindow
{
public:

	// table window variants for scrolling possibilities
	enum ibDataViewWindowType
	{
		ibDataViewWindowNormal = 0,
		ibDataViewWindowFrozenCol = 1,
		ibDataViewWindowFrozenRow = 2,
		ibDataViewWindowFrozenCorner = ibDataViewWindowFrozenCol | ibDataViewWindowFrozenRow
	};

	ibDataViewMainWindow(ibDataViewCtrl* owner,
		ibDataViewWindowType type, int additionalStyle = wxWANTS_CHARS | wxCLIP_CHILDREN,
		const wxString& name = wxT("wxdataviewctrlmainwindow"))
		: m_owner(NULL), m_type(type)
	{
		// We want to use a specific class name for this window in wxMSW to make it
		// possible to configure screen readers to handle it specifically.
#ifdef __WXMSW__
		CreateUsingMSWClass
		(
			wxApp::GetRegisteredClassName
			(
				wxT("ibDataView"),
				-1, // no specific background brush
				0, // no special styles either
				wxApp::RegClass_OnlyNR
			),
			owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, additionalStyle | wxBORDER_NONE, name
		);
#else
		Create(owner, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxBORDER_NONE, name);
#endif

		SetOwner(owner);

		SetBackgroundColour(*wxWHITE);

		SetBackgroundStyle(wxBG_STYLE_PAINT);
	}

	void SetOwner(ibDataViewCtrl* owner) { m_owner = owner; }
	ibDataViewCtrl* GetOwner() { return m_owner; }
	const ibDataViewCtrl* GetOwner() const { return m_owner; }

	ibDataViewModel* GetModel() { return GetOwner()->GetModel(); }
	const ibDataViewModel* GetModel() const { return GetOwner()->GetModel(); }

	virtual wxWindow* GetMainWindowOfCompositeControl() wxOVERRIDE
	{
		return GetOwner();
	}

	virtual void ScrollWindow(int dx, int dy, const wxRect* rect = NULL) wxOVERRIDE;

	ibDataViewWindowType GetType() const { return m_type; }

protected:

	void OnPaint(wxPaintEvent& event);
	void OnCharHook(wxKeyEvent& event);
	void OnChar(wxKeyEvent& event);
	void OnMouse(wxMouseEvent& event);
	void OnSetFocus(wxFocusEvent& event);
	void OnKillFocus(wxFocusEvent& event);

private:

	ibDataViewCtrl* m_owner;

	const ibDataViewWindowType m_type;

	wxDECLARE_DYNAMIC_CLASS(ibDataViewCtrl);
	wxDECLARE_EVENT_TABLE();
};

#endif // OES_DATAVGEN_WINDOW_PRIVATE_H
