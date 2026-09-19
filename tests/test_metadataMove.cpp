// =============================================================================
// OES Enterprise — the order of an object among its siblings (ibMetaData::MoveMetaObject).
//
// The order of an object's children is data: the sections of a configuration come out in the
// navigation panel in the order the tree holds them. Only the designer's drag could change it; the
// door is now on the metadata, and the MCP verb metadata_move is a thin shell over it.
//
// DB-FREE AND UI-FREE, like test_metadataPasteIdentity: a fresh ibMetaDataConfigurationFile (public
// ctor, never "run"), sections created with runObject=false.
// =============================================================================

#include <gtest/gtest.h>

#include <vector>

#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaSectionObject.h"   // g_metaSectionCLSID

namespace {

// Watches for the stages the door announces.
struct StageRecorder : ibMetaDataNotifier {
	std::vector<ibMetaStage> stages;
	std::vector<ibValueMetaObject*> objects;

	void MetaObjectChanged(ibMetaStage stage, ibValueMetaObject* object) override {
		stages.push_back(stage);
		objects.push_back(object);
	}

	size_t Count(ibMetaStage stage) const {
		size_t n = 0;
		for (ibMetaStage s : stages) if (s == stage) ++n;
		return n;
	}
};

// Four sections A, B, C, D in one fresh configuration, in that order.
struct SectionsFix {
	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = nullptr;
	ibValueMetaObject* A = nullptr;
	ibValueMetaObject* B = nullptr;
	ibValueMetaObject* C = nullptr;
	ibValueMetaObject* D = nullptr;

	SectionsFix() {
		root = cfg.GetCommonMetaObject();
		if (root == nullptr)
			return;
		A = Make(wxT("A")); B = Make(wxT("B")); C = Make(wxT("C")); D = Make(wxT("D"));
	}

	ibValueMetaObject* Make(const wxString& name, ibValueMetaObject* parent = nullptr) {
		ibValueMetaObject* section = cfg.CreateMetaObject(g_metaSectionCLSID, parent != nullptr ? parent : root, /*runObject*/ false);
		if (section != nullptr)
			cfg.RenameMetaObject(section, name);
		return section;
	}

	// The names of the sections directly under `parent`, in the order the tree holds them.
	static std::vector<wxString> Order(ibValueMetaObject* parent) {
		std::vector<wxString> names;
		for (unsigned int idx = 0; idx < parent->GetChildCount(); ++idx) {
			ibValueMetaObject* child = parent->GetChild(idx);
			if (child->GetClassType() == g_metaSectionCLSID)
				names.push_back(child->GetName());
		}
		return names;
	}
};

std::vector<wxString> Names(std::initializer_list<const wxChar*> list) {
	std::vector<wxString> names;
	for (const wxChar* n : list) names.push_back(n);
	return names;
}

} // namespace

TEST(MoveMetaObject, TheFixture_HoldsTheSectionsInTheOrderTheyWereMade)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
}

TEST(MoveMetaObject, Before_TheLastPutInFrontOfTheFirst_BecomesFirst)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	bool changed = false;
	EXPECT_TRUE(f.cfg.MoveMetaObject(f.D, f.A, /*before*/ true, &changed));
	EXPECT_TRUE(changed);
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("D"), wxT("A"), wxT("B"), wxT("C") }));
}

TEST(MoveMetaObject, After_TheFirstPutBehindTheLast_BecomesLast)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	bool changed = false;
	EXPECT_TRUE(f.cfg.MoveMetaObject(f.A, f.D, /*before*/ false, &changed));
	EXPECT_TRUE(changed);
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("B"), wxT("C"), wxT("D"), wxT("A") }));
}

// The sibling stood AFTER the object, so it is one place earlier by the time the object is put back:
// "before C" for A is between B and C, not between C and D.
TEST(MoveMetaObject, Before_ASiblingFurtherOn_LandsJustInFrontOfIt)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	EXPECT_TRUE(f.cfg.MoveMetaObject(f.A, f.C, /*before*/ true));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("B"), wxT("A"), wxT("C"), wxT("D") }));
}

TEST(MoveMetaObject, After_ASiblingFurtherBack_LandsJustBehindIt)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	EXPECT_TRUE(f.cfg.MoveMetaObject(f.D, f.A, /*before*/ false));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("D"), wxT("B"), wxT("C") }));
}

