#include "artProvider.h"

wxIMPLEMENT_DYNAMIC_CLASS(wxOESArtModule, wxModule)

wxBitmapBundle wxOESArtProvider::CreateBitmapBundle(const wxArtID& id, const wxArtClient& client, const wxSize size)
{
    // Not one of the bitmaps that we support.
    return wxBitmapBundle();
}