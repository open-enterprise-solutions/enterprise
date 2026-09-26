#ifndef _APPBITMAPS_H__
#define _APPBITMAPS_H__

#include <wx/artprov.h>

// ----------------------------------------------------------------------------
// Art clients
// ----------------------------------------------------------------------------

#define wxART_AUTOCOMPLETE			wxART_MAKE_CLIENT_ID(wxART_AUTOCOMPLETE)
#define wxART_DOC_MODULE			wxART_MAKE_CLIENT_ID(wxART_DOC_MODULE)
#define wxART_DOC_FORM				wxART_MAKE_CLIENT_ID(wxART_DOC_FORM)
#define wxART_DOC_TEMPLATE			wxART_MAKE_CLIENT_ID(wxART_DOC_TEMPLATE)
#define wxART_METATREE				wxART_MAKE_CLIENT_ID(wxART_METATREE)
#define wxART_VISUALHOST			wxART_MAKE_CLIENT_ID(wxART_VISUALHOST)
#define wxART_SERVICE				wxART_MAKE_CLIENT_ID(wxART_SERVICE)
#define wxART_DEBUG					wxART_MAKE_CLIENT_ID(wxART_DEBUG)

///////////////////////////////////////////////////////////////////////////////
#define wxART_FRONTEND				wxART_MAKE_CLIENT_ID(wxART_FRONTEND)

// ----------------------------------------------------------------------------
// Art IDs
// ----------------------------------------------------------------------------

// A form's tool with neither a picture nor a caption (client wxART_VISUALHOST): a "?", so it can still be seen.
#define wxART_NO_PICTURE			 wxART_MAKE_ART_ID(wxART_NO_PICTURE)

#define wxART_FUNCTION				 wxART_MAKE_ART_ID(wxART_FUNCTION)
#define wxART_PROCEDURE				 wxART_MAKE_ART_ID(wxART_PROCEDURE)
#define wxART_VARIABLE				 wxART_MAKE_ART_ID(wxART_VARIABLE)
#define wxART_VARIABLE_ALTERNATIVE	 wxART_MAKE_ART_ID(wxART_VARIABLE_ALTERNATIVE)

#define wxART_ADD_COMMENT			 wxART_MAKE_ART_ID(wxART_ADD_COMMENT)
#define wxART_REMOVE_COMMENT		 wxART_MAKE_ART_ID(wxART_REMOVE_COMMENT)
#define wxART_SYNTAX_CONTROL		 wxART_MAKE_ART_ID(wxART_SYNTAX_CONTROL)
#define wxART_GOTO_LINE				 wxART_MAKE_ART_ID(wxART_GOTO_LINE)
#define wxART_PROC_AND_FUNC			 wxART_MAKE_ART_ID(wxART_PROC_AND_FUNC)
#define wxART_FORMAT_CODE			 wxART_MAKE_ART_ID(wxART_FORMAT_CODE)
#define wxART_INCREASE_INDENT		 wxART_MAKE_ART_ID(wxART_INCREASE_INDENT)
#define wxART_DECREASE_INDENT		 wxART_MAKE_ART_ID(wxART_DECREASE_INDENT)

#define wxART_DESIGNER_PAGE			 wxART_MAKE_ART_ID(wxART_DESIGNER_PAGE)
#define wxART_CODE_PAGE				 wxART_MAKE_ART_ID(wxART_CODE_PAGE)

#define wxART_MERGE_CELL			 wxART_MAKE_ART_ID(wxART_MERGE_CELL)
#define wxART_ADD_SECTION			 wxART_MAKE_ART_ID(wxART_ADD_SECTION)
#define wxART_REMOVE_SECTION		 wxART_MAKE_ART_ID(wxART_REMOVE_SECTION)
#define wxART_SHOW_CELL				 wxART_MAKE_ART_ID(wxART_SHOW_CELL)
#define wxART_SHOW_HEADER			 wxART_MAKE_ART_ID(wxART_SHOW_HEADER)
#define wxART_SHOW_SECTION			 wxART_MAKE_ART_ID(wxART_SHOW_SECTION)
#define wxART_BORDER				 wxART_MAKE_ART_ID(wxART_BORDER)

#define wxART_MESSAGE				 wxART_MAKE_ART_ID(wxART_MESSAGE)
#define wxART_LOCAL_VARIABLE		 wxART_MAKE_ART_ID(wxART_LOCAL_VARIABLE)
#define wxART_STACK					 wxART_MAKE_ART_ID(wxART_STACK)
#define wxART_WATCH					 wxART_MAKE_ART_ID(wxART_WATCH)

#define wxART_PROPERTY				 wxART_MAKE_ART_ID(wxART_PROPERTY)

// The output window's marker, one per level of a message (client wxART_SERVICE). Drawn on 12x12, the line's height.
#define wxART_OUTPUT_INFORMATION	 wxART_MAKE_ART_ID(wxART_OUTPUT_INFORMATION)
#define wxART_OUTPUT_WARNING		 wxART_MAKE_ART_ID(wxART_OUTPUT_WARNING)
#define wxART_OUTPUT_ERROR			 wxART_MAKE_ART_ID(wxART_OUTPUT_ERROR)

