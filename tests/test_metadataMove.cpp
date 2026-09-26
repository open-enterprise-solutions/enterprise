// =============================================================================
// OES Enterprise — the order of an object among its siblings.
//
// The order of an object's children is data: the sections of a configuration come out in the
// navigation panel in the order the tree holds them. The door is the metaobject's own
// ChangeChildPosition — it moves, marks the configuration modified and announces `Moved`. The MCP verb
// metadata_move puts an object where another of its kind stands (the position of that one), which is
// what the designer's up and down do with the neighbouring row.
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

// Where `other` stands, `object` goes — what metadata_move and the designer's up / down ask the door.
bool PutAt(ibValueMetaObject* object, ibValueMetaObject* other) {
	ibValueMetaObject* const parent = object->GetParent();
	return parent->ChangeChildPosition(object, parent->GetChildPosition(other));
}

std::vector<wxString> Names(std::initializer_list<const wxChar*> list) {
	std::vector<wxString> names;
	for (const wxChar* n : list) names.push_back(n);
	return names;
}

} // namespace

TEST(MetaObjectOrder, TheFixture_HoldsTheSectionsInTheOrderTheyWereMade)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
}

TEST(MetaObjectOrder, TheLast_PutWhereTheFirstStands_BecomesFirst)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	EXPECT_TRUE(PutAt(f.D, f.A));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("D"), wxT("A"), wxT("B"), wxT("C") }));
}

TEST(MetaObjectOrder, TheFirst_PutWhereTheLastStands_BecomesLast)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	EXPECT_TRUE(PutAt(f.A, f.D));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("B"), wxT("C"), wxT("D"), wxT("A") }));
}

// Up and down are the neighbour's position: one place each way.
TEST(MetaObjectOrder, UpAndDown_AreTheNeighboursPosition)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	EXPECT_TRUE(PutAt(f.C, f.B));   // C up
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("C"), wxT("B"), wxT("D") }));

	EXPECT_TRUE(PutAt(f.C, f.B));   // C down again
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
}

// Already where it was asked to be: an answer of yes, and nothing changed, nothing announced, nothing
// left modified.
TEST(MetaObjectOrder, AlreadyThere_IsYesButChangesNothingAndSaysNothing)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	f.cfg.Modify(false);

	StageRecorder recorder;
	f.cfg.AddNotifier(&recorder);

	EXPECT_TRUE(PutAt(f.B, f.B));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
	EXPECT_EQ(recorder.Count(ibMetaDataNotifier::ibMetaStage::Moved), 0u);
	EXPECT_FALSE(f.cfg.IsModified()) << "a move that moved nothing changed nothing";

	f.cfg.RemoveNotifier(&recorder);
}

TEST(MetaObjectOrder, AMove_IsAnnouncedOnceAsMovedOfTheObjectThatMoved)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	StageRecorder recorder;
	f.cfg.AddNotifier(&recorder);

	ASSERT_TRUE(PutAt(f.D, f.A));

	EXPECT_EQ(recorder.Count(ibMetaDataNotifier::ibMetaStage::Moved), 1u);
	ASSERT_FALSE(recorder.objects.empty());
	EXPECT_EQ(recorder.objects.back(), f.D);

	f.cfg.RemoveNotifier(&recorder);
}

TEST(MetaObjectOrder, AMove_MarksTheConfigurationModified)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	f.cfg.Modify(false);

	ASSERT_TRUE(PutAt(f.D, f.A));
	EXPECT_TRUE(f.cfg.IsModified()) << "a changed order has to be saved like any other change";
}

// ---- refusals -----------------------------------------------------------------------------------

TEST(MetaObjectOrder, APositionPastTheChildren_IsRefusedAndNothingMoves)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);

	EXPECT_FALSE(f.root->ChangeChildPosition(f.B, f.root->GetChildCount()));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
}

TEST(MetaObjectOrder, AReadOnlyConfiguration_RefusesTheMoveAndSaysNothing)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	f.cfg.Modify(false);
	f.cfg.SetReadOnly(true);

	StageRecorder recorder;
	f.cfg.AddNotifier(&recorder);

	EXPECT_FALSE(PutAt(f.D, f.A));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("A"), wxT("B"), wxT("C"), wxT("D") }));
	EXPECT_EQ(recorder.Count(ibMetaDataNotifier::ibMetaStage::Moved), 0u);
	EXPECT_FALSE(f.cfg.IsModified());

	f.cfg.RemoveNotifier(&recorder);
	f.cfg.SetReadOnly(false);
}

// Children of ONE object keep their own order when a sibling of theirs is reordered elsewhere.
TEST(MetaObjectOrder, ReorderingOneLevel_LeavesTheOrderOfAnotherAlone)
{
	SectionsFix f;
	ASSERT_NE(nullptr, f.D);
	ibValueMetaObject* x = f.Make(wxT("X"), f.A);
	ibValueMetaObject* y = f.Make(wxT("Y"), f.A);
	ASSERT_NE(nullptr, y);

	ASSERT_TRUE(PutAt(f.D, f.A));
	EXPECT_EQ(SectionsFix::Order(f.A), Names({ wxT("X"), wxT("Y") }));

	ASSERT_TRUE(PutAt(y, x));
	EXPECT_EQ(SectionsFix::Order(f.A), Names({ wxT("Y"), wxT("X") }));
	EXPECT_EQ(SectionsFix::Order(f.root), Names({ wxT("D"), wxT("A"), wxT("B"), wxT("C") }));
}
