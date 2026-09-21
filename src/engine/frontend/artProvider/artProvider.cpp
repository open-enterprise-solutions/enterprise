#include "artProvider.h"

#include "backend/backend_picture.h"   // ibBackendPicture::GetBitmapFromBase64

// EVERY PICTURE HERE IS A PNG IN BASE64, one string each in private/picturePredefined.h: a master four times the
// size of its drawing, scaled down here to the size asked. Its source is an SVG in tools/pictures/render.js, which
// draws the PNG and writes the string; the groups are the debugger, the code editor and its autocomplete, the form
// designer, the metadata tree, the service panes and the output window, the spreadsheet, the list commands and the
// query constructor, and wx's own stock ids.
#include "private/picturePredefined.h"

#include <wx/bmpbndl.h>

// ----------------------------------------------------------------------------
// wxOESArtProvider class
// ----------------------------------------------------------------------------

class wxFrontendArtProvider : public wxArtProvider {
public:
	wxFrontendArtProvider() : wxArtProvider() {}
protected:

	virtual wxBitmapBundle CreateBitmapBundle(
		const wxArtID& id,
		const wxArtClient& client,
		const wxSize& size) override {

		// The pictures: a PNG master in Base64, scaled down to the size asked for (16x16 when none is).
		static const struct { wxArtClient client; wxArtID id; const wxString& png; } s_allPictures[] =
		{
			// ******* wxART_VISUALHOST *******
			{ wxART_VISUALHOST, wxART_NO_PICTURE, s_null_png },

			// ******* wxART_FRONTEND ******* - moving a row is the stock arrow's act, so it is the stock arrow
			// (wxART_EDIT and wxART_DELETE are stock ids themselves and are answered by the stock table below)
			{ wxART_FRONTEND, wxART_ADD, s_add_png },
			{ wxART_FRONTEND, wxART_UP, s_goUp_png },
			{ wxART_FRONTEND, wxART_DOWN, s_goDown_png },
			{ wxART_FRONTEND, wxART_SORT, s_sort_png },
			{ wxART_FRONTEND, wxART_DATABASE, s_db_png },
			{ wxART_FRONTEND, wxART_DATABASE_APPLY, s_dbApply_png },
			{ wxART_FRONTEND, wxART_DATABASE_ROOLBACK, s_dbRollback_png },
			{ wxART_FRONTEND, wxART_QUERY_CONSTRUCTOR, s_queryConstructor_png },
			{ wxART_FRONTEND, wxART_TEMP_TABLE, s_tempTable_png },
			{ wxART_FRONTEND, wxART_NESTED_QUERY, s_nestedQuery_png },
			{ wxART_FRONTEND, wxART_TRANSLATION_CONSTRUCTOR, s_translationConstructor_png },
			{ wxART_FRONTEND, wxART_FORMAT_CONSTRUCTOR, s_formatConstructor_png },
			{ wxART_FRONTEND, wxART_LINQ_CONSTRUCTOR, s_linqConstructor_png },
			{ wxART_DEBUG, wxART_BREAKPOINT_CONDITION, s_breakpointCondition_png },
			{ wxART_DOC_MODULE, wxART_SELECT_ALL, s_selectAll_png },

			// ******* wxART_DOC_FORM *******
			{ wxART_DOC_FORM, wxART_DESIGNER_PAGE, s_designerPage_png },
			{ wxART_DOC_FORM, wxART_CODE_PAGE, s_codePage_png },

			// ******* wxART_DOC_TEMPLATE *******
			{ wxART_DOC_TEMPLATE, wxART_MERGE_CELL, s_mergeCells_png },
			{ wxART_DOC_TEMPLATE, wxART_ADD_SECTION, s_addSection_png },
			{ wxART_DOC_TEMPLATE, wxART_REMOVE_SECTION, s_removeSection_png },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_CELL, s_showCells_png },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_HEADER, s_showHeaders_png },
			{ wxART_DOC_TEMPLATE, wxART_SHOW_SECTION, s_showSections_png },
			{ wxART_DOC_TEMPLATE, wxART_BORDER, s_borders_png },

			// ******* wxART_SERVICE *******
			{ wxART_SERVICE, wxART_MESSAGE, s_message_png },
			{ wxART_SERVICE, wxART_LOCAL_VARIABLE, s_variables_png },
			{ wxART_SERVICE, wxART_STACK, s_stack_png },
			{ wxART_SERVICE, wxART_WATCH, s_watch_png },
			{ wxART_SERVICE, wxART_PROPERTY, s_property_png },

			// ******* wxART_METATREE *******
			{ wxART_METATREE, wxART_COMMON_FOLDER, s_commonFolder_png },
			{ wxART_METATREE, wxART_SAVE_METADATA, s_saveMetadata_png },

