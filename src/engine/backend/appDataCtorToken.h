#ifndef __APP_DATA_CTOR_TOKEN_H__
#define __APP_DATA_CTOR_TOKEN_H__

// ibAppDataCtorToken — gate-keeper for app-owned subsystems.
//
// Replaces the previous `friend class ibApplicationData` declaration
// scattered across every owned-subsystem header (ibSessionRegistry,
// ibConnectionPool, ibLockManager, ibPluginManager,
// ibMetaDataConfigurationBase, ibMetaDataConfiguration,
// ibMetaDataConfigurationStorage).
//
// How it works:
//   - The token's default constructor is private.
//   - Only `ibApplicationData` is a friend, so only it can mint a token.
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

// Forward declaration is REQUIRED. `friend class ::ibApplicationData;`
// uses a *qualified* name; per [class.friend], qualified friend names
// are looked up — they do NOT introduce a forward declaration the way
// an unqualified `friend class Foo;` would. Without this line the
// friend declaration triggers C2039 "not a member of global namespace"
// because the lookup has nothing to find. appData.h includes this
// header BEFORE its own `class ibApplicationData` body, which is the
// path that surfaced the bug.
class ibApplicationData;

namespace ib {

class AppDataCtorToken {
#ifdef OES_TESTING
	// Unit-test escape hatch — gtest TUs construct subsystems directly
	// (no full appData bring-up) and need to mint the token themselves.
	// The CMake test target sets OES_TESTING; production builds (sln /
	// CMake non-test) leave it undefined and the default ctor stays
	// reachable only to ibApplicationData via friend below.
public:
#else
	// Mintable only from inside ibApplicationData's TU.
	friend class ::ibApplicationData;
#endif
	AppDataCtorToken() = default;
public:
	AppDataCtorToken(const AppDataCtorToken&) = default;
	AppDataCtorToken& operator=(const AppDataCtorToken&) = default;
};

} // namespace ib

#endif // __APP_DATA_CTOR_TOKEN_H__
