#ifndef _DESIGNER_QUERY_CONSTRUCTOR_REQUEST_H__
#define _DESIGNER_QUERY_CONSTRUCTOR_REQUEST_H__

// WHAT THE QUERY CONSTRUCTOR ASKS THE CONFIGURATION — ibProtocol::ibView::ibQueryConstructor, answered by any designer
// tab whose document has one (a module's, a form's, a composer's). The thin client's ibQueryConstructorModel asks; the
// desktop's model (backend/query/queryConstructorModel.h) answers here, the same calls the desktop window makes.

#include "frmserver/frmserver.h"

#include <wx/string.h>

class ibDataNode;
class ibMetaData;

// A kind's class picture, by its id; none: empty. Both constructors' Kinds.
FRMSERVER_API wxString ibKindPicture(const wxString& kind, const ibMetaData* metaData);

// True: the request was one of ibQueryConstructor's, answered in `response`. False: not one of them.
FRMSERVER_API bool ibFetchQueryConstructorRequest(const ibDataNode& request, ibDataNode& response, const ibMetaData* metaData);

#endif // _DESIGNER_QUERY_CONSTRUCTOR_REQUEST_H__