#define wxART_COMMON_FOLDER			 wxART_MAKE_ART_ID(wxART_COMMON_FOLDERS)
#define wxART_SAVE_METADATA			 wxART_MAKE_ART_ID(wxART_SAVE_METADATA)

///////////////////////////////////////////////////////////////////////////////

#define wxART_DATABASE				wxART_MAKE_ART_ID(wxART_DATABASE)
#define wxART_DATABASE_ROOLBACK		wxART_MAKE_ART_ID(wxART_DATABASE_ROOLBACK)
#define wxART_DATABASE_APPLY		wxART_MAKE_ART_ID(wxART_DATABASE_APPLY)

#define wxART_ADD					wxART_MAKE_ART_ID(wxART_ADD)
#define wxART_EDIT					wxART_MAKE_ART_ID(wxART_EDIT)
#define wxART_DELETE				wxART_MAKE_ART_ID(wxART_DELETE)
#define wxART_UP					wxART_MAKE_ART_ID(wxART_UP)
#define wxART_DOWN					wxART_MAKE_ART_ID(wxART_DOWN)

#define wxART_SORT					wxART_MAKE_ART_ID(wxART_SORT)
// The query constructor — REGISTERED here rather than dropped into the code editor's marker
// bitmaps, because an icon in the provider is one anything can ask for by id (a toolbar, a
// menu, a metadata tree row) while one embedded in a control's own resource file belongs to
// that control and is copied by whoever needs it next.
#define wxART_QUERY_CONSTRUCTOR		wxART_MAKE_ART_ID(wxART_QUERY_CONSTRUCTOR)

// A TEMPORARY TABLE. It is a source like any other from the next statement point of view, so it
// wants a picture like any other - and it is not a metatype, so it has no registered class icon to
// borrow one from. Here rather than in the constructor because the tree is not the only place a
// temp table will be shown.
#define wxART_TEMP_TABLE			wxART_MAKE_ART_ID(wxART_TEMP_TABLE)

// A NESTED QUERY - a query standing where a table would. Same argument as the temp table above: it
// is a source with no metatype behind it, so it carries its own picture rather than borrowing one.
#define wxART_NESTED_QUERY			wxART_MAKE_ART_ID(wxART_NESTED_QUERY)

// The code editor's context menu: the constructors beside the query's (client wxART_FRONTEND), a breakpoint's
// condition (wxART_DEBUG), select all (wxART_DOC_MODULE).
#define wxART_TRANSLATION_CONSTRUCTOR	wxART_MAKE_ART_ID(wxART_TRANSLATION_CONSTRUCTOR)
#define wxART_FORMAT_CONSTRUCTOR		wxART_MAKE_ART_ID(wxART_FORMAT_CONSTRUCTOR)
#define wxART_LINQ_CONSTRUCTOR			wxART_MAKE_ART_ID(wxART_LINQ_CONSTRUCTOR)
#define wxART_BREAKPOINT_CONDITION		wxART_MAKE_ART_ID(wxART_BREAKPOINT_CONDITION)
#define wxART_SELECT_ALL				wxART_MAKE_ART_ID(wxART_SELECT_ALL)

// The debugger's pictures (client wxART_DEBUG) — the Debug menu's and the debug toolbar's, one per command.
// Drawn as SVG in artProvider/debugger/. Prefixed wxART_DEBUG_ because wxWidgets already owns wxART_STOP.
#define wxART_DEBUG_START						wxART_MAKE_ART_ID(wxART_DEBUG_START)
#define wxART_DEBUG_START_WITHOUT_DEBUGGING		wxART_MAKE_ART_ID(wxART_DEBUG_START_WITHOUT_DEBUGGING)
#define wxART_DEBUG_ATTACH						wxART_MAKE_ART_ID(wxART_DEBUG_ATTACH)
#define wxART_DEBUG_CONTINUE					wxART_MAKE_ART_ID(wxART_DEBUG_CONTINUE)
#define wxART_DEBUG_PAUSE						wxART_MAKE_ART_ID(wxART_DEBUG_PAUSE)
#define wxART_DEBUG_STEP_INTO					wxART_MAKE_ART_ID(wxART_DEBUG_STEP_INTO)
#define wxART_DEBUG_STEP_OVER					wxART_MAKE_ART_ID(wxART_DEBUG_STEP_OVER)
#define wxART_DEBUG_STEP_OUT					wxART_MAKE_ART_ID(wxART_DEBUG_STEP_OUT)
#define wxART_DEBUG_STOP_DEBUGGING				wxART_MAKE_ART_ID(wxART_DEBUG_STOP_DEBUGGING)
#define wxART_DEBUG_STOP_PROGRAM				wxART_MAKE_ART_ID(wxART_DEBUG_STOP_PROGRAM)
#define wxART_DEBUG_REMOVE_ALL_BREAKPOINTS		wxART_MAKE_ART_ID(wxART_DEBUG_REMOVE_ALL_BREAKPOINTS)

#endif
