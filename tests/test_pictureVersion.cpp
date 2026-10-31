////////////////////////////////////////////////////////////////////////////
//	Description : a configuration picture's version and the id it rides in
////////////////////////////////////////////////////////////////////////////
//
// A client keeps a picture by the id an answer names it with (ibBackendPicture::SendPicture). A configuration
// picture's id is `<guid>#<version>`: the version a guid new with each picture its Picture is given — so a picture
// changed since is named by another version and asked for anew, and one unchanged is the same id in every answer. The
// Picture method reads the guid before the `#`.

#include <gtest/gtest.h>

#include <cstring>

#include "backend/backend_picture.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaPictureObject.h"
#include "backend/propertyManager/property/variant/variantPicture.h"
#include "core/picturePredefined.h"
#include "core/serialize/dataBuilder.h"

namespace {

// An image of its own, as a configuration's Picture object holds it: the file's bytes and its size.
ibExternalPictureDescription Image(const std::string& bytes, unsigned int width, unsigned int height)
{
	ibExternalPictureDescription image;
	image.m_img_name = "test.png";
	image.m_img_buffer = bytes;
	image.m_width = width;
	image.m_height = height;
	return image;
}

struct PictureFixture {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectPicture* first = nullptr;
	ibValueMetaObjectPicture* second = nullptr;

	PictureFixture() {
		ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
		if (root == nullptr)
			return;
		first = dynamic_cast<ibValueMetaObjectPicture*>(cfg.CreateMetaObject(g_metaPictureCLSID, root, /*runObject*/ false));
		second = dynamic_cast<ibValueMetaObjectPicture*>(cfg.CreateMetaObject(g_metaPictureCLSID, root, /*runObject*/ false));
	}

	// Through the door a person's edit goes through — the inspector's, the MCP server's.
	static bool Set(ibValueMetaObjectPicture* picture, const ibExternalPictureDescription& image) {
		ibProperty* const property = picture->GetProperty(ibT("Picture"));
		return property != nullptr && ibPropertyGate::SetValue(picture, property, ibVariant(new ibVariantExternalPicture(image)));
	}

	static ibGuid Guid(const ibValueMetaObjectPicture* picture) {
		const ibGuid guid = picture->GetGuid();
		return guid;
	}
};

} // namespace

TEST(ibValueMetaObjectPicture, GetValueAsVersion_EachPictureGiven_AnotherVersion)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	const ibGuid before = f.first->GetValueAsVersion();
	EXPECT_TRUE(before.isValid());
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG two", 16, 16)));
	EXPECT_NE(f.first->GetValueAsVersion(), before);
	// …the version is of the giving, not of the bytes: the same picture given again is asked for again.
	const ibGuid two = f.first->GetValueAsVersion();
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG two", 16, 16)));
	EXPECT_NE(f.first->GetValueAsVersion(), two);
}

TEST(ibVariantExternalPicture, GetPictureVersion_Copy_SameVersion)
{
	const ibVariantExternalPicture given(Image("\x89PNG one", 16, 16));
	const ibVariantExternalPicture copy(given);
	EXPECT_EQ(copy.GetPictureVersion(), given.GetPictureVersion());
	EXPECT_NE(ibVariantExternalPicture(Image("\x89PNG one", 16, 16)).GetPictureVersion(), given.GetPictureVersion());
}

TEST(ibValueMetaObjectPicture, GetValueAsVersion_ReadFromItsNode_NewVersion)
{
	// Not written with the picture: one read is given a version of its own process.
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_NE(f.second, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG read back", 24, 24)));
	ibDataNode node;
	ASSERT_TRUE(f.first->SaveNode(node));
	ASSERT_TRUE(f.second->LoadNode(node));
	EXPECT_TRUE(f.second->GetValueAsVersion().isValid());
	EXPECT_NE(f.second->GetValueAsVersion(), f.first->GetValueAsVersion());
}

TEST(ibBackendPicture, SendPicture_ConfigurationPicture_GuidAndVersion)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	const ibString sent = ibBackendPicture::SendPicture(ibPictureDescription(PictureFixture::Guid(f.first)), &f.cfg).AsString();
	EXPECT_EQ(sent, PictureFixture::Guid(f.first).str() + wxT("#") + f.first->GetValueAsVersion().str());
	// The Picture method reads the guid before the '#'.
	EXPECT_EQ(ibGuid(sent.BeforeFirst(wxT('#'))), PictureFixture::Guid(f.first));
}

TEST(ibBackendPicture, SendPicture_PictureChanged_AnotherIdOfTheSameGuid)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	const ibPictureDescription desc(PictureFixture::Guid(f.first));
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	const ibString before = ibBackendPicture::SendPicture(desc, &f.cfg).AsString();
	EXPECT_EQ(ibBackendPicture::SendPicture(desc, &f.cfg).AsString(), before);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG two", 16, 16)));
	const ibString after = ibBackendPicture::SendPicture(desc, &f.cfg).AsString();
	EXPECT_NE(after, before);
	EXPECT_EQ(after.BeforeFirst(wxT('#')), before.BeforeFirst(wxT('#')));
}

TEST(ibBackendPicture, SendPicture_NoConfiguration_ConfigurationPictureEmpty)
{
	// A form with no configuration (no metaobject, no source) names no configuration picture — and does not fall.
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	EXPECT_TRUE(ibBackendPicture::SendPicture(ibPictureDescription(PictureFixture::Guid(f.first)), nullptr).IsEmpty());
}

TEST(ibBackendPicture, SendPicture_GuidOfNoPicture_Empty)
{
	PictureFixture f;
	EXPECT_TRUE(ibBackendPicture::SendPicture(ibPictureDescription(ibGuid(ibGuid::newGuid())), &f.cfg).IsEmpty());
}

TEST(ibBackendPicture, SendPicture_BackendPicture_NumberAlone)
{
	// …by its number alone, or described, with a configuration or without: a backend picture is the process's.
	const ibString number = ibString::Format(wxT("%llu"), static_cast<unsigned long long>(g_picErrorCLSID));
	PictureFixture f;
	EXPECT_EQ(ibBackendPicture::SendPicture(g_picErrorCLSID), number);
	EXPECT_EQ(ibBackendPicture::SendPicture(ibPictureDescription(g_picErrorCLSID), &f.cfg).AsString(), number);
	EXPECT_EQ(ibBackendPicture::SendPicture(ibPictureDescription(g_picErrorCLSID), nullptr).AsString(), number);
	EXPECT_TRUE(ibBackendPicture::SendPicture(static_cast<ibPictureID>(0)).IsEmpty());
}

TEST(ibBackendPicture, SendPicture_FilePicture_ItsPngBytes)
{
	// A picture held as a file's bytes has no id of its own: it goes as its PNG — the bytes themselves, no text of them.
	const ibPicture drawn = ibPicture::FromSvg(
		wxT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'><rect width='16' height='16'/></svg>"),
		ibSize(16, 16));
	ASSERT_TRUE(drawn.IsOk());
	const ibMemoryBuffer& file = drawn.GetData();
	const std::string bytes(static_cast<const char*>(file.GetData()), file.GetDataLen());

	const ibDataValue sent = ibBackendPicture::SendPicture(ibPictureDescription(Image(bytes, 16, 16)), nullptr);
	ASSERT_EQ(sent.Kind(), ibDataKind::Binary);
	const ibMemoryBuffer png = sent.AsBinary();
	ASSERT_GE(png.GetDataLen(), 8u);
	EXPECT_EQ(std::memcmp(png.GetData(), "\x89PNG\r\n\x1a\n", 8), 0);
}
