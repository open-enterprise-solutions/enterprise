#ifndef _MAINFRAMECHILD_H__
#define _MAINFRAMECHILD_H__

#include <wx/aui/aui.h>
#include <wx/docview.h>
#include <wx/cmdproc.h>

class wxAuiMDIDocChildFrame : 
	public wxDocChildFrameAny<wxAuiMDIChildFrame, wxAuiMDIParentFrame> {
public:

	// default ctor, use Create after it
	wxAuiMDIDocChildFrame() {
	}

	// ctor for a valueForm showing the given view of the specified document
	wxAuiMDIDocChildFrame(wxDocument* doc,
		wxView* view,
		wxAuiMDIParentFrame* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr)) 
		: wxDocChildFrameAny(doc, view,
			parent, id, title, pos, size, style, name)
	{
	}

	virtual ~wxAuiMDIDocChildFrame();

	bool Create(wxDocument* doc,
		wxView* view,
		wxAuiMDIParentFrame* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_FRAME_STYLE,
		const wxString& name = wxASCII_STR(wxFrameNameStr)) 
	{
		return wxDocChildFrameAny::Create
		(
			doc, view,
			parent, id, title, pos, size, style, name
		);
	}

	virtual void SetLabel(const wxString& label) override;

private:

	wxDECLARE_CLASS(wxAuiMDIDocChildFrame);
	wxDECLARE_NO_COPY_CLASS(wxAuiMDIDocChildFrame);
};

class wxDialogDocChildFrame :
	public wxDocChildFrameAny<wxDialog, wxWindow> {
public:
	
	wxDialogDocChildFrame()
	{
	}

	wxDialogDocChildFrame(wxDocument* doc,
		wxView* view,
		wxWindow* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_DIALOG_STYLE,
		const wxString& name = wxASCII_STR(wxDialogNameStr))
		: wxDocChildFrameAny(doc, view,
			parent, id, title, pos, size, style, name)
	{
	}

	bool Create(wxDocument* doc,
		wxView* view,
		wxWindow* parent,
		wxWindowID id,
		const wxString& title,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxDEFAULT_DIALOG_STYLE,
		const wxString& name = wxASCII_STR(wxDialogNameStr))
	{
		return wxDocChildFrameAny::Create
		(
			doc, view,
			parent, id, title, pos, size, style, name
		);
	}

private:

	wxDECLARE_CLASS(wxDialogDocChildFrame);
	wxDECLARE_NO_COPY_CLASS(wxDialogDocChildFrame);
};

#endif 