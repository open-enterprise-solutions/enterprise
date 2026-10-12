#ifndef OES_PAGE_LOAD_SESSION_H_
#define OES_PAGE_LOAD_SESSION_H_

// A navigation that fetches the page cannot send X-OES-Session. The only id
// it can carry is the oes_session cookie, and the client does not use that
// cookie: each tab keeps its own id in sessionStorage and sends it on the
// first request that can carry a header. Opening a session for the page
// left an unauthenticated row nobody adopted, and a reload then saw that
// row's tab count of zero and replaced it, while the forms sat on the
// tab's own session.
//
// The page is served either way. No session is opened and no cookie is set.
// The first request that names an id registers it (POST /login).
constexpr bool PageLoadOpensASession()
{
	return false;
}

#endif
