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
//   - Only `ibApplicationInstance` and `ibApplicationHost` are friends, so only they can mint a token.
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
	// the plugins and the syntax-helper corpus (appHost.h).
	friend class ::ibApplicationInstance;
	friend class ::ibApplicationHost;
#endif
	explicit AppDataCtorToken(ibApplicationInstance* applicationInstance = nullptr) : m_applicationInstance(applicationInstance) {}
public:
	AppDataCtorToken(const AppDataCtorToken&) = default;
	AppDataCtorToken& operator=(const AppDataCtorToken&) = default;

	// The base that minted it — the owner of what it builds.
	ibApplicationInstance* GetApplicationInstance() const { return m_applicationInstance; }

private:
	ibApplicationInstance* m_applicationInstance;
};

} // namespace ib

#endif // __APP_DATA_CTOR_TOKEN_H__
