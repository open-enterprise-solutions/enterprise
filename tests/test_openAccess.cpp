// A user list that cannot be read is not an empty user list. Login used to
// treat them as the same, and an empty name was then accepted.

#include <gtest/gtest.h>

#include "backend/appData.h"
#include "backend/backend_exception.h"
#include "backend/userInfo.h"

TEST(OpenAccess, EmptyListOnALocalProcessIsPermitted) {
	EXPECT_TRUE(ibOpenAccessPermitted(true, false));
}

TEST(OpenAccess, EmptyListWhileServingClientsIsRefused) {
	EXPECT_FALSE(ibOpenAccessPermitted(true, true));
}

TEST(OpenAccess, APopulatedListIsNotOpenAccess) {
	EXPECT_FALSE(ibOpenAccessPermitted(false, false));
	EXPECT_FALSE(ibOpenAccessPermitted(false, true));
}

TEST(UserList, UnreadableListIsNotReportedEmpty) {
	EXPECT_THROW(ibUserInfo::HasAny(), ibBackendException);
}
