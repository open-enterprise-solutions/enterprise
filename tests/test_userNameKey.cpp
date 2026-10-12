#include "backend/userInfo.h"

#include <gtest/gtest.h>

#include <wx/init.h>

namespace {

struct WxReady {
	WxReady()
	{
		static wxInitializer init;
		(void)init;
	}
};

} // namespace

TEST(UserNameKey, FoldingIgnoresCaseAndSurroundingSpace)
{
	WxReady wx;
	EXPECT_EQ(ibUserInfo::NameKey(wxT("Admin")), wxT("admin"));
	EXPECT_EQ(ibUserInfo::NameKey(wxT(" ADMIN ")), ibUserInfo::NameKey(wxT("admin")));
	EXPECT_TRUE(ibUserInfo::NameKey(wxT("   ")).empty());
	EXPECT_EQ(ibUserInfo::NameAsShown(wxT("Admin")), wxT("Admin"));
	EXPECT_EQ(ibUserInfo::NameAsShown(wxT("   ")), wxT("(blank)"));
}
