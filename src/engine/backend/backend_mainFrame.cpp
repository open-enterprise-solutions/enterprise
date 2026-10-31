#include "backend_mainFrame.h"

#include "backend/session/session.h"

// ibBackendDocFrame no longer owns any process-level / thread-local
// state — the frame is a member of the ibSession that created it.
// ms_mainFrame / t_mainFrame / GetDocMDIFrame / InstallOnThread and the
// transitional ibCurrentFrame() helper are all gone. Callers reach
// their frame through the session pointer that's already in scope.
// Default ctor/dtor live in the header.

int ibBackendDocFrame::ShowModalMessage(const ibString& message,
                                           const ibString& caption,
                                           int style)
{
	if (ibSession* const session = GetSession())
		session->RefuseQuestionFromRoleHandler();
	return DoShowModalMessage(message, caption, style);
}

bool ibBackendDocFrame::Request(const ibDataNode& request, ibDataNode& response)
{
	if (ibSession* const session = GetSession())
		session->RefuseQuestionFromRoleHandler();
	return DoRequest(request, response);
}
