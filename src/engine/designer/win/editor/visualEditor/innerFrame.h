#ifndef __INNER_FRAME__
#define __INNER_FRAME__

#include <wx/aui/aui.h>
#include <wx/wx.h>

class CInnerFrame : public wxPanel {
	wxDECLARE_EVENT_TABLE();
private:
	enum {
		NONE,
		RIGHTBOTTOM,
		RIGHT,
		BOTTOM
	} m_sizing;

	int m_curX, m_curY, m_difX, m_difY;
	int m_resizeBorder;
	wxSize m_minSize;
	wxSize m_baseMinSize;

	class TitleBar;

	TitleBar *m_titleBar;
	wxPanel *m_valueFrameContent;

protected:

	virtual wxSize DoGetBestSize() const override;

public:
	CInnerFrame(wxWindow *parent, wxWindowID id,
		const wxPoint &pos = wxDefaultPosition,
		const wxSize &size = wxDefaultSize,
		long style = 0);

	wxPanel *GetFrameContentPanel() const {
		return m_valueFrameContent; 
	}
	
	void OnMouseMotion(wxMouseEvent& e);
	void OnLeftDown(wxMouseEvent& e);
	void OnLeftUp(wxMouseEvent& e);

	void SetTitle(const wxString &title);
	wxString GetTitle();

	void SetTitleStyle(long style);

	void ShowTitleBar(bool show = true);
	void SetToBaseSize();
	bool IsTitleBarShown();
};

BEGIN_DECLARE_EVENT_TYPES()
DECLARE_LOCAL_EVENT_TYPE(wxEVT_INNER_FRAME_RESIZED, -1)
END_DECLARE_EVENT_TYPES()

#define EVT_INNER_FRAME_RESIZED(id, fn) \
  wx__DECLARE_EVT1(wxEVT_INNER_FRAME_RESIZED,id,wxCommandEventHandler(fn))

#endif //__INNER_FRAME__