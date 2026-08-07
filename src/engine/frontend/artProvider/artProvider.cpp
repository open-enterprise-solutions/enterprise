#include "artProvider.h"
#include "backend/backend_picture.h"

#include "artProvider/null/null.xpm"

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
#include "artProvider/codeEditor/formatCode.xpm"

#include "artProvider/designer/designerPage.xpm"
#include "artProvider/designer/codePage.xpm"

#include "artProvider/metadata/commonFolder.xpm"
#include "artProvider/metadata/saveMetadata.xpm"

#include "artProvider/service/message.xpm"
#include "artProvider/service/variables.xpm"
#include "artProvider/service/stack.xpm"
#include "artProvider/service/watch.xpm"

#include "artProvider/service/property.xpm"

// ----------------------------------------------------------------------------
// wxOESArtProvider class
// ----------------------------------------------------------------------------

#include "private/picturePredefined.h"

class wxFrontendArtProvider : public wxArtProvider {
public:
	wxFrontendArtProvider() : wxArtProvider() {}
protected:

	virtual wxBitmapBundle CreateBitmapBundle(
		const wxArtID& id,
		const wxArtClient& client,
		const wxSize& size) override {

		static const struct wxFrontendArtProviderIconEntry {

			struct wxFrontendArtProviderIconData {

				wxFrontendArtProviderIconData(const char* data[]) :
					m_data(data), m_len(0) {
					unsigned int idx = 0;
					while (m_data[idx++] != nullptr) {
						m_len += sizeof(m_data[idx]);
					}
				}

				const char** m_data;
				size_t m_len;
			};

			wxArtClient client;
			wxArtID id;
			wxFrontendArtProviderIconData data;
		}

		s_allBitmaps[] =
		{
			// ******* wxART_AUTOCOMPLETE *******
			{ wxART_AUTOCOMPLETE, wxART_FUNCTION_RED, s_functionBlue_xpm},
			{ wxART_AUTOCOMPLETE, wxART_FUNCTION_BLUE, s_functionRed_xpm },
			{ wxART_AUTOCOMPLETE, wxART_PROCEDURE_RED, s_procedureRed_xpm },
			{ wxART_AUTOCOMPLETE, wxART_PROCEDURE_BLUE, s_procedureBlue_xpm },
			{ wxART_AUTOCOMPLETE, wxART_VARIABLE, s_variable_xpm },
			{ wxART_AUTOCOMPLETE, wxART_VARIABLE_ALTERNATIVE, s_variable_alt_xpm },

			// ******* wxART_DOC_MODULE *******
			{ wxART_DOC_MODULE, wxART_ADD_COMMENT, s_addComment_xpm },
			{ wxART_DOC_MODULE, wxART_REMOVE_COMMENT, s_removeComment_xpm },
			{ wxART_DOC_MODULE, wxART_SYNTAX_CONTROL, s_syntaxControl_xpm },
			{ wxART_DOC_MODULE, wxART_GOTO_LINE, s_gotoLine_xpm },
			{ wxART_DOC_MODULE, wxART_PROC_AND_FUNC, s_proceduresFunctions_xpm },
			{ wxART_DOC_MODULE, wxART_FORMAT_CODE, s_formatCode_xpm },

			// ******* wxART_DOC_FORM *******
			{ wxART_DOC_FORM, wxART_DESIGNER_PAGE, s_designerPage_xpm },
			{ wxART_DOC_FORM, wxART_CODE_PAGE, s_codePage_xpm },

			// ******* wxART_DOC_TEMPLATE *******
			{ wxART_DOC_TEMPLATE, wxART_MERGE_CELL , s_mergeCells_xpm },
			{ wxART_DOC_TEMPLATE, wxART_ADD_SECTION , s_addSection_xpm },
			{ wxART_DOC_TEMPLATE, wxART_REMOVE_SECTION , s_removeSection_xpm },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_CELL, s_showCells_xpm },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_HEADER, s_showHeaders_xpm },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_SECTION, s_showSections_xpm },
			{ wxART_DOC_TEMPLATE, wxART_BORDER, s_borders_xpm },
			{ wxART_DOC_TEMPLATE, wxART_DOCK_TABLE , s_dockTable_xpm },

			// ******* wxART_SERVICE *******
			{ wxART_SERVICE, wxART_MESSAGE, s_message_xpm },
			{ wxART_SERVICE, wxART_LOCAL_VARIABLE, s_variables_xpm },
			{ wxART_SERVICE, wxART_STACK, s_stack_xpm },
			{ wxART_SERVICE, wxART_WATCH, s_watch_xpm },

			{ wxART_SERVICE, wxART_PROPERTY, s_property_xpm },

			// ******* wxART_METATREE *******
			{ wxART_METATREE, wxART_COMMON_FOLDER, s_commonFolder_xpm },
			{ wxART_METATREE, wxART_SAVE_METADATA, s_saveMetadata_xpm }
		};

		for (unsigned n = 0; n < WXSIZEOF(s_allBitmaps); n++) {
			const wxFrontendArtProviderIconEntry& entry = s_allBitmaps[n];
			if (entry.id != id)
				continue;

			return wxIcon(entry.data.m_data);
		}

		if (client == wxART_FRONTEND) {

			if (id == wxART_DATABASE)
				return ibBackendPicture::GetImageFromBase64(s_db_32_png, size);
			else if (id == wxART_DATABASE_ROOLBACK)
				return ibBackendPicture::GetImageFromBase64(s_db_rollback_32_png, size);
			else if (id == wxART_DATABASE_APPLY)
				return ibBackendPicture::GetImageFromBase64(s_db_apply_32_png, size);
			else if (id == wxART_ADD)
				return ibBackendPicture::GetImageFromBase64(s_add_32_png, size);
			else if (id == wxART_EDIT)
				return ibBackendPicture::GetImageFromBase64(s_edit_32_png, size);
			else if (id == wxART_DELETE)
				return ibBackendPicture::GetImageFromBase64(s_delete_32_png, size);
			else if (id == wxART_UP)
				return ibBackendPicture::GetImageFromBase64(s_up_32_png, size);
			else if (id == wxART_DOWN)
				return ibBackendPicture::GetImageFromBase64(s_down_32_png, size);
			else if (id == wxART_SORT)
				return ibBackendPicture::GetImageFromBase64(s_sort_32_png, size);
			else if (id == wxART_QUERY_CONSTRUCTOR)
				return ibBackendPicture::GetImageFromBase64(s_query_constructor_32_png, size);
			else if (id == wxART_TEMP_TABLE)
				return ibBackendPicture::GetImageFromBase64(s_temp_table_32_png, size);
			else if (id == wxART_NESTED_QUERY)
				return ibBackendPicture::GetImageFromBase64(s_nested_query_32_png, size);

			return wxNullBitmap;
		}

		return wxNullBitmap;
	}

private:
	wxDECLARE_NO_COPY_CLASS(wxFrontendArtProvider);
};

#include <wx/module.h>

class wxFrontendModule : public wxModule
{
public:
	wxFrontendModule() : wxModule() {}
	virtual bool OnInit() {
		wxArtProvider::Push(new wxFrontendArtProvider);
		return true;
	}
	virtual void OnExit() {}
private:
	wxDECLARE_DYNAMIC_CLASS(wxFrontendModule);
};

wxIMPLEMENT_DYNAMIC_CLASS(wxFrontendModule, wxModule);