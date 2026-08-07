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

///////////////////////////////////////////////////////////////////////////////
#define wxART_FRONTEND				wxART_MAKE_CLIENT_ID(wxART_FRONTEND)

// ----------------------------------------------------------------------------
// Art IDs
// ----------------------------------------------------------------------------

#define wxART_FUNCTION_RED			 wxART_MAKE_ART_ID(wxART_FUNCTION_RED)
#define wxART_FUNCTION_BLUE			 wxART_MAKE_ART_ID(wxART_FUNCTION_BLUE)
#define wxART_PROCEDURE_RED			 wxART_MAKE_ART_ID(wxART_PROCEDURE_RED)
#define wxART_PROCEDURE_BLUE		 wxART_MAKE_ART_ID(wxART_PROCEDURE_BLUE)
#define wxART_VARIABLE				 wxART_MAKE_ART_ID(wxART_VARIABLE)
#define wxART_VARIABLE_ALTERNATIVE	 wxART_MAKE_ART_ID(wxART_VARIABLE_ALTERNATIVE)

#define wxART_ADD_COMMENT			 wxART_MAKE_ART_ID(wxART_ADD_COMMENT)
#define wxART_REMOVE_COMMENT		 wxART_MAKE_ART_ID(wxART_REMOVE_COMMENT)
#define wxART_SYNTAX_CONTROL		 wxART_MAKE_ART_ID(wxART_SYNTAX_CONTROL)
#define wxART_GOTO_LINE				 wxART_MAKE_ART_ID(wxART_GOTO_LINE)
#define wxART_PROC_AND_FUNC			 wxART_MAKE_ART_ID(wxART_PROC_AND_FUNC)
#define wxART_FORMAT_CODE			 wxART_MAKE_ART_ID(wxART_FORMAT_CODE)

#define wxART_DESIGNER_PAGE			 wxART_MAKE_ART_ID(wxART_DESIGNER_PAGE)
#define wxART_CODE_PAGE				 wxART_MAKE_ART_ID(wxART_CODE_PAGE)

#define wxART_MERGE_CELL			 wxART_MAKE_ART_ID(wxART_MERGE_CELL)
#define wxART_ADD_SECTION			 wxART_MAKE_ART_ID(wxART_ADD_SECTION)
#define wxART_REMOVE_SECTION		 wxART_MAKE_ART_ID(wxART_REMOVE_SECTION)
#define wxART_SHOW_CELL				 wxART_MAKE_ART_ID(wxART_SHOW_CELL)
#define wxART_SHOW_HEADER			 wxART_MAKE_ART_ID(wxART_SHOW_HEADER)
#define wxART_SHOW_SECTION			 wxART_MAKE_ART_ID(wxART_SHOW_SECTION)
#define wxART_BORDER				 wxART_MAKE_ART_ID(wxART_BORDER)
#define wxART_DOCK_TABLE			 wxART_MAKE_ART_ID(wxART_DOCK_TABLE)

#define wxART_MESSAGE				 wxART_MAKE_ART_ID(wxART_MESSAGE)
#define wxART_LOCAL_VARIABLE		 wxART_MAKE_ART_ID(wxART_LOCAL_VARIABLE)
#define wxART_STACK					 wxART_MAKE_ART_ID(wxART_STACK)
#define wxART_WATCH					 wxART_MAKE_ART_ID(wxART_WATCH)

#define wxART_PROPERTY				 wxART_MAKE_ART_ID(wxART_PROPERTY)

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

#endif
