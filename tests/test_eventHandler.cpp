// =============================================================================
// Event handlers — what a handler's source may name, and how it knows its event.
//
// An event handler handles one event of the types its Source names: an object, a manager, a record set,
// of one metaobject or of every metaobject of a metatype. The families that make "every" sayable are
// registered by the platform itself, one for each kind a metatype HAS (s_features, metaObject.h):
// `DocumentRef`, `DocumentObject`, `DocumentManager`, `AccumulationRegisterRecordSet`…
//
// What is pinned here, with no database and no configuration:
//
//   1. a family is named by its metatype and its kind's prefix without the dot;
//   2. a metatype has a family of each kind it has — and none of a kind it does not;
//   3. a family admits its members and nobody else: the gate a handler's source asks;
//   4. the event source filter offers only what raises an event, asked of the configuration's own types —
//      with no configuration, nothing (a family is offered through a member that raises one);
//   5. an event's number is its name's own: the same whoever asks, and different events differ.
// =============================================================================

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "backend/compiler/value.h"
#include "backend/metaCtor.h"                                     // ibCtorMetaAnyKind / ib_find_meta_any_kind
#include "backend/backend_type.h"                                 // GetTypesByFilter
#include "backend/metaCollection/metaEventHandlerObject.h"        // EventId
#include "backend/system/value/valueType.h"                       // ibValueTypeDescription::AllowValue
#include "backend/typeDescription.h"

// 1 - the family's name
TEST(EventHandler, AFamilyIsNamedByItsMetatypeAndKind)
{
	EXPECT_EQ(ibCtorMetaAnyKind::NameOf(wxT("Document"), ibCtorObjectMetaType_Reference), wxString(wxT("DocumentRef")));
	EXPECT_EQ(ibCtorMetaAnyKind::NameOf(wxT("Document"), ibCtorObjectMetaType_Object), wxString(wxT("DocumentObject")));
	EXPECT_EQ(ibCtorMetaAnyKind::NameOf(wxT("Document"), ibCtorObjectMetaType_Manager), wxString(wxT("DocumentManager")));
	EXPECT_EQ(ibCtorMetaAnyKind::NameOf(wxT("AccumulationRegister"), ibCtorObjectMetaType_RecordSet),
		wxString(wxT("AccumulationRegisterRecordSet")));
}

// 2 - the families a metatype has
TEST(EventHandler, AMetatypeHasAFamilyOfEachKindItHas)
{
	EXPECT_NE(ib_find_meta_any_kind(wxT("Document"), ibCtorObjectMetaType_Reference), nullptr);
	EXPECT_NE(ib_find_meta_any_kind(wxT("Document"), ibCtorObjectMetaType_Object), nullptr);
	EXPECT_NE(ib_find_meta_any_kind(wxT("Document"), ibCtorObjectMetaType_Manager), nullptr);
	EXPECT_EQ(ib_find_meta_any_kind(wxT("Document"), ibCtorObjectMetaType_RecordSet), nullptr) << "a document keeps no record set";

	EXPECT_NE(ib_find_meta_any_kind(wxT("AccumulationRegister"), ibCtorObjectMetaType_RecordSet), nullptr);
	EXPECT_NE(ib_find_meta_any_kind(wxT("AccumulationRegister"), ibCtorObjectMetaType_Manager), nullptr);
	EXPECT_EQ(ib_find_meta_any_kind(wxT("AccumulationRegister"), ibCtorObjectMetaType_Object), nullptr) << "a register has no object";

	EXPECT_EQ(ib_find_meta_any_kind(wxT("Enumeration"), ibCtorObjectMetaType_Object), nullptr) << "an enumeration's values are no objects";
}

// 3 - a family's gate
TEST(EventHandler, AFamilyAdmitsItsMembersOnly)
{
	ibCtorMetaAnyKind* family = ib_find_meta_any_kind(wxT("Document"), ibCtorObjectMetaType_Object);
	ASSERT_NE(family, nullptr);

	const ibClassID member   = make_clsid("EventHandlerTestMember",   ibClassKind_Object);
	const ibClassID stranger = make_clsid("EventHandlerTestStranger", ibClassKind_Object);
	const ibTypeDescription source(std::vector<ibClassID>{ family->GetClassType() });

	family->AddMember(member);
	EXPECT_TRUE(ibValueTypeDescription::AllowValue(source, member)) << "every document's object is a document's object";
	EXPECT_FALSE(ibValueTypeDescription::AllowValue(source, stranger)) << "an object nobody registered is not";

	family->RemoveMember(member);
	EXPECT_FALSE(ibValueTypeDescription::AllowValue(source, member)) << "a member that went is forgotten";
}

// 4 - the event source filter asks the configuration
TEST(EventHandler, TheSourceFilterAsksTheConfiguration)
{
	std::vector<ibClassID> offered;
	ibBackendTypeConfigFactory::GetTypesByFilter(ibSelectorDataType::ibSelectorDataType_eventSource, nullptr, offered);

	EXPECT_TRUE(offered.empty()) << "what raises an event is asked of the configuration's own types - with none, nothing";
}

// 5 - an event's number
TEST(EventHandler, AnEventIsNumberedByItsName)
{
	const wxString events[] = {
		wxT("BeforeWrite"), wxT("OnWrite"), wxT("BeforeDelete"), wxT("OnDelete"), wxT("Posting"), wxT("UndoPosting"),
		wxT("Filling"), wxT("OnCopy"), wxT("SetNewCode"), wxT("SetNewNumber"), wxT("JobProcessing"),
	};
	std::set<long> ids;
	for (const wxString& event : events) {
		const long id = ibValueMetaObjectEventHandler::EventId(event);
		EXPECT_GE(id, 0) << event.ToStdString() << " - wxNOT_FOUND stays free for \"none\"";
		EXPECT_EQ(id, ibValueMetaObjectEventHandler::EventId(event)) << "the same whoever asks";
		ids.insert(id);
	}
	EXPECT_EQ(ids.size(), sizeof(events) / sizeof(events[0])) << "different events are different numbers";
}
