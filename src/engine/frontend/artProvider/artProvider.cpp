#include "artProvider.h"

#include <map>

#include "artProvider/codeEditor/intelli/functionBlue.xpm"
#include "artProvider/codeEditor/intelli/functionRed.xpm"
#include "artProvider/codeEditor/intelli/procedureBlue.xpm"
#include "artProvider/codeEditor/intelli/procedureRed.xpm"
#include "artProvider/codeEditor/intelli/variable.xpm"
#include "artProvider/codeEditor/intelli/variableAlt.xpm"

#include "artProvider/template/mergeCells.xpm"
#include "artProvider/template/addSection.xpm"
#include "artProvider/template/removeSection.xpm"
#include "artProvider/template/showCells.xpm"
#include "artProvider/template/showHeaders.xpm"
#include "artProvider/template/showSections.xpm"
#include "artProvider/template/borders.xpm"
#include "artProvider/template/dockTable.xpm"

#include "artProvider/codeEditor/addComment.xpm"
#include "artProvider/codeEditor/removeComment.xpm"
#include "artProvider/codeEditor/syntaxControl.xpm"
#include "artProvider/codeEditor/gotoLine.xpm"
#include "artProvider/codeEditor/proceduresFunctions.xpm"

#include "artProvider/designer/designerPage.xpm"
#include "artProvider/designer/codePage.xpm"

#include "artProvider/metaData/commonFolder.xpm"
#include "artProvider/metaData/saveMetadata.xpm"

// ----------------------------------------------------------------------------
// wxOESArtProvider class
// ----------------------------------------------------------------------------

class wxFrontendArtProvider : public wxArtProvider {
	std::map<
		std::pair<wxArtClient, wxArtID>, wxIcon
	> m_dataIcon;
public:
	wxFrontendArtProvider() : wxArtProvider() {
		m_dataIcon = {
			{ { wxART_AUTOCOMPLETE, wxART_FUNCTION_RED }, s_functionBlue_xpm },
			{ { wxART_AUTOCOMPLETE, wxART_FUNCTION_BLUE }, s_functionRed_xpm },
			{ { wxART_AUTOCOMPLETE, wxART_PROCEDURE_RED }, s_procedureRed_xpm },
			{ { wxART_AUTOCOMPLETE, wxART_PROCEDURE_BLUE }, s_procedureBlue_xpm },
			{ { wxART_AUTOCOMPLETE, wxART_VARIABLE }, s_variable_xpm },
			{ { wxART_AUTOCOMPLETE, wxART_VARIABLE_ALTERNATIVE }, s_variable_alt_xpm },

			{ { wxART_DOC_MODULE, wxART_ADD_COMMENT }, s_addComment_xpm },
			{ { wxART_DOC_MODULE, wxART_REMOVE_COMMENT }, s_removeComment_xpm },
			{ { wxART_DOC_MODULE, wxART_SYNTAX_CONTROL }, s_syntaxControl_xpm },
			{ { wxART_DOC_MODULE, wxART_GOTO_LINE }, s_gotoLine_xpm },
			{ { wxART_DOC_MODULE, wxART_PROC_AND_FUNC }, s_proceduresFunctions_xpm },

			{ { wxART_DOC_FORM, wxART_DESIGNER_PAGE }, s_designerPage_xpm },
			{ { wxART_DOC_FORM, wxART_CODE_PAGE }, s_codePage_xpm },

			{ { wxART_DOC_TEMPLATE, wxART_MERGE_CELL }, s_mergeCells_xpm },
			{ { wxART_DOC_TEMPLATE, wxART_ADD_SECTION }, s_addSection_xpm },
			{ { wxART_DOC_TEMPLATE, wxART_REMOVE_SECTION }, s_removeSection_xpm },
			{ { wxART_DOC_TEMPLATE, wxART_SHOW_CELL }, s_showCells_xpm },
			{ { wxART_DOC_TEMPLATE, wxART_SHOW_HEADER }, s_showHeaders_xpm },
			{ { wxART_DOC_TEMPLATE, wxART_SHOW_SECTION }, s_showSections_xpm },
			{ { wxART_DOC_TEMPLATE, wxART_BORDER }, s_borders_xpm },
			{ { wxART_DOC_TEMPLATE, wxART_DOCK_TABLE }, s_dockTable_xpm },

			{ { wxART_METATREE, wxART_COMMON_FOLDER }, s_commonFolder_xpm },
			{ { wxART_METATREE, wxART_SAVE_METADATA }, s_saveMetadata_xpm }
		};
	}
protected:
	virtual wxBitmap CreateBitmap(
		const wxArtID& id,
		const wxArtClient& client,
		const wxSize& size) override {
		auto foundedIconIt = m_dataIcon.find(
			std::pair<wxArtClient, wxArtID>(client, id)
		);
		if (foundedIconIt != m_dataIcon.end())
			return foundedIconIt->second;
		// Not one of the bitmaps that we support.
		return wxBitmap();
	}

	virtual wxIconBundle CreateIconBundle(const wxArtID& WXUNUSED(id),
		const wxArtClient& WXUNUSED(client))
	{
		return wxNullIconBundle;
	}
private:
	wxDECLARE_NO_COPY_CLASS(wxFrontendArtProvider);
};

#include <wx/module.h>

class wxOESArtModule : public wxModule
{
public:
	wxOESArtModule() : wxModule() { }
	virtual bool OnInit() {
		wxArtProvider::Push(new wxFrontendArtProvider);
		return true;
	}
	virtual void OnExit() {}
private:
	wxDECLARE_DYNAMIC_CLASS(wxOESArtModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxOESArtModule, wxModule)