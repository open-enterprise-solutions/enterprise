#ifndef _MAINFRAMECHILD_H__
#define _MAINFRAMECHILD_H__

#include <wx/wx.h>
#include <wx/aui/aui.h>
#include <wx/docview.h>

class CDocChildFrame : public wxAuiMDIChildFrame,
	public wxDocChildFrameAnyBase
{

public:
	
	// default ctor, use Create after it
	CDocChildFrame();

	// ctor for a valueForm showing the given view of the specified document
	CDocChildFrame(wxDocument *doc,
		wxView *view,
		wxAuiMDIParentFrame *parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr));

	virtual ~CDocChildFrame();

	bool Create(wxDocument *doc,
		wxView *view,
		wxAuiMDIParentFrame *parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr));

	virtual void SetLabel(const wxString& label) override;

protected:

	// hook the child view into event handlers chain here
	virtual bool TryBefore(wxEvent& event) override;

private:

	void OnActivate(wxActivateEvent& event);
	void OnCloseWindow(wxCloseEvent& event);

	wxDECLARE_NO_COPY_CLASS(CDocChildFrame);
};

#endif 