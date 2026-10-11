// =============================================================================
// OES Enterprise — ibValueArray tests
//
// ibValueArray (backend/system/value/valueArray.h) is the script Array value:
// Add / Insert / Remove / Find / Contains / Sort / Sum over a vector<ibValue>.
// The LINQ pipeline and block-syntax orderby/distinct lean on it. Pure (no DB).
//
// RemoveByIndex is a regression guard: Remove(index) used to std::find the
// element whose VALUE equalled `index` (remove-by-value) instead of erasing the
// element AT that index — now fixed.
// =============================================================================

#include <gtest/gtest.h>
#include <vector>
#include "backend/system/value/valueArray.h"
#include "backend/backend_exception.h"
#include "backend/appData.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaSessionParameterObject.h"
#include "backend/session/session.h"
#include "backend/typeDescription.h"

TEST(ValueArray, EmptyByDefault) {
    ibValueArray a;
    EXPECT_TRUE(a.IsEmpty());
    EXPECT_EQ(a.Count(), 0u);
}

TEST(ValueArray, AddIncreasesCount) {
    ibValueArray a;
    a.Add(ibValue(ibNumber(1)));
    a.Add(ibValue(wxString(wxT("x"))));
    EXPECT_FALSE(a.IsEmpty());
    EXPECT_EQ(a.Count(), 2u);
}

TEST(ValueArray, ContainsAndFind) {
    ibValueArray a;
    a.Add(ibValue(ibNumber(10)));
    a.Add(ibValue(ibNumber(20)));
    EXPECT_TRUE(a.Contains(ibValue(ibNumber(20))));
    EXPECT_FALSE(a.Contains(ibValue(ibNumber(99))));
    EXPECT_EQ(a.Find(ibValue(ibNumber(20))).GetInteger(), 1);   // index 1
    EXPECT_TRUE(a.Find(ibValue(ibNumber(99))).IsEmpty());        // miss -> TYPE_EMPTY
}

TEST(ValueArray, InsertAtIndex) {
    ibValueArray a;
    a.Add(ibValue(ibNumber(1)));
    a.Add(ibValue(ibNumber(3)));
    a.Insert(1, ibValue(ibNumber(2)));                           // [1, 2, 3]
    EXPECT_EQ(a.Count(), 3u);
    EXPECT_EQ(a.Find(ibValue(ibNumber(2))).GetInteger(), 1);
}

TEST(ValueArray, RemoveByIndex) {
    ibValueArray a;
    a.Add(ibValue(ibNumber(10)));
    a.Add(ibValue(ibNumber(20)));
    a.Add(ibValue(ibNumber(30)));
    a.Remove(1);                                                 // element AT index 1 (value 20)
    EXPECT_EQ(a.Count(), 2u);
    EXPECT_TRUE (a.Contains(ibValue(ibNumber(10))));
    EXPECT_FALSE(a.Contains(ibValue(ibNumber(20))));            // the removed one
    EXPECT_TRUE (a.Contains(ibValue(ibNumber(30))));
}

TEST(ValueArray, ClearEmpties) {
    ibValueArray a;
    a.Add(ibValue(ibNumber(1)));
    a.Clear();
    EXPECT_TRUE(a.IsEmpty());
}

TEST(ValueArray, SortAscending) {
    ibValueArray a;
    a.Add(ibValue(ibNumber(3)));
    a.Add(ibValue(ibNumber(1)));
    a.Add(ibValue(ibNumber(2)));
    a.Sort();
    EXPECT_EQ(a.Find(ibValue(ibNumber(1))).GetInteger(), 0);
    EXPECT_EQ(a.Find(ibValue(ibNumber(3))).GetInteger(), 2);
}

TEST(ValueArray, ConstructFromVector) {
    std::vector<ibValue> v{ ibValue(ibNumber(5)), ibValue(ibNumber(6)) };
    ibValueArray a(v);
    EXPECT_EQ(a.Count(), 2u);
    EXPECT_TRUE(a.Contains(ibValue(ibNumber(6))));
}

TEST(ValueArray, SumNumeric) {
    ibValueArray a;
    a.Add(ibValue(ibNumber(10)));
    a.Add(ibValue(ibNumber(20)));
    a.Add(ibValue(ibNumber(30)));
    EXPECT_EQ(a.Sum().GetInteger(), 60);
}

TEST(ValueArray, AFixedArrayIsACopyAndNewArrayCopiesItBack) {
    ibValueArray* made = new ibValueArray();
    made->Add(ibValue(ibNumber(10)));
    made->Add(ibValue(ibNumber(20)));
    ibValue source(made);

    ibValue* arg = &source;
    ibValueFixedArray* fixed = new ibValueFixedArray();
    ASSERT_TRUE(fixed->Init(&arg, 1));
    ibValue held(fixed);
    EXPECT_EQ(fixed->Count(), 2u);
    ibValue at;
    ASSERT_TRUE(fixed->GetAt(ibValue(1), at));
    EXPECT_EQ(at.GetInteger(), 20);
    EXPECT_THROW(fixed->SetAt(ibValue(0), ibValue(ibNumber(1))), ibBackendException);

    ibValue walked;
    auto cursor = fixed->CreateIterator();
    ASSERT_TRUE(cursor->MoveNext(walked));
    EXPECT_EQ(walked.GetInteger(), 10);

    ibValue* copyArg = &held;
    ibValueArray copy;
    ASSERT_TRUE(copy.Init(&copyArg, 1));
    EXPECT_EQ(copy.Count(), 2u);
    copy.Add(ibValue(ibNumber(30)));
    EXPECT_EQ(copy.Count(), 3u);
    EXPECT_EQ(fixed->Count(), 2u);

    ibValue* again = &source;
    ibValueArray fromArray;
    ASSERT_TRUE(fromArray.Init(&again, 1));
    EXPECT_EQ(fromArray.Count(), 2u);
}

TEST(ValueArray, ANumberStillSizesTheArray) {
    ibValue count(3);
    ibValue* arg = &count;
    ibValueArray a;
    ASSERT_TRUE(a.Init(&arg, 1));
    EXPECT_EQ(a.Count(), 3u);
}

TEST(ValueArray, NewArrayOfASessionParameterValueCopiesWhatItStores) {
    struct Env {
        bool ready;
        Env() : ready(ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE)) {}
        ~Env() {
            if (ibApplicationInstance::Get() != nullptr)
                ibApplicationInstance::DestroyAppDataEnv();
        }
    } env;
    if (!env.ready)
        GTEST_SKIP() << "appData env unavailable";

    auto session = std::make_shared<ibSession>(wxT("fixed-array"), ibSessionKind::Designer);
    ibSessionScope scope(session.get());
    ibMetaDataConfigurationFile cfg;
    ibValueMetaObject* created = cfg.CreateMetaObject(
        g_metaSessionParameterCLSID, cfg.GetCommonMetaObject(), false);
    ASSERT_NE(created, nullptr);
    created->SetName(wxT("ErrorStack"));
    auto* declaration = dynamic_cast<ibValueMetaObjectSessionParameter*>(created);
    ASSERT_NE(declaration, nullptr);
    declaration->GetTypeDesc() = ibTypeDescription(g_valueArrayCLSID);

    ibValue holder(new ibValueSessionParameter(declaration));
    ibValue* arg = &holder;
    ibValueArray copy;
    ASSERT_TRUE(copy.Init(&arg, 1));
    EXPECT_EQ(copy.Count(), 0u);
}
