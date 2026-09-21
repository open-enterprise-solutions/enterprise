// =============================================================================
// The debugger's pictures.
//
// The designer's Debug toolbar and Debug menu ask the art provider for one picture per command, under client
// wxART_DEBUG — a PNG in Base64 in artProvider/private/picturePredefined.h, drawn from its SVG by
// tools/pictures/render.js. A picture kept as text can fail quietly: a string cut short gives a blank button,
// not an error. These tests draw each one and look at the pixels, so a button that would be empty or a twin
// of its neighbour is caught here and not by somebody squinting at the toolbar.
// =============================================================================

#include <gtest/gtest.h>

#include "frontend/artProvider/artProvider.h"

#include <wx/bitmap.h>
#include <wx/bmpbndl.h>
#include <wx/image.h>

#include <map>
#include <string>
#include <vector>

namespace {

struct DebugIcon { const char* name; wxArtID id; };

std::vector<DebugIcon> AllDebugIcons()
{
	return {
		{ "Start",                  wxART_DEBUG_START },
		{ "StartWithoutDebugging",  wxART_DEBUG_START_WITHOUT_DEBUGGING },
		{ "Attach",                 wxART_DEBUG_ATTACH },
		{ "Continue",               wxART_DEBUG_CONTINUE },
		{ "Pause",                  wxART_DEBUG_PAUSE },
		{ "StepInto",               wxART_DEBUG_STEP_INTO },
		{ "StepOver",               wxART_DEBUG_STEP_OVER },
		{ "StepOut",                wxART_DEBUG_STEP_OUT },
		{ "StopDebugging",          wxART_DEBUG_STOP_DEBUGGING },
		{ "StopProgram",            wxART_DEBUG_STOP_PROGRAM },
		{ "RemoveAllBreakpoints",   wxART_DEBUG_REMOVE_ALL_BREAKPOINTS },
	};
}

wxImage Draw(const wxArtID& id, const wxSize& size)
{
	const wxBitmapBundle bundle = wxArtProvider::GetBitmapBundle(id, wxART_DEBUG, wxSize(16, 16));
	if (!bundle.IsOk())
		return wxImage();
	return bundle.GetBitmap(size).ConvertToImage();
}

// How many pixels are visible (not fully transparent).
int VisiblePixels(const wxImage& image)
{
	int count = 0;
	for (int y = 0; y < image.GetHeight(); ++y) {
		for (int x = 0; x < image.GetWidth(); ++x) {
			if (!image.HasAlpha() || image.GetAlpha(x, y) > 32)
				++count;
		}
	}
	return count;
}

// A fingerprint of the picture: what it looks like, not where the object lives.
std::string Fingerprint(const wxImage& image)
{
	std::string out;
	for (int y = 0; y < image.GetHeight(); ++y) {
		for (int x = 0; x < image.GetWidth(); ++x) {
			const int a = image.HasAlpha() ? image.GetAlpha(x, y) : 255;
			out += static_cast<char>(a > 32 ? image.GetRed(x, y) / 16 : 0);
			out += static_cast<char>(a > 32 ? image.GetGreen(x, y) / 16 : 0);
			out += static_cast<char>(a > 32 ? image.GetBlue(x, y) / 16 : 0);
		}
	}
	return out;
}

} // namespace

// Every button the toolbar shows has a picture.
TEST(DebugToolbarIcons, EveryCommand_HasAPicture)
{
	for (const DebugIcon& icon : AllDebugIcons()) {
		const wxBitmapBundle bundle = wxArtProvider::GetBitmapBundle(icon.id, wxART_DEBUG, wxSize(16, 16));
		EXPECT_TRUE(bundle.IsOk()) << icon.name;
	}
}

// The markup parses AND draws something: a blank button is a typo in the SVG, and it would not raise anything.
TEST(DebugToolbarIcons, EveryPicture_DrawsSomething_AtOneAndTwoTimes)
{
	for (const DebugIcon& icon : AllDebugIcons()) {
		for (const int side : { 16, 32 }) {
			const wxImage image = Draw(icon.id, wxSize(side, side));
			ASSERT_TRUE(image.IsOk()) << icon.name << " @" << side;
			EXPECT_EQ(image.GetWidth(), side) << icon.name;
			EXPECT_GT(VisiblePixels(image), side) << icon.name << " @" << side << " is blank";
			// It is a glyph, not a filled tile: some of the square stays clear.
			EXPECT_LT(VisiblePixels(image), side * side) << icon.name << " @" << side << " fills the whole square";
		}
	}
}

// Two buttons that look the same cannot be told apart; each command has a picture of its own.
TEST(DebugToolbarIcons, NoTwoCommands_ShareAPicture)
{
	std::map<std::string, std::string> seen;   // fingerprint -> the command that had it first
	for (const DebugIcon& icon : AllDebugIcons()) {
		const std::string print = Fingerprint(Draw(icon.id, wxSize(32, 32)));
		const auto inserted = seen.emplace(print, icon.name);
		EXPECT_TRUE(inserted.second) << icon.name << " looks the same as " << inserted.first->second;
	}
}

// Sharp at every scale: the same id at a larger size gives a larger picture, not a stretched small one. The
// bundle carries the master at the size asked and at twice and four times it (artProvider.cpp, BundleOf).
TEST(DebugToolbarIcons, ScalesWithTheDisplay)
{
	const wxBitmapBundle bundle = wxArtProvider::GetBitmapBundle(wxART_DEBUG_STEP_OVER, wxART_DEBUG, wxSize(16, 16));
	ASSERT_TRUE(bundle.IsOk());
	EXPECT_EQ(bundle.GetBitmap(wxSize(16, 16)).GetSize(), wxSize(16, 16));
	EXPECT_EQ(bundle.GetBitmap(wxSize(48, 48)).GetSize(), wxSize(48, 48));

	// At 32 the picture is the master's own, not the 16 one blown up: they differ pixel for pixel.
	const wxImage drawn = bundle.GetBitmap(wxSize(32, 32)).ConvertToImage();
	const wxImage stretched = bundle.GetBitmap(wxSize(16, 16)).ConvertToImage().Scale(32, 32, wxIMAGE_QUALITY_NEAREST);
	EXPECT_NE(Fingerprint(drawn), Fingerprint(stretched)) << "the 32 px picture is the 16 px one stretched";
}

// An id that is not a debug picture gets none, rather than somebody else's.
TEST(DebugToolbarIcons, AnUnknownId_GetsNothing)
{
	const wxBitmapBundle bundle = wxArtProvider::GetBitmapBundle(wxART_MAKE_ART_ID(wxART_DEBUG_NOT_A_THING), wxART_DEBUG, wxSize(16, 16));
	EXPECT_FALSE(bundle.IsOk());
}
