////////////////////////////////////////////////////////////////////////////
//	Description : a picture is a PNG's bytes or an SVG's text, told apart by the bytes themselves
////////////////////////////////////////////////////////////////////////////
//
// ibPicture (core/graphics/picture.h): an engine's picture is kept and sent as its drawing, an image a person gave as
// its PNG. Its size, its scaling and its SVG answer for both, each as the bytes are.

#include <cstring>   // std::memcmp - two PNGs byte for byte

#include <gtest/gtest.h>

#include "core/graphics/picture.h"

namespace {

// 16 by 12, a blue plate - wider than it is high, so a size read the wrong way round shows
const ibString kPlate = ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='12' viewBox='0 0 16 12'>"
	"<rect x='2' y='2' width='12' height='8' fill='#1c6fd1'/></svg>");

}

TEST(Picture, ADrawing_IsKeptAsItsText)
{
	const ibPicture drawing = ibPicture::FromSvg(kPlate);
	ASSERT_TRUE(drawing.IsOk());
	EXPECT_TRUE(drawing.ToSvg(0) == kPlate) << "said as SVG, a drawing is itself - no pixels traced";
}

TEST(Picture, ADrawing_IsAsBigAsItsTextSays)
{
	EXPECT_TRUE(ibPicture::FromSvg(kPlate).GetSize() == ibSize(16, 12));
}

TEST(Picture, ADrawingScaled_IsItsPngDrawnAtThatSize)
{
	const ibPicture drawn = ibPicture::FromSvg(kPlate).Scale(ibSize(32, 24));
	ASSERT_TRUE(drawn.IsOk());
	EXPECT_TRUE(drawn.GetSize() == ibSize(32, 24));
	const ibPicture made = ibPicture::FromSvg(kPlate, ibSize(32, 24));
	ASSERT_EQ(drawn.GetData().GetDataLen(), made.GetData().GetDataLen());
	EXPECT_EQ(std::memcmp(drawn.GetData().GetData(), made.GetData().GetData(), made.GetData().GetDataLen()), 0);
}

TEST(Picture, APng_StaysAPng)
{
	const ibPicture png = ibPicture::FromSvg(kPlate, ibSize(16, 12));
	const ibPicture read(png.GetData());
	EXPECT_TRUE(read.GetSize() == ibSize(16, 12));
	// itself at its own size, the same bytes
	EXPECT_EQ(read.Scale(ibSize(16, 12)).GetData().GetData(), png.GetData().GetData());
	EXPECT_TRUE(read.Scale(ibSize(8, 6)).GetSize() == ibSize(8, 6));
	EXPECT_FALSE(read.ToSvg(4096).IsEmpty()) << "a PNG's pixels said as SVG";
}

TEST(Picture, BytesThatAreNeither_AreNoPicture)
{
	const char bytes[] = "not a picture";
	const ibPicture neither(ibMemoryBuffer(bytes, sizeof(bytes) - 1));
	EXPECT_TRUE(neither.GetSize() == ibSize());
	EXPECT_FALSE(neither.Scale(ibSize(16, 16)).IsOk());
	EXPECT_TRUE(neither.ToSvg(4096).IsEmpty());
}

// A drawing that draws nothing - here a shape with neither fill nor stroke - is no picture, not a transparent square
// taken as set.
TEST(Picture, ADrawingOfNothing_ScalesToNone)
{
	const ibPicture nothing = ibPicture::FromSvg(ibT("<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16'>"
		"<rect x='2' y='2' width='12' height='12' fill='none'/></svg>"));
	EXPECT_FALSE(nothing.Scale(ibSize(16, 16)).IsOk());
}
