#ifndef __CLIENT_HOST_CTOR_TOKEN_H__
#define __CLIENT_HOST_CTOR_TOKEN_H__

// ClientHostCtorToken — gate-keeper for what a client host owns, as ib::AppDataCtorToken is for a base's subsystems
// (backend/appDataCtorToken.h): the constructor is private and the host alone mints it, so what takes it can be built
// nowhere else. It says whose too: the base the host serves, the owner of what it builds.

class ibApplicationInstance;
class ibClientHost;

namespace ib {

class ClientHostCtorToken {
#ifdef OES_TESTING
	// As AppDataCtorToken: gtest TUs build what they test without a host.
public:
#else
	friend class ::ibClientHost;
#endif
	explicit ClientHostCtorToken(ibApplicationInstance* applicationInstance) : m_applicationInstance(applicationInstance) {}
public:
	ClientHostCtorToken(const ClientHostCtorToken&) = default;
	ClientHostCtorToken& operator=(const ClientHostCtorToken&) = default;

	// The base of the host that minted it.
	ibApplicationInstance* GetApplicationInstance() const { return m_applicationInstance; }

private:
	ibApplicationInstance* m_applicationInstance;
};

} // namespace ib

#endif // __CLIENT_HOST_CTOR_TOKEN_H__
