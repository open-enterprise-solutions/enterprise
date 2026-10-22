////////////////////////////////////////////////////////////////////////////
//	Description : a configuration picture's content hash and the id it rides in
////////////////////////////////////////////////////////////////////////////
//
// A client keeps a picture by the id an answer names it with (ibBackendPicture::SendPicture). A configuration
// picture's id is `<guid>#<hash>`: the hash of what the picture holds, worked out when its Picture is set — so a
// picture changed since is another id, asked for anew, and one unchanged is the same id in every answer. The Picture
// method reads the guid before the `#`.

#include <gtest/gtest.h>

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
		ibProperty* const property = picture->GetProperty(wxT("Picture"));
		return property != nullptr && ibPropertyGate::SetValue(picture, property, wxVariant(new ibVariantDataExternalPicture(image)));
	}

	static ibGuid Guid(const ibValueMetaObjectPicture* picture) {
		const ibGuid guid = picture->GetGuid();
		return guid;
	}
};

} // namespace

TEST(ibValueMetaObjectPicture, GetContentHash_EmptyPicture_IsZero)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	EXPECT_EQ(f.first->GetContentHash(), 0u);
}

TEST(ibValueMetaObjectPicture, GetContentHash_SameContent_SameHash)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_NE(f.second, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	ASSERT_TRUE(PictureFixture::Set(f.second, Image("\x89PNG one", 16, 16)));
	// FNV-1a 64 over the bytes, then width and height as four little-endian bytes each — worked out apart.
	EXPECT_EQ(f.first->GetContentHash(), 0x9a8286f5ade491b3ull);
	EXPECT_EQ(f.second->GetContentHash(), f.first->GetContentHash());
}

TEST(ibValueMetaObjectPicture, GetContentHash_OtherBytesOrSize_OtherHash)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	const u64 before = f.first->GetContentHash();
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG two", 16, 16)));
	EXPECT_NE(f.first->GetContentHash(), before);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 32, 16)));
	EXPECT_NE(f.first->GetContentHash(), before);
	// …and back: the hash is of the content, not of the edits.
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	EXPECT_EQ(f.first->GetContentHash(), before);
}

TEST(ibValueMetaObjectPicture, GetContentHash_ReadFromItsNode_HashItWasWrittenWith)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_NE(f.second, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG read back", 24, 24)));
	ibDataNode node;
	ASSERT_TRUE(f.first->SaveNode(node));
	ASSERT_TRUE(f.second->LoadNode(node));
	EXPECT_EQ(f.second->GetContentHash(), f.first->GetContentHash());
}

TEST(ibBackendPicture, SendPicture_ConfigurationPicture_GuidAndHash)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	const wxString sent = ibBackendPicture::SendPicture(ibPictureDescription(PictureFixture::Guid(f.first)), f.cfg);
	EXPECT_EQ(sent, PictureFixture::Guid(f.first).str() + wxT("#9a8286f5ade491b3"));
	// The Picture method reads the guid before the '#'.
	EXPECT_EQ(ibGuid(sent.BeforeFirst(wxT('#'))), PictureFixture::Guid(f.first));
}

TEST(ibBackendPicture, SendPicture_ContentChanged_AnotherId)
{
	PictureFixture f;
	ASSERT_NE(f.first, nullptr);
	const ibPictureDescription desc(f.first->GetGuid());
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG one", 16, 16)));
	const wxString before = ibBackendPicture::SendPicture(desc, f.cfg);
	const wxString again = ibBackendPicture::SendPicture(desc, f.cfg);
	EXPECT_EQ(again, before);
	ASSERT_TRUE(PictureFixture::Set(f.first, Image("\x89PNG two", 16, 16)));
	const wxString after = ibBackendPicture::SendPicture(desc, f.cfg);
	EXPECT_EQ(after, PictureFixture::Guid(f.first).str() + wxT("#35bbe4b1e775857d"));
	EXPECT_EQ(after.BeforeFirst(wxT('#')), before.BeforeFirst(wxT('#')));
}

TEST(ibBackendPicture, SendPicture_GuidOfNoPicture_Empty)
{
	PictureFixture f;
	const wxString sent = ibBackendPicture::SendPicture(ibPictureDescription(ibGuid(ibGuid::newGuid())), f.cfg);
	EXPECT_TRUE(sent.IsEmpty());
}

TEST(ibBackendPicture, SendPicture_BackendPicture_NumberAlone)
{
	PictureFixture f;
	const wxString sent = ibBackendPicture::SendPicture(ibPictureDescription(g_picErrorCLSID), f.cfg);
	EXPECT_EQ(sent, wxString::Format(wxT("%llu"), static_cast<unsigned long long>(g_picErrorCLSID)));
}
