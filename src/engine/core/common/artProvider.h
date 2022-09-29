#ifndef _APPBITMAPS_H__
#define _APPBITMAPS_H__

#include <wx/artprov.h>
#include <wx/module.h>

class wxOESArtProvider : public wxArtProvider
{
public:
	wxOESArtProvider() : wxArtProvider() {}
protected:
	wxBitmapBundle CreateBitmapBundle(const wxArtID& id,
		const wxArtClient& WXUNUSED(client),
		const wxSize WXUNUSED(size));
private:
	wxDECLARE_NO_COPY_CLASS(wxOESArtProvider);
};

class wxOESArtModule : public wxModule
{
public:
	wxOESArtModule() : wxModule() { }
	virtual bool OnInit() {
		wxArtProvider::Push(new wxOESArtProvider);
		return true;
	}
	virtual void OnExit() {}
private:
	wxDECLARE_DYNAMIC_CLASS(wxOESArtModule);
};

#endif
