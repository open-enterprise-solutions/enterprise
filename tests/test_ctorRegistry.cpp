// =============================================================================
// OES Enterprise — the ctor registry: identity vs presentation
//
// A registered type has TWO keys, and they are not the same kind of thing:
//
//   * the clsid is its IDENTITY. For a metaobject type the body of that id IS the
//     metaID (clsid.h, constructive), so it survives everything the user does to
//     the object — including renaming it.
//   * the name is a PRESENTATION. A metaobject ctor COMPUTES it from the object
//     ("ConstantObject." + GetName()), so it changes the moment the object is
//     renamed. The registry's by-name map is therefore a CACHE of a computed
//     value, not a store — and a cache has to be told when it went stale.
//
// This is what these tests pin down, because getting it wrong was expensive: a
// rename used to leave the name index pointing at the old string, so the type was
// unfindable under the name on screen, and teardown — which asked by NAME — raised
// "Object '…' is not exist" in the middle of closing a configuration.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/ctorRegistry.h"

namespace {

// A stand-in for a metaobject's ctor: the registry only ever asks a type for its
// identity, its name and its type_info. The name is MUTABLE here for the same
// reason it is computed in the real thing — the metaobject behind it gets renamed.
class ibFakeCtor {
public:
	ibFakeCtor(ibClassID clsid, const wxString& name)
		: m_clsid(clsid), m_name(name) {}

	ibClassID             GetClassType() const { return m_clsid; }
	wxString              GetClassName() const { return m_name; }
	const std::type_info& GetTypeInfo()  const { return typeid(void); }

	void Rename(const wxString& name) { m_name = name; }

private:
	const ibClassID m_clsid;
	wxString        m_name;
};

// Identities as the engine builds them for metaobject types: kind in the high byte,
// metaID as the body. Two different metaIDs, so nothing collides.
constexpr ibClassID kObjectOfMeta42 = object_to_clsid(42);
constexpr ibClassID kObjectOfMeta43 = object_to_clsid(43);

} // namespace

// --- the two keys resolve to the same entry -----------------------------------
TEST(CtorRegistry, FindsByIdentityAndByName) {
	ibCtorRegistry<ibFakeCtor> registry;
	registry.Register(new ibFakeCtor(kObjectOfMeta42, wxT("ConstantObject.Rate")));

	EXPECT_NE(registry.Find(kObjectOfMeta42), nullptr);
	EXPECT_NE(registry.Find(wxString(wxT("ConstantObject.Rate"))), nullptr);
	EXPECT_EQ(registry.Find(kObjectOfMeta42), registry.Find(wxString(wxT("ConstantObject.Rate"))));
}

// --- name lookup is case-insensitive (OES convention) --------------------------
TEST(CtorRegistry, NameLookupIgnoresCase) {
	ibCtorRegistry<ibFakeCtor> registry;
	registry.Register(new ibFakeCtor(kObjectOfMeta42, wxT("ConstantObject.Rate")));

	EXPECT_NE(registry.Find(wxString(wxT("constantobject.rate"))), nullptr);
	EXPECT_NE(registry.Find(wxString(wxT("CONSTANTOBJECT.RATE"))), nullptr);
}

// --- THE RENAME: identity holds, presentation moves ---------------------------
TEST(CtorRegistry, RenameKeepsIdentityAndRefilesTheName) {
	ibCtorRegistry<ibFakeCtor> registry;
	ibFakeCtor* ctor = new ibFakeCtor(kObjectOfMeta42, wxT("ConstantObject.Rate"));
	registry.Register(ctor);

	ctor->Rename(wxT("ConstantObject.Price"));
	registry.InvalidateNames();   // what a metaobject says when its name property changed

	// The identity never moved — that is the whole point of keying on it.
	EXPECT_EQ(registry.Find(kObjectOfMeta42), ctor);

	// The NEW name resolves…
	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.Price"))), ctor);
	// …and the OLD one does not. A stale entry here is not merely untidy: it is a
	// name resolving to a type that no longer answers to it.
	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.Rate"))), nullptr);
}

// --- a rename with no lookup afterwards costs nothing, and is not lost ---------
TEST(CtorRegistry, RenameIsRecomputedLazilyOnFirstLookup) {
	ibCtorRegistry<ibFakeCtor> registry;
	ibFakeCtor* ctor = new ibFakeCtor(kObjectOfMeta42, wxT("ConstantObject.Rate"));
	registry.Register(ctor);

	// Several renames in a row (an import, a paste that rebuilds names) — the cache
	// is only marked, never rebuilt per rename.
	ctor->Rename(wxT("ConstantObject.A"));
	registry.InvalidateNames();
	ctor->Rename(wxT("ConstantObject.B"));
	registry.InvalidateNames();

	// The first lookup after them sees the LAST name, not an intermediate one.
	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.B"))), ctor);
	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.A"))), nullptr);
}

// --- unregister drops the name entry even when the name has since changed ------
TEST(CtorRegistry, UnregisterAfterRenameLeavesNoStaleName) {
	ibCtorRegistry<ibFakeCtor> registry;
	ibFakeCtor* ctor = new ibFakeCtor(kObjectOfMeta42, wxT("ConstantObject.Rate"));
	registry.Register(ctor);

	// Renamed but NOT looked up: the by-name cache still holds the old string, and
	// the ctor no longer answers with it. Unregister must not depend on that string
	// — it used to recompute the key and erase nothing, leaving an entry that
	// resolved to a freed ctor.
	ctor->Rename(wxT("ConstantObject.Price"));
	registry.Unregister(ctor);

	EXPECT_EQ(registry.Find(kObjectOfMeta42), nullptr);
	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.Rate"))), nullptr);
	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.Price"))), nullptr);
	EXPECT_TRUE(registry.IsEmpty());
}

// --- one object's rename does not disturb another -----------------------------
TEST(CtorRegistry, RebuildKeepsEveryOtherEntry) {
	ibCtorRegistry<ibFakeCtor> registry;
	ibFakeCtor* rate  = new ibFakeCtor(kObjectOfMeta42, wxT("ConstantObject.Rate"));
	ibFakeCtor* other = new ibFakeCtor(kObjectOfMeta43, wxT("ConstantObject.Vat"));
	registry.Register(rate);
	registry.Register(other);

	rate->Rename(wxT("ConstantObject.Price"));
	registry.InvalidateNames();

	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.Price"))), rate);
	EXPECT_EQ(registry.Find(wxString(wxT("ConstantObject.Vat"))), other);
	EXPECT_EQ(registry.Size(), 2u);
}

// --- the identity of a metaobject type carries its metaID ---------------------
TEST(CtorRegistry, IdentityIsConstructiveFromMetaId) {
	// Not a registry property, but the reason the registry can key on it: two kinds
	// of the same metaobject are different ids, and the same kind of two objects
	// never collides — no hashing, no names involved.
	EXPECT_NE(object_to_clsid(42), manager_to_clsid(42));
	EXPECT_NE(object_to_clsid(42), object_to_clsid(43));
	EXPECT_EQ(object_to_clsid(42), kObjectOfMeta42);
}
