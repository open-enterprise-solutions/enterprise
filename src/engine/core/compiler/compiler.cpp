////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : compile defines  
////////////////////////////////////////////////////////////////////////////

#include "compiler.h"

#pragma warning(push)
#pragma warning(disable : 4996)

#include <wx/dynlib.h>
#include <wx/msw/dib.h>

wxBitmap wxGetImageBMPFromResource(long id)
{
	const HINSTANCE hInstance = wxDynamicLibrary::MSWGetModuleHandle("core", NULL); 
	wxBitmap bmp;
	HBITMAP hbm = ::LoadBitmap(hInstance, MAKEINTRESOURCE(id));
	wxDIB dib(hbm);
	bmp.CopyFromDIB(dib);
	return bmp;
}

wxImageList *GetImageList()
{
	static bool s_pictureForIcon = false;
	static wxImageList pictureForIcon(16, 16);

	if (!s_pictureForIcon)
	{
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP1), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP2), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP3), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP4), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP5), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP6), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP7), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP8), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP9), wxColour(0, 128, 128));
		pictureForIcon.Add(wxGetImageBMPFromResource(IDB_BITMAP10), wxColour(0, 128, 128));

		s_pictureForIcon = true;
	}

	return &pictureForIcon;
}

#pragma warning(pop)