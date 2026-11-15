#include "deskgui/artProvider.h"

#include <wx/bmpbndl.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include "core/picturePredefined.h"
#include "core/pictureRegistry.h"   // ibPictureRegistry — the masters, by their ids

wxBitmapBundle ibStockArtProvider::BundleOf(const wxImage& master, const wxSize& size)
{
	if (!master.IsOk())
		return wxBitmapBundle();

	wxVector<wxBitmap> bitmaps;
	for (const int times : { 1, 2, 4 }) {
		const wxSize at(size.x * times, size.y * times);
		if (times > 1 && (at.x > master.GetWidth() || at.y > master.GetHeight()))
			break;
		bitmaps.push_back(wxBitmap(at == master.GetSize() ? master : master.Scale(at.x, at.y, wxIMAGE_QUALITY_HIGH)));
	}
	return wxBitmapBundle::FromBitmaps(bitmaps);
}

wxBitmapBundle ibStockArtProvider::CreateBitmapBundle(const wxArtID& id, const wxArtClient& WXUNUSED(client),
	const wxSize& size)
{
	static const struct { wxArtID id; ibPictureID picture; } s_stockPictures[] =
	{
		{ wxART_NEW, g_picNewCLSID },
		{ wxART_FILE_OPEN, g_picOpenCLSID },
		{ wxART_FILE_SAVE, g_picSaveCLSID },
		{ wxART_FILE_SAVE_AS, g_picSaveAsCLSID },
		{ wxART_CLOSE, g_picCloseFormCLSID },
		{ wxART_PRINT, g_picPrintCLSID },
		{ wxART_QUIT, g_picQuitCLSID },
		{ wxART_UNDO, g_picUndoCLSID },
		{ wxART_REDO, g_picRedoCLSID },
		{ wxART_CUT, g_picCutCLSID },
		{ wxART_COPY, g_picClipboardCopyCLSID },
		{ wxART_PASTE, g_picPasteCLSID },
		{ wxART_DELETE, g_picDeleteCLSID },
		{ wxART_EDIT, g_picEditCLSID },
		{ wxART_FIND, g_picFindCLSID },
		{ wxART_GO_UP, g_picMoveUpCLSID },
		{ wxART_GO_DOWN, g_picMoveDownCLSID },
		{ wxART_GO_BACK, g_picGoBackCLSID },
		{ wxART_GO_FORWARD, g_picGoForwardCLSID },
		{ wxART_GO_DIR_UP, g_picGoDirUpCLSID },
		{ wxART_PLUS, g_picPlusCLSID },
		{ wxART_MINUS, g_picMinusCLSID },
		{ wxART_NORMAL_FILE, g_picFileCLSID },
		{ wxART_FOLDER, g_picFolderCLSID },
		{ wxART_LIST_VIEW, g_picListViewCLSID },
		{ wxART_REPORT_VIEW, g_picReportViewCLSID },
		{ wxART_FULL_SCREEN, g_picFullScreenCLSID },
		{ wxART_HELP_BOOK, g_picHelpBookCLSID },
		{ wxART_HELP_SETTINGS, g_picHelpSettingsCLSID },
		{ wxART_TICK_MARK, g_picTickMarkCLSID },
		{ wxART_STOP, g_picStopCLSID },
		{ wxART_MISSING_IMAGE, g_picMissingImageCLSID },
		{ wxART_WARNING, g_picWarningCLSID },
		{ wxART_ERROR, g_picErrorCLSID },
		{ wxART_INFORMATION, g_picInformationCLSID },
		{ wxART_QUESTION, g_picQuestionCLSID },
		{ wxART_TIP, g_picTipCLSID }
	};

	for (const auto& entry : s_stockPictures) {
		if (entry.id != id)
			continue;
		const ibPicture picture = ibPictureRegistry::GetPicture(entry.picture);
		const ibMemoryBuffer& png = picture.GetData();
		if (png.GetDataLen() == 0)
			return wxBitmapBundle();
		if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
			wxImage::AddHandler(new wxPNGHandler());
		wxMemoryInputStream stream(png.GetData(), png.GetDataLen());
		return BundleOf(wxImage(stream, wxBITMAP_TYPE_PNG), size.IsFullySpecified() ? size : wxSize(16, 16));
	}
	return wxBitmapBundle();
}