// Already where it was asked to be: an answer of yes, and nothing changed, nothing announced.
TEST(MoveMetaObject, AlreadyThere_IsYesButChangesNothingAndSaysNothing)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	StageRecorder recorder;
	f.cfg.AddNotifier(&recorder);

	bool changed = true;
	EXPECT_TRUE(f.cfg.MoveMetaObject(f.B, f.A, /*before*/ false, &changed));   // B is already behind A
	EXPECT_FALSE(changed);
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
	EXPECT_EQ(recorder.Count(ibMetaDataNotifier::ibMetaStage::Moved), 0u);

	f.cfg.RemoveNotifier(&recorder);
}

// How a whole list is arranged: each one goes after the one before it.
TEST(MoveMetaObject, ChainingAfter_ArrangesAWholeListInTheOrderAsked)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	const std::vector<ibValueMetaObject*> wanted = { f.C, f.A, f.D, f.B };
	for (size_t i = 1; i < wanted.size(); ++i)
		ASSERT_TRUE(f.cfg.MoveMetaObject(wanted[i], wanted[i - 1], /*before*/ false)) << "step " << i;

	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("C"), wxT("A"), wxT("D"), wxT("B") }));
}

TEST(MoveMetaObject, AMove_IsAnnouncedOnceAsMovedOfTheObjectThatMoved)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	StageRecorder recorder;
	f.cfg.AddNotifier(&recorder);

	ASSERT_TRUE(f.cfg.MoveMetaObject(f.D, f.A, true));

	EXPECT_EQ(recorder.Count(ibMetaDataNotifier::ibMetaStage::Moved), 1u);
	ASSERT_FALSE(recorder.objects.empty());
	EXPECT_EQ(recorder.objects.back(), f.D);

	f.cfg.RemoveNotifier(&recorder);
}

TEST(MoveMetaObject, AMove_MarksTheConfigurationModified)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	f.cfg.Modify(false);

	ASSERT_TRUE(f.cfg.MoveMetaObject(f.D, f.A, true));
	EXPECT_TRUE(f.cfg.IsModified()) << "a changed order has to be saved like any other change";
}

// ---- refusals -----------------------------------------------------------------------------------

TEST(MoveMetaObject, PlacedRelativeToItself_IsRefusedAndNothingMoves)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	bool changed = true;
	EXPECT_FALSE(f.cfg.MoveMetaObject(f.B, f.B, true, &changed));
	EXPECT_FALSE(changed);
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
}

TEST(MoveMetaObject, TwoObjectsThatAreNotSiblings_AreRefusedAndNothingMoves)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	ibValueMetaObject* nested = f.Make(wxT("Inner"), f.A);   // a section INSIDE A
	ASSERT_NE(nullptr, nested);

	bool changed = true;
	EXPECT_FALSE(f.cfg.MoveMetaObject(nested, f.B, true, &changed)) << "Inner sits under A, B under the root";
	EXPECT_FALSE(changed);
	EXPECT_FALSE(f.cfg.MoveMetaObject(f.B, nested, false, &changed));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
}

TEST(MoveMetaObject, TheRootAndNothing_AreRefused)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	EXPECT_FALSE(f.cfg.MoveMetaObject(f.root, f.A, true)) << "the root has no siblings";
	EXPECT_FALSE(f.cfg.MoveMetaObject(nullptr, f.A, true));
	EXPECT_FALSE(f.cfg.MoveMetaObject(f.A, nullptr, true));
}

// Children of ONE object keep their own order when a sibling of theirs is reordered elsewhere.
TEST(MoveMetaObject, ReorderingOneLevel_LeavesTheOrderOfAnotherAlone)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	ibValueMetaObject* x = f.Make(wxT("X"), f.A);
	ibValueMetaObject* y = f.Make(wxT("Y"), f.A);
	ASSERT_NE(nullptr, y);

	ASSERT_TRUE(f.cfg.MoveMetaObject(f.D, f.A, true));
	EXPECT_EQ(SectionsFix::Order(f.A), Names({ wxT("X"), wxT("Y") }));

	ASSERT_TRUE(f.cfg.MoveMetaObject(y, x, true));
	EXPECT_EQ(SectionsFix::Order(f.A), Names({ wxT("Y"), wxT("X") }));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("D"), wxT("A"), wxT("B"), wxT("C") }));
}
