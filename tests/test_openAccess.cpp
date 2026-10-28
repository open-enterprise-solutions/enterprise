// A user list that cannot be read is not an empty user list. Login used to
// treat them as the same, and an empty name was then accepted.
//
// The door itself — an empty table opens, a dropped table refuses, on a
// file base and on a server — is in test_multiBase.cpp. This only checks
// that HasAny and ListAll throw when nothing is open, instead of answering
// "no rows".

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/userInfo.h"

TEST(UserList, UnreadableListIsNotReportedEmpty) {
	EXPECT_THROW(ibUserInfo::HasAny(), ibCoreException);
	EXPECT_THROW(ibUserInfo::ListAll(), ibCoreException);
}
