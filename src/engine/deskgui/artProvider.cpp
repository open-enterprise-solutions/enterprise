#include "deskgui/artProvider.h"

#include <wx/bmpbndl.h>
#include <wx/image.h>
#include <wx/mstream.h>

#include "core/picturePredefined.h"
#include "core/pictureRegistry.h"   // ibPictureRegistry — the drawings, by their ids

wxBitmapBundle ibStockArtProvider::BundleOf(const ibPicture& picture, const wxSize& size)
{
	// The largest drawn once, by the core — writing its PNG is most of the cost, so it is written once — and the
	// smaller scaled down from it.
	const ibPicture drawn = picture.Scale(ibSize(size.x * 4, size.y * 4));
	const ibMemoryBuffer& png = drawn.GetData();
	if (png.IsEmpty())
		return wxBitmapBundle();
	if (wxImage::FindHandler(wxBITMAP_TYPE_PNG) == nullptr)
		wxImage::AddHandler(new wxPNGHandler());
	wxMemoryInputStream stream(png.GetData(), png.GetDataLen());
	const wxImage master(stream, wxBITMAP_TYPE_PNG);
	if (!master.IsOk())
		return wxBitmapBundle();

	wxVector<wxBitmap> bitmaps;
	for (const int times : { 1, 2, 4 }) {
		const wxSize at(size.x * times, size.y * times);
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
		{ wxART_GOTO_FIRST, g_picGotoFirstCLSID },
		{ wxART_GOTO_LAST, g_picGotoLastCLSID },
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
		{ wxART_TIP, g_picTipCLSID },

		// …and the product's own whose picture is the core's: a list's add, a query's conditions and grouping (a list's
		// filter and hierarchy), its advanced settings (a composer's settings), the metadata tree's Common (a folder row)
		{ wxART_ADD, g_picAddCLSID },
		{ wxART_FILTER, g_picFilterCLSID },
		{ wxART_GROUPING, g_picHierarchyCLSID },
		{ wxART_ADVANCED, g_picComposerSettingsCLSID },
		{ wxART_COMMON_FOLDER, g_picRowFolderCLSID },
		// …the code editor's translation constructor (a language's picture)
		{ wxART_TRANSLATION_CONSTRUCTOR, g_picTranslationCLSID }
	};

	for (const auto& entry : s_stockPictures) {
		if (entry.id == id)
			return BundleOf(ibPictureRegistry::GetPicture(entry.picture), size.IsFullySpecified() ? size : wxSize(16, 16));
	}
	return wxBitmapBundle();
}
