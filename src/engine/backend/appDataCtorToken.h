#ifndef __APP_DATA_CTOR_TOKEN_H__
#define __APP_DATA_CTOR_TOKEN_H__

// ibAppDataCtorToken — gate-keeper for app-owned subsystems.
//
// Replaces the previous `friend class ibApplicationInstance` declaration
// scattered across every owned-subsystem header (ibSessionRegistry,
// ibConnectionPool, ibLockManager, ibPluginManager,
// ibMetaDataConfigurationBase, ibMetaDataConfiguration,
// ibMetaDataConfigurationStorage).
//
// How it works:
//   - The token's constructor is private.
//   - Only `ibApplicationInstance`, `ibApplicationHost` and `ibSession` are friends, so only they can mint a
//     token (a designer's session makes the configuration it edits — ibSession::AcquireMetaData).
//   - Subsystem ctors take an `ibAppDataCtorToken` argument.
//
// Net effect: subsystems can declare their ctors public (no friend
// gymnastics in their headers), but external code still cannot
// construct them — you cannot produce a token outside appData.
//
// Compared to friend:
//   - One header carries the "who can build subsystems" decision,
//     not seven scattered declarations.
//   - Refactoring appData no longer ripples into every subsystem
//     header just because the friend list shifted.
//   - The compile-time error when an outsider tries to construct a
//     subsystem points at the missing-token, which spells out the
//     architectural intent better than "ctor is private".
//
// Cascading construction: when one subsystem composes another
// internally (e.g. ibMetaDataConfigurationStorage constructs an
// inner ibMetaDataConfiguration as its baseline reference), the
// outer's ctor forwards the token it received from appData into
// the inner's ctor. No re-minting — the same `t` flows down.
//
// ⭐ THE TOKEN ALSO SAYS WHOSE. For a base's subsystem the one that creates it IS its owner, so the base
// mints the token with itself inside (`AppDataCtorToken{ this }`) and the subsystem reads its owner off
// it (`GetApplicationInstance()`) — one argument for one fact, "made by this base"
// (docs/private/multi-base-process.md § 3). Null when the host mints it for the process's share (the
// plugins, the syntax-helper corpus) or a test mints it with no base.
//
// ⭐ …AND FOR WHOM, WHEN A SESSION MINTS IT — its kind: what is made for a designer's session is built as the
// designer's (a configuration's compile cache), whatever thread builds it. Minted by the base, it is for no session
// (Unknown): the base's own, the one the rest work in (Max, 2026-10-28).

// Forward declaration is REQUIRED. `friend class ::ibApplicationInstance;`
// uses a *qualified* name; per [class.friend], qualified friend names
// are looked up — they do NOT introduce a forward declaration the way
// an unqualified `friend class Foo;` would. Without this line the
// friend declaration triggers C2039 "not a member of global namespace"
// because the lookup has nothing to find. appData.h includes this
// header BEFORE its own `class ibApplicationInstance` body, which is the
// path that surfaced the bug.
class ibApplicationInstance;
class ibApplicationHost;
class ibSession;
enum class ibSessionKind : int;   // defined in backend/session/session.h

namespace ib {

class AppDataCtorToken {
#ifdef OES_TESTING
	// Unit-test escape hatch — gtest TUs construct subsystems directly
	// (no full appData bring-up) and need to mint the token themselves.
	// The CMake test target sets OES_TESTING; production builds (sln /
	// CMake non-test) leave it undefined and the default ctor stays
	// reachable only to ibApplicationInstance via friend below.
public:
#else
	// Mintable only from inside ibApplicationInstance's TU — and the host's, which owns the process's share:
	// the plugins and the syntax-helper corpus (appHost.h); and a designer's session's, for the configuration
	// it edits, made for it and held by it alone (ibSession::AcquireMetaData).
	friend class ::ibApplicationInstance;
	friend class ::ibApplicationHost;
	friend class ::ibSession;
#endif
	explicit AppDataCtorToken(ibApplicationInstance* applicationInstance = nullptr) : m_applicationInstance(applicationInstance) {}
	AppDataCtorToken(ibApplicationInstance* applicationInstance, ibSessionKind sessionKind) :
		m_applicationInstance(applicationInstance), m_sessionKind(sessionKind) {}
public:
	AppDataCtorToken(const AppDataCtorToken&) = default;
	AppDataCtorToken& operator=(const AppDataCtorToken&) = default;

	// The base that minted it — the owner of what it builds.
	ibApplicationInstance* GetApplicationInstance() const { return m_applicationInstance; }
	// The kind of the session that minted it — Unknown when the base or the host did.
	ibSessionKind GetSessionKind() const { return m_sessionKind; }

private:
	ibApplicationInstance* m_applicationInstance;
	ibSessionKind m_sessionKind{};   // Unknown (0)
};

} // namespace ib

#endif // __APP_DATA_CTOR_TOKEN_H__