			// ******* wxART_DEBUG *******
			{ wxART_DEBUG, wxART_DEBUG_START, s_start_png },
			{ wxART_DEBUG, wxART_DEBUG_START_WITHOUT_DEBUGGING, s_startWithoutDebugging_png },
			{ wxART_DEBUG, wxART_DEBUG_ATTACH, s_attach_png },
			{ wxART_DEBUG, wxART_DEBUG_CONTINUE, s_continue_png },
			{ wxART_DEBUG, wxART_DEBUG_PAUSE, s_pause_png },
			{ wxART_DEBUG, wxART_DEBUG_STEP_INTO, s_stepInto_png },
			{ wxART_DEBUG, wxART_DEBUG_STEP_OVER, s_stepOver_png },
			{ wxART_DEBUG, wxART_DEBUG_STEP_OUT, s_stepOut_png },
			{ wxART_DEBUG, wxART_DEBUG_STOP_DEBUGGING, s_stopDebugging_png },
			{ wxART_DEBUG, wxART_DEBUG_STOP_PROGRAM, s_stopProgram_png },
			{ wxART_DEBUG, wxART_DEBUG_REMOVE_ALL_BREAKPOINTS, s_removeAllBreakpoints_png },

			// ******* wxART_DOC_MODULE *******
			{ wxART_DOC_MODULE, wxART_ADD_COMMENT, s_addComment_png },
			{ wxART_DOC_MODULE, wxART_REMOVE_COMMENT, s_removeComment_png },
			{ wxART_DOC_MODULE, wxART_SYNTAX_CONTROL, s_syntaxControl_png },
			{ wxART_DOC_MODULE, wxART_GOTO_LINE, s_gotoLine_png },
			{ wxART_DOC_MODULE, wxART_PROC_AND_FUNC, s_proceduresFunctions_png },
			{ wxART_DOC_MODULE, wxART_FORMAT_CODE, s_formatCode_png },
			{ wxART_DOC_MODULE, wxART_INCREASE_INDENT, s_increaseIndent_png },
			{ wxART_DOC_MODULE, wxART_DECREASE_INDENT, s_decreaseIndent_png },

			// ******* wxART_AUTOCOMPLETE *******
			{ wxART_AUTOCOMPLETE, wxART_FUNCTION, s_function_png },
			{ wxART_AUTOCOMPLETE, wxART_PROCEDURE, s_procedure_png },
			{ wxART_AUTOCOMPLETE, wxART_VARIABLE, s_variable_png },
			{ wxART_AUTOCOMPLETE, wxART_VARIABLE_ALTERNATIVE, s_variableAlt_png },

			// ******* wxART_SERVICE *******
			{ wxART_SERVICE, wxART_OUTPUT_INFORMATION, s_outputInformation_png },
			{ wxART_SERVICE, wxART_OUTPUT_WARNING, s_outputWarning_png },
			{ wxART_SERVICE, wxART_OUTPUT_ERROR, s_outputError_png }
		};

		for (const auto& entry : s_allPictures) {
			if (entry.client == client && entry.id == id)
				return ibBackendPicture::GetBitmapFromBase64(entry.png, size.IsFullySpecified() ? size : wxSize(16, 16));
		}

		// ⭐ WX'S OWN PICTURES, FOR EVERY CLIENT. This provider stands first (pushed over wx's), so a stock id asked
		// anywhere - a menu, a toolbar, a button, a list, a message window - gets this drawing instead of Tango's
		// or an XPM of wx's, and not one call site changes.
		static const struct { wxArtID id; const wxString& png; } s_stockPictures[] =
		{
			{ wxART_DELETE, s_delete_png },
			{ wxART_NEW, s_new_png },
			{ wxART_EDIT, s_edit_png },
			{ wxART_GO_UP, s_goUp_png },
			{ wxART_GO_DOWN, s_goDown_png },
			{ wxART_GO_BACK, s_goBack_png },
			{ wxART_GO_FORWARD, s_goForward_png },
			{ wxART_GO_DIR_UP, s_goDirUp_png },
			{ wxART_COPY, s_copy_png },
			{ wxART_CUT, s_cut_png },
			{ wxART_PASTE, s_paste_png },
			{ wxART_UNDO, s_undo_png },
			{ wxART_REDO, s_redo_png },
			{ wxART_FIND, s_find_png },
			{ wxART_PLUS, s_plus_png },
			{ wxART_MINUS, s_minus_png },
			{ wxART_FILE_OPEN, s_fileOpen_png },
			{ wxART_FILE_SAVE, s_fileSave_png },
			{ wxART_FILE_SAVE_AS, s_fileSaveAs_png },
			{ wxART_NORMAL_FILE, s_normalFile_png },
			{ wxART_FOLDER, s_folder_png },
			{ wxART_LIST_VIEW, s_listView_png },
			{ wxART_REPORT_VIEW, s_reportView_png },
			{ wxART_FULL_SCREEN, s_fullScreen_png },
			{ wxART_HELP_BOOK, s_helpBook_png },
			{ wxART_HELP_SETTINGS, s_helpSettings_png },
			{ wxART_TICK_MARK, s_tickMark_png },
			{ wxART_STOP, s_stop_png },
			{ wxART_QUIT, s_quit_png },
			{ wxART_MISSING_IMAGE, s_missingImage_png },
			{ wxART_WARNING, s_warning_png },
			{ wxART_ERROR, s_error_png },
			{ wxART_INFORMATION, s_information_png },
			{ wxART_QUESTION, s_question_png },
			{ wxART_TIP, s_tip_png }
		};

		for (const auto& entry : s_stockPictures) {
			if (entry.id == id)
				return ibBackendPicture::GetBitmapFromBase64(entry.png, size.IsFullySpecified() ? size : wxSize(16, 16));
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