#include "launcher.h"

#include <wx/artprov.h>

#include "core/graphics/picture.h"      // ibPicture — the web client's globe, drawn here
#include "core/picturePredefined.h"     // g_picApplicationCLSID — the studio opens the application
#include "core/pictureRegistry.h"       // ibPictureRegistry — the core's drawing of it
#include "deskgui/artProvider.h"        // wxART_ADD, wxART_DEBUG, ibStockArtProvider — the desktop's pictures

// The launcher's pictures (see launcher.h). The desktop's, by their ids, wherever it has the same act: a base is added,
// edited, deleted, the launcher is left (the core's), the application is started — run without debugging (the
// theme's) — the studio opens the APPLICATION (the core's, the metadata tree's root). Drawn here: the web client's
// globe, which is its own.

static const wxString s_launcherWeb_svg = wxT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><circle cx='8' cy='8' r='7' fill='#1c6fd1'/><ellipse cx='8' cy='8' rx='2.8' ry='6.3' fill='none' stroke='#ffffff' stroke-width='1.1'/><path d='M1.5 8 H14.5 M2.6 4.6 H13.4 M2.6 11.4 H13.4' fill='none' stroke='#ffffff' stroke-width='1.1' stroke-linecap='butt' stroke-linejoin='round'/></svg>");

wxBitmapBundle ibGetLauncherPicture(ibLauncherPicture which)
{
	const wxSize size(16, 16);
	switch (which) {
		case ibLauncherPicture::Workspace: return wxArtProvider::GetBitmapBundle(wxART_DEBUG_START_WITHOUT_DEBUGGING, wxART_DEBUG, size);
		case ibLauncherPicture::Studio:   return ibStockArtProvider::BundleOf(ibPictureRegistry::GetPicture(g_picApplicationCLSID), size);
		case ibLauncherPicture::Web:        return ibStockArtProvider::BundleOf(ibPicture::FromSvg(s_launcherWeb_svg), size);
		case ibLauncherPicture::Add:        return wxArtProvider::GetBitmapBundle(wxART_ADD, wxART_BUTTON, size);
		case ibLauncherPicture::Edit:       return wxArtProvider::GetBitmapBundle(wxART_EDIT, wxART_BUTTON, size);
		case ibLauncherPicture::Delete:     return wxArtProvider::GetBitmapBundle(wxART_DELETE, wxART_BUTTON, size);
		case ibLauncherPicture::Exit:       return wxArtProvider::GetBitmapBundle(wxART_QUIT, wxART_BUTTON, size);
	}
	return wxBitmapBundle();
}
