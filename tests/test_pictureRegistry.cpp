////////////////////////////////////////////////////////////////////////////
//	Description : every picture the engine offers is a drawing that draws
////////////////////////////////////////////////////////////////////////////
//
// A picture is written in the source as its SVG and kept as it (ibPictureRegistry): whoever shows it draws it at the
// size it shows it. A drawing that does not parse would show as no picture wherever it is shown, with nothing said.
// Asked here of every one the core loaded, and of every class icon the engine offers beside them.

#include <gtest/gtest.h>

#include "backend/backend_picture.h"   // ibBackendPicture::GetArrayPicture - the registry's and every class's icon
#include "core/pictureRegistry.h"

TEST(PictureRegistry, EveryRegisteredPicture_IsADrawingThatDraws)
{
	const std::vector<ibPictureEntry> entries = ibPictureRegistry::GetArrayPicture();
	ASSERT_FALSE(entries.empty());
	for (const ibPictureEntry& entry : entries) {
		ASSERT_TRUE(entry.m_picture.IsOk()) << entry.m_name.ToStdString();
		// kept as its text - the drawing itself, not pixels made of it
		EXPECT_EQ(static_cast<const char*>(entry.m_picture.GetData().GetData())[0], '<') << entry.m_name.ToStdString();
		EXPECT_TRUE(entry.m_picture.Scale(ibSize(16, 16)).GetSize() == ibSize(16, 16)) << entry.m_name.ToStdString();
		EXPECT_TRUE(entry.m_picture.Scale(ibSize(64, 64)).GetSize() == ibSize(64, 64)) << entry.m_name.ToStdString();
	}
}

// Kept ONCE: asked again, the registry hands out the text it was given, not a copy made anew.
TEST(PictureRegistry, GetPicture_IsTheDrawingKeptOnce)
{
	for (const ibPictureEntry& entry : ibPictureRegistry::GetArrayPicture())
		EXPECT_EQ(ibPictureRegistry::GetPicture(entry.m_id).GetData().GetData(), entry.m_picture.GetData().GetData())
			<< entry.m_name.ToStdString();
}

// Every icon a picture property offers - the registry's and every metadata and control class's - draws at the size a
// row shows it.
TEST(PictureRegistry, EveryOfferedIcon_Draws)
{
	const std::vector<ibPictureEntry> entries = ibBackendPicture::GetArrayPicture();
	ASSERT_FALSE(entries.empty());
	for (const ibPictureEntry& entry : entries)
		EXPECT_TRUE(entry.m_picture.Scale(ibSize(16, 16)).IsOk()) << entry.m_name.ToStdString();
}
