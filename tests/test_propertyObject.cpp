// =============================================================================
// OES Enterprise — ibPropertyObject attach-owner / child-change notify tests
//
// A property object can be ATTACHED to an owner (AttachPropertyObject) or be
// given one structurally (a value-table sets it on its column-infos). A change
// on it bubbles UP via OnChildChanged so a holder that CAN react — a frontend
// form-attribute — refreshes. This is the BACKEND seam under the value-table
// column live-refresh, testable with no GUI / metadata / DB.
//
// These guard the attach graph AND its dangling-reference safety: the back-link
// is severed BOTH ways — on detach, and on destruction of either end.
// Pure — no DB / appData.
// =============================================================================

#include <gtest/gtest.h>
#include <wx/string.h>   // wxT, wxString - the engine's headers include no wx
#include "backend/propertyManager/propertyManager.h"   // ibPropertyObject
#include "backend/propertyManager/property/eventControl.h"   // ibEventControl — a control's event, as one is made
#include "backend/propertyManager/property/propertyBoolean.h"   // ibPropertyBoolean — a property, as one is made

#include <initializer_list>
#include <vector>

namespace {

// Minimal concrete property object: implements the three pure virtuals and
// counts child-change notifications so upward propagation is observable.
class TestNode : public ibPropertyObject {
public:
    int childChanged = 0;

    ibString GetClassName() const override { return wxT("TestNode"); }
    bool     IsEditable() const override { return true; }
    ibString GetObjectTypeName() const override { return wxT("TestNode"); }

    void OnChildChanged() override {
        ++childChanged;
        ibPropertyObject::OnChildChanged();   // keep bubbling up the attach-owner chain
    }
};

// …with events of its own, made as a control makes them.
class EventNode : public TestNode {
public:
    explicit EventNode(std::initializer_list<ibString> names) {
        ibPropertyCategory* events = CreatePropertyCategory(ibT("Event"));
        for (const ibString& name : names)
            CreateEvent<ibEventControl>(events, name, std::vector<ibString>{});
    }
};

// …and with properties of its own.
class PropertyNode : public TestNode {
public:
    explicit PropertyNode(std::initializer_list<ibString> names) {
        ibPropertyCategory* properties = CreatePropertyCategory(ibT("Property"));
        for (const ibString& name : names)
            CreateProperty<ibPropertyBoolean>(properties, name);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// Attach sets the upward owner; detach clears it (no dangling back-link)
// ---------------------------------------------------------------------------
TEST(PropertyObjectAttach, AttachSetsOwnerDetachClears) {
    TestNode parent, child;
    EXPECT_EQ(child.GetAttachOwner(), nullptr);

    parent.AttachPropertyObject(&child);
    EXPECT_EQ(child.GetAttachOwner(), &parent);
    ASSERT_EQ(parent.GetAttachedObjects().size(), 1u);

    parent.DetachAllPropertyObjects();
    EXPECT_EQ(child.GetAttachOwner(), nullptr);
    EXPECT_TRUE(parent.GetAttachedObjects().empty());
}

// ---------------------------------------------------------------------------
// An attached object's EVENTS are ours, as its properties are — by name and by
// place, our own first, then the attached in attach order; detached, gone
// ---------------------------------------------------------------------------
TEST(PropertyObjectAttach, AttachedEventsAreFoundByNameAndByPlace) {
    EventNode parent({ ibT("Own") });
    EventNode child({ ibT("First"), ibT("Second") });
    parent.AttachPropertyObject(&child);

    EXPECT_EQ(parent.GetEventCount(), 3u);
    ASSERT_NE(parent.GetEvent(0u), nullptr);
    EXPECT_EQ(parent.GetEvent(0u)->GetName(), ibString(ibT("Own")));
    EXPECT_EQ(parent.GetEvent(2u), child.GetEvent(1u));   // "Second" — the child's by name order
    EXPECT_EQ(parent.GetEvent(3u), nullptr);
    EXPECT_NE(parent.GetEvent(ibString(ibT("First"))), nullptr);
    EXPECT_EQ(parent.GetEvent(ibString(ibT("First"))), child.GetEvent(ibString(ibT("First"))));

    parent.DetachAllPropertyObjects();
    EXPECT_EQ(parent.GetEventCount(), 1u);
    EXPECT_EQ(parent.GetEvent(ibString(ibT("First"))), nullptr);
}

// ---------------------------------------------------------------------------
// …and its PROPERTIES the same way, by the same walk
// ---------------------------------------------------------------------------
TEST(PropertyObjectAttach, AttachedPropertiesAreFoundByNameAndByPlace) {
    PropertyNode parent({ ibT("Own") });
    PropertyNode child({ ibT("First"), ibT("Second") });
    parent.AttachPropertyObject(&child);

    EXPECT_EQ(parent.GetPropertyCount(), 3u);
    ASSERT_NE(parent.GetProperty(0u), nullptr);
    EXPECT_EQ(parent.GetProperty(0u)->GetName(), ibString(ibT("Own")));
    EXPECT_EQ(parent.GetProperty(2u), child.GetProperty(1u));   // "Second" — the child's by name order
    EXPECT_EQ(parent.GetProperty(3u), nullptr);
    EXPECT_NE(parent.GetProperty(ibString(ibT("First"))), nullptr);
    EXPECT_EQ(parent.GetProperty(ibString(ibT("First"))), child.GetProperty(ibString(ibT("First"))));

    parent.DetachAllPropertyObjects();
    EXPECT_EQ(parent.GetPropertyCount(), 1u);
    EXPECT_EQ(parent.GetProperty(ibString(ibT("First"))), nullptr);
}

// ---------------------------------------------------------------------------
// OnChildChanged bubbles up the WHOLE attach-owner chain, starting from the
// owner (as the inspector does) — the edited object's own handler is NOT re-run
// ---------------------------------------------------------------------------
TEST(PropertyObjectAttach, ChildChangeBubblesUpChain) {
    TestNode holder, mid, leaf;
    holder.AttachPropertyObject(&mid);    // mid.owner  = holder
    mid.AttachPropertyObject(&leaf);      // leaf.owner = mid

    leaf.GetAttachOwner()->OnChildChanged();   // notify the owner, from the edited leaf

    EXPECT_EQ(mid.childChanged, 1);       // mid heard it
    EXPECT_EQ(holder.childChanged, 1);    // and it climbed all the way to the holder
    EXPECT_EQ(leaf.childChanged, 0);      // the edited object's own handler is not re-fired
}

// ---------------------------------------------------------------------------
// Destroying a CHILD removes it from the owner's downward list (no dangle)
// ---------------------------------------------------------------------------
TEST(PropertyObjectAttach, DestroyedChildLeavesOwnerList) {
    TestNode parent;
    {
        TestNode child;
        parent.AttachPropertyObject(&child);
        ASSERT_EQ(parent.GetAttachedObjects().size(), 1u);
    }   // child destroyed here -> its dtor drops itself from parent's list
    EXPECT_TRUE(parent.GetAttachedObjects().empty());
}

// ---------------------------------------------------------------------------
// Destroying the OWNER clears the child's upward back-link; a later notify on
// the now-orphan child is a safe no-op (no use-after-free)
// ---------------------------------------------------------------------------
TEST(PropertyObjectAttach, DestroyedOwnerClearsChildBackLink) {
    TestNode child;
    {
        TestNode parent;
        parent.AttachPropertyObject(&child);
        ASSERT_EQ(child.GetAttachOwner(), &parent);
    }   // parent destroyed -> DetachAllPropertyObjects clears the child's owner
    EXPECT_EQ(child.GetAttachOwner(), nullptr);

    child.OnChildChanged();   // orphan child: base sees no owner -> no-op, no crash
    SUCCEED();
}
