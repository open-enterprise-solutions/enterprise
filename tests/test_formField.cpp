// =============================================================================
// OES Enterprise — the field: one control, its view a child (docs/private/form-field.md).
//
// A field's View makes a child that holds the view's properties, events and behaviour; outside there is one control.
// So every check here goes through the doors any control has — the frame, the type factory, the property object —
// and never names the child. These pin:
//
//   * a field is born an Input, and its view, its view's properties and its handlers go out with the form, come back
//     with it and ride the studio's copy;
//   * a view's events and properties are the field's: counted, found by name, and members a script reads and writes;
//   * what two views share by name crosses over a change of view — on the studio's road (the form read anew from
//     its node) and on the live one (the property changed), a lambda a script hung on included;
//   * a view that binds is not shown unbound; a Label is;
//   * a Flag writes a Boolean, a Switch the value of the item picked, a Slider a Number within its ends — and none writes
//     through a read-only field; a Gauge shows its value within its range;
//   * a view is offered by its source's type and refused otherwise; one the type does not allow — set past the door,
//     left by a source of another type (at once, never drawn), or saved before the type changed — gives way to an Input;
//   * a switch's items are read and written by a script as a value list, and by MCP as [{value, presentation}];
//   * an Input is today's text field: its word over its type's in its doors, its own links, its mask in the state;
//   * a view this version does not know loads as an Input.
//
// DB-FREE AND UI-FREE: a fresh ibMetaDataApplicationFile that is never run, a form made in the test on a session with
// a frame (framedSession.h), and a field made by its own ctor. Whether a handler RUNS is not asked here — that needs
// the application's runtime; it is the same CallAsEvent door every control fires through.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/init.h>
#include <wx/log.h>                                  // wxLogStderr — a warning must not become a modal box

#include "backend/metadataApplication.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/appData.h"                         // the environment a form is made in
#include "backend/objCtor.h"                         // ibCtorAbstractType — a field made by its own ctor
#include "backend/metadataSerialization.h"           // ibSerializeValue — a switch's item as its row stores it
#include "backend/compiler/compileCode.h"            // a lambda, made by the compiler
#include "backend/compiler/procUnit.h"
#include "backend/compiler/procUnitLambda.h"         // AsFunction — the lambda a handler holds
#include "backend/propertyManager/property/propertyEnum.h"
#include "backend/propertyManager/property/propertyChoiceLink.h"
#include "backend/propertyManager/property/propertyChoices.h"
#include "backend/propertyManager/property/variant/variantChoiceLink.h"   // ibChoiceHolderFields — what a row may read
#include "backend/system/value/valueList.h"          // ibValueValueList — a switch's items as a script holds them
#include "core/serialize/dataBuilder.h"
#include "core/inputMask.h"

#include "frmserver/visualView/ctrl/field.h"         // g_controlFieldCLSID
#include "frmserver/visualView/ctrl/form.h"
#include "frmserver/visualView/ctrl/formAttribute.h"
#include "frmserver/mcp/mcpTool.h"                    // ibMcpSetProperty — a switch's items set as MCP sets them
#include "framedSession.h"                           // KeyFrame, FramedSession — the frame a form is born against

#include <memory>

namespace {

ibValue MakeField()
{
	ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(g_controlFieldCLSID);
	return ctor != nullptr ? ctor->CreateObject() : ibValue();
}

// A field asked only through the doors every control has: the frame saves, loads, updates and answers the client, the
// factory is bound, the property object lists. The view inside is never named.
struct FieldUnderTest {
	ibValue m_value;
	ibValueFrame* m_frame = nullptr;
	ibTypeControlFactory* m_factory = nullptr;

	explicit FieldUnderTest(const ibValue& value = MakeField())
		: m_value(value), m_frame(dynamic_cast<ibValueFrame*>(m_value.GetRef())),
		m_factory(dynamic_cast<ibTypeControlFactory*>(m_value.GetRef())) {}
	explicit FieldUnderTest(ibValueFrame* frame)
		: FieldUnderTest(ibValue(static_cast<ibValue*>(frame))) {}

	bool IsOk() const { return m_frame != nullptr && m_factory != nullptr; }

	int View() const {
		const ibPropertyEnumBase* view = dynamic_cast<const ibPropertyEnumBase*>(m_frame->GetProperty(ibString(ibT("View"))));
		return view != nullptr ? (int)view->GetValueAsInteger() : -1;
	}

	// PAST THE DOOR — the property set, then the control told, no veto asked: as a form the studio's client hands back
	// is read (ReadData).
	void SetView(ibFieldView view) {
		ibProperty* property = m_frame->GetProperty(ibString(ibT("View")));
		ASSERT_NE(property, nullptr);
		const ibVariant oldValue = property->GetValue();
		dynamic_cast<ibPropertyEnumBase*>(property)->SetValue(static_cast<int>(view));
		m_frame->OnPropertyChanged(property, oldValue, property->GetValue());
	}

	ibEvent* Event(const ibChar* name) const { return m_frame->GetEvent(ibString(name)); }
	ibString Handler(const ibChar* name) const {
		const ibEvent* event = Event(name);
		return event != nullptr ? event->GetValue().GetString() : ibString();
	}
	void SetHandler(const ibChar* name, const ibChar* handler) {
		ibEvent* event = Event(name);
		ASSERT_NE(event, nullptr);
		event->SetValue(ibVariant(ibString(handler)));
	}

	ibPropertyEnumBase* EnumProperty(const ibChar* name) const {
		return dynamic_cast<ibPropertyEnumBase*>(m_frame->GetProperty(ibString(name)));
	}
};

// The View a node carries, as the field writes it.
ibDataValue ViewNode(ibFieldView view)
{
	return ibDataValue::Int(static_cast<std::int64_t>(view));
}

} // namespace

//*************************************************************************************************
//*                                   The facade and its child                                    *
//*************************************************************************************************

TEST(FormField, AFieldIsBornAnInput)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest field;
	ASSERT_TRUE(field.IsOk());
	EXPECT_EQ(field.View(), (int)ibFieldView_Input);
	EXPECT_NE(field.Event(ibT("StartChoice")), nullptr);   // the Input's own
}

TEST(FormField, SavedAndLoadedKeepsTheViewAndItsHandlers)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest written;
	ASSERT_TRUE(written.IsOk());
	written.SetView(ibFieldView_Label);
	written.SetHandler(ibT("Opening"), ibT("LabelOpening"));

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	FieldUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_EQ(read.View(), (int)ibFieldView_Label);
	EXPECT_TRUE(read.Handler(ibT("Opening")) == ibString(ibT("LabelOpening")));
	EXPECT_EQ(read.Event(ibT("StartChoice")), nullptr);   // a Label, not an Input
}

TEST(FormField, EventsAreCountedThroughTheField)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest field;
	ASSERT_TRUE(field.IsOk());

	field.m_factory->SetDefaultMetaType(g_valueBooleanCLSID);   // a Flag stands on a Boolean
	field.SetView(ibFieldView_Flag);
	ASSERT_EQ(field.View(), (int)ibFieldView_Flag);
	EXPECT_EQ(field.m_frame->GetEventCount(), 1u);   // the field has none of its own: the Flag's OnChange
	ASSERT_NE(field.m_frame->GetEvent(0u), nullptr);
	EXPECT_TRUE(field.m_frame->GetEvent(0u)->GetName() == ibString(ibT("OnChange")));

	field.SetView(ibFieldView_Label);
	EXPECT_EQ(field.Event(ibT("OnChange")), nullptr);
	EXPECT_NE(field.Event(ibT("Opening")), nullptr);
}

TEST(FormField, AnUnknownViewLoadsAsInput)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest written;
	ASSERT_TRUE(written.IsOk());
	written.SetView(ibFieldView_Label);
	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));
	node.SetField(ibString(ibT("View")), ibDataValue::Int(99));   // a later version's, or a damaged form's - in its place

	FieldUnderTest read;
	ASSERT_TRUE(read.IsOk());
	EXPECT_TRUE(read.m_frame->ReadData(node));
	EXPECT_EQ(read.View(), (int)ibFieldView_Input);
	EXPECT_NE(read.Event(ibT("StartChoice")), nullptr);
}

TEST(FormField, CopyAndPasteCarryTheView)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest copied;
	ASSERT_TRUE(copied.IsOk());
	copied.SetView(ibFieldView_Label);
	copied.SetHandler(ibT("Opening"), ibT("X"));

	ibDataNode node;
	ASSERT_TRUE(copied.m_frame->CopyNode(node));

	FieldUnderTest pasted;
	ASSERT_TRUE(pasted.IsOk());
	ASSERT_TRUE(pasted.m_frame->PasteNode(node));
	EXPECT_EQ(pasted.View(), (int)ibFieldView_Label);
	EXPECT_TRUE(pasted.Handler(ibT("Opening")) == ibString(ibT("X")));
	EXPECT_EQ(pasted.Event(ibT("StartChoice")), nullptr);
}

// …and the view's properties, in the walk over the field's own.
TEST(FormField, CopyAndPasteCarryTheViewsProperties)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest copied;
	ASSERT_TRUE(copied.IsOk());
	ibProperty* const password = copied.m_frame->GetProperty(ibString(ibT("PasswordMode")));
	ASSERT_NE(password, nullptr);
	ASSERT_TRUE(password->SetDataValue(ibValue(true)));

	ibDataNode node;
	ASSERT_TRUE(copied.m_frame->CopyNode(node));
	EXPECT_TRUE(node.FindProperty(ibT("PasswordMode")));

	FieldUnderTest pasted;
	ASSERT_TRUE(pasted.IsOk());
	ASSERT_TRUE(pasted.m_frame->PasteNode(node));
	ASSERT_NE(pasted.m_frame->GetProperty(ibString(ibT("PasswordMode"))), nullptr);
	EXPECT_TRUE(pasted.m_frame->GetProperty(ibString(ibT("PasswordMode")))->GetValue().GetBoolean());
}

//*************************************************************************************************
//*                                        Input                                                  *
//*************************************************************************************************

// Today's text field, through the field's doors: its word over its type's, Auto leaving the type's answer standing —
// and the two properties go out with the form and come back with it.
TEST(FormField, AnInputSaysItsWordInItsDoors)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ibPropertyEnumBase* quickChoice = written.EnumProperty(ibT("QuickChoice"));
	ibPropertyEnumBase* foldersAndItems = written.EnumProperty(ibT("ChoiceFoldersAndItems"));
	ASSERT_NE(quickChoice, nullptr);
	ASSERT_NE(foldersAndItems, nullptr);

	EXPECT_FALSE(written.m_frame->HasQuickChoice());
	EXPECT_EQ(written.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_Items);

	quickChoice->SetValue(static_cast<int>(ibQuickChoiceMode_Use));
	EXPECT_TRUE(written.m_frame->HasQuickChoice());
	quickChoice->SetValue(static_cast<int>(ibQuickChoiceMode_DontUse));
	EXPECT_FALSE(written.m_frame->HasQuickChoice());
	foldersAndItems->SetValue(static_cast<int>(ibChoiceFoldersAndItems_FoldersAndItems));
	EXPECT_EQ(written.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_FoldersAndItems);

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	FieldUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_FALSE(read.m_frame->HasQuickChoice());
	EXPECT_EQ(read.m_factory->GetSelectMode(), ibSelectMode::ibSelectMode_FoldersAndItems);
}

// An Input's links (#616) go out with the form and come back with it — a path through the dot whole.
TEST(FormField, AnInputKeepsItsLinks)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest written;
	ASSERT_TRUE(written.IsOk());
	ibPropertyChoiceLink* link = dynamic_cast<ibPropertyChoiceLink*>(written.m_frame->GetProperty(ibString(ibT("TypeLink"))));
	ibPropertyChoiceParameters* rows = dynamic_cast<ibPropertyChoiceParameters*>(written.m_frame->GetProperty(ibString(ibT("ChoiceParameters"))));
	ASSERT_NE(link, nullptr);
	ASSERT_NE(rows, nullptr);

	ibChoiceTypeLinkDescription linkDesc;
	linkDesc.m_source.AppendSource(3);
	linkDesc.m_source.AppendSource(17);   // Object.Kind — through the dot
	link->SetValue(linkDesc);
	ibChoiceParametersDescription paramsDesc;
	ibChoiceParameterRowDescription row;
	row.m_parameter = 41;
	row.m_source.AppendSource(3);
	paramsDesc.SetRow(row);
	rows->SetValue(paramsDesc);
	EXPECT_TRUE(written.m_frame->GetTypeLink() == linkDesc);
	EXPECT_TRUE(written.m_frame->GetChoiceParameters() == paramsDesc);

	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	FieldUnderTest read;
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_TRUE(read.m_frame->GetTypeLink() == linkDesc);
	EXPECT_TRUE(read.m_frame->GetChoiceParameters() == paramsDesc);
}

TEST(FormField, AnInputHasItsSixEvents)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest field;
	ASSERT_TRUE(field.IsOk());
	for (const ibChar* name : { ibT("OnChange"), ibT("StartChoice"), ibT("StartListChoice"), ibT("Clearing"), ibT("Opening"), ibT("ChoiceProcessing") })
		EXPECT_NE(field.Event(name), nullptr) << ibString(name).ToStdString();
	EXPECT_EQ(field.m_frame->GetEventCount(), 6u);
}

// The view's properties are the field's members: read and written by a script under their own names, and gone with the
// view that had them.
TEST(FormField, TheViewsPropertiesAreTheFieldsMembers)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest field;
	ASSERT_TRUE(field.IsOk());

	const long texteditMode = field.m_frame->FindProp(ibString(ibT("TexteditMode")));
	ASSERT_NE(texteditMode, ibNOT_FOUND);
	ibValue read;
	ASSERT_TRUE(field.m_frame->GetPropVal(texteditMode, read));
	EXPECT_TRUE(read.GetBoolean());

	const long passwordMode = field.m_frame->FindProp(ibString(ibT("PasswordMode")));
	ASSERT_NE(passwordMode, ibNOT_FOUND);
	field.m_frame->SetPropVal(passwordMode, ibValue(true));
	ASSERT_NE(field.m_frame->GetProperty(ibString(ibT("PasswordMode"))), nullptr);
	EXPECT_TRUE(field.m_frame->GetProperty(ibString(ibT("PasswordMode")))->GetValue().GetBoolean());

	field.m_factory->SetDefaultMetaType(g_valueBooleanCLSID);   // a Flag stands on a Boolean
	field.SetView(ibFieldView_Flag);
	ASSERT_EQ(field.View(), (int)ibFieldView_Flag);
	EXPECT_EQ(field.m_frame->FindProp(ibString(ibT("PasswordMode"))), ibNOT_FOUND);
	EXPECT_NE(field.m_frame->FindProp(ibString(ibT("Title"))), ibNOT_FOUND);   // the field's own stays
}

// TEXT EDIT MODE OFF IS NO READ-ONLY (Max, 2026-11-24): the value is not typed, but still picked and cleared — the field
// says it may be written, and the client keeps its Select and Clear.
TEST(FormField, TexteditModeOffLeavesTheFieldWritable)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest field;
	ASSERT_TRUE(field.IsOk());
	const long texteditMode = field.m_frame->FindProp(ibString(ibT("TexteditMode")));
	ASSERT_NE(texteditMode, ibNOT_FOUND);
	field.m_frame->SetPropVal(texteditMode, ibValue(false));
	ASSERT_FALSE(field.m_frame->GetProperty(ibString(ibT("TexteditMode")))->GetValue().GetBoolean());
	EXPECT_FALSE(dynamic_cast<ibValueField*>(field.m_frame)->IsReadOnly());
}

// THE SELECT BUTTON IS THE SOURCE'S TYPE'S, told as the source changes — the form's builder binds through the same door
// (SetSource): none on one String, nothing to pick it from; one on a date (2026-11-24: a generated field kept "..." on
// a string).
TEST(FormField, AnInputsSelectButtonIsItsSourcesType)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest field;
	ASSERT_TRUE(field.IsOk());
	ibProperty* const source = field.m_frame->GetProperty(ibString(ibT("Source")));
	ibProperty* const button = field.m_frame->GetProperty(ibString(ibT("ButtonSelect")));
	ASSERT_NE(source, nullptr);
	ASSERT_NE(button, nullptr);

	field.m_factory->SetDefaultMetaType(g_valueStringCLSID);
	field.m_frame->OnPropertyChanged(source, source->GetValue(), source->GetValue());
	EXPECT_FALSE(button->GetValue().GetBoolean());

	field.m_factory->SetDefaultMetaType(g_valueDateCLSID);
	field.m_frame->OnPropertyChanged(source, source->GetValue(), source->GetValue());
	EXPECT_TRUE(button->GetValue().GetBoolean());
}

// THE STUDIO'S ROAD — the form is read anew from its node: what the new view has by the old one's name is read off it.
TEST(FormField, ASwitchedViewKeepsASameNamedHandler_TheStudiosRoad)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest input;
	ASSERT_TRUE(input.IsOk());
	input.SetHandler(ibT("Opening"), ibT("Open1"));
	ibDataNode node;
	ASSERT_TRUE(input.m_frame->WriteData(node));

	node.SetField(ibString(ibT("View")), ViewNode(ibFieldView_Label));
	FieldUnderTest label;
	ASSERT_TRUE(label.IsOk());
	ASSERT_TRUE(label.m_frame->ReadData(node));
	EXPECT_EQ(label.View(), (int)ibFieldView_Label);
	EXPECT_TRUE(label.Handler(ibT("Opening")) == ibString(ibT("Open1")));

	ibDataNode again;
	ASSERT_TRUE(label.m_frame->WriteData(again));
	again.SetField(ibString(ibT("View")), ViewNode(ibFieldView_Input));
	FieldUnderTest back;
	ASSERT_TRUE(back.IsOk());
	ASSERT_TRUE(back.m_frame->ReadData(again));
	EXPECT_EQ(back.View(), (int)ibFieldView_Input);
	EXPECT_TRUE(back.Handler(ibT("Opening")) == ibString(ibT("Open1")));
}

// THE LIVE ROAD — the property changed: the new view's child takes the old one's handlers by name, a lambda among them.
TEST(FormField, ASwitchedViewKeepsASameNamedHandler_TheLiveRoad)
{
	wxInitializer wxInit;
	if (!wxInit.IsOk())
		GTEST_SKIP() << "wxBase init failed (no wxApp host)";

	FieldUnderTest field;
	ASSERT_TRUE(field.IsOk());
	field.SetHandler(ibT("Opening"), ibT("Open1"));

	field.SetView(ibFieldView_Label);
	EXPECT_TRUE(field.Handler(ibT("Opening")) == ibString(ibT("Open1")));
	field.SetView(ibFieldView_Input);
	EXPECT_TRUE(field.Handler(ibT("Opening")) == ibString(ibT("Open1")));

	// …and a lambda a script hung on, which no node carries.
	ibCompileCode cc(ibT("test"), ibT("memory"), false);
	ASSERT_TRUE(cc.Compile(ibT("var f public; f = Function(x) Return x EndFunction;")));
	ibProcUnit pu;
	pu.Execute(cc.m_cByteCode);
	ibValue lambda;
	ASSERT_TRUE(pu.GetPropVal(ibT("f"), lambda));
	ASSERT_NE(AsFunction(lambda), nullptr);

	ASSERT_TRUE(field.Event(ibT("Clearing"))->SetDataValue(lambda));
	field.SetView(ibFieldView_Label);   // a Label has no Clearing — it goes with the Input…
	field.SetView(ibFieldView_Input);   // …and comes back empty
	ibValue cleared;
	ASSERT_TRUE(field.Event(ibT("Clearing"))->GetDataValue(cleared));
	EXPECT_EQ(AsFunction(cleared), nullptr);

	ASSERT_TRUE(field.Event(ibT("Opening"))->SetDataValue(lambda));
	field.SetView(ibFieldView_Label);   // Opening is both views'
	ibValue held;
	ASSERT_TRUE(field.Event(ibT("Opening"))->GetDataValue(held));
	EXPECT_EQ(AsFunction(held), AsFunction(lambda));
}

//*************************************************************************************************
//*                                     On a form                                                 *
//*************************************************************************************************

namespace {

// A form made in the test answers with the test's application, as a form opened from one does.
struct FieldForm : ibValueForm {
	const ibMetaData* m_metaData = nullptr;
	const ibMetaData* GetMetaData() const override { return m_metaData; }
};

struct FieldOnAForm : ::testing::Test {
	wxInitializer m_wxInit;
	ibMetaDataApplicationFile cfg;

	// A form is born against its session's frame (framedSession.h).
	std::shared_ptr<FramedSession> session;
	std::unique_ptr<KeyFrame> frame;
	std::unique_ptr<ibSessionScope> scope;

	FieldForm* form = nullptr;
	ibValue formValue;   // holds the form
	ibFormAttributeValue* text = nullptr;
	ibFormAttributeValue* agreed = nullptr;
	ibFormAttributeValue* counterparty = nullptr;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";
		if (wxLog::GetActiveTarget() != nullptr)
			delete wxLog::SetActiveTarget(new wxLogStderr());
		if (ibApplicationInstance::Get() == nullptr && !ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE))
			GTEST_SKIP() << "appData env unavailable headless";

		session = std::make_shared<FramedSession>(ibT("form-field"), ibSessionKind::Workspace);
		frame = std::make_unique<KeyFrame>(ibApplicationInstance::GetSessionRegistry()->MintUnlisted(session));
		session->Attach(frame.get());
		scope = std::make_unique<ibSessionScope>(session.get());

		form = new FieldForm();
		formValue = ibValue(static_cast<ibValue*>(form));
		form->m_metaData = &cfg;
		text = form->AddAttribute(ibT("Text"), g_valueStringCLSID, ibValue(ibString(ibT("Acme"))));
		agreed = form->AddAttribute(ibT("Agreed"), g_valueBooleanCLSID, ibValue(false));
		counterparty = form->AddAttribute(ibT("Counterparty"), g_valueStringCLSID, ibValue(ibString(ibT("Our firm"))));
		ASSERT_TRUE(text && agreed && counterparty);
	}

	void TearDown() override {
		formValue = ibValue();
		scope.reset();
		frame.reset();
		session.reset();
		if (ibApplicationInstance::Get() != nullptr)
			ibApplicationInstance::DestroyAppDataEnv();
	}

	// A field on the form, bound to `attribute` (none: unbound, a String), then of `view` — bound first: the view is
	// the type's to allow.
	FieldUnderTest Field(ibFieldView view, const ibFormAttributeValue* attribute = nullptr) {
		FieldUnderTest field(form->NewObject(g_controlFieldCLSID, form));
		if (field.IsOk()) {
			if (attribute != nullptr)
				field.m_factory->SetDefaultSourceType(ibSourceDescription(attribute->GetId()));
			field.SetView(view);
		}
		return field;
	}

	static ibValue ValueOf(const ibFormAttributeValue* attribute) {
		ibValue value;
		attribute->GetValueByPath({}, value);
		return value;
	}

	static ibDataNode StateOf(const FieldUnderTest& field) {
		ibDataNode state;
		field.m_frame->OnUpdate(state, nullptr);
		return state;
	}

	// A switch's item: `value` as its row stores it, `presentation` the text it shows.
	static ibChoiceDescription Item(const ibValue& value, const ibChar* presentation) {
		ibChoiceDescription item;
		ibSerializeValue(value, item.m_value);
		item.m_presentation = ibString(presentation);
		return item;
	}

	// A Switch bound to Text, offering "a", "b" and "c".
	FieldUnderTest AbcSwitch() {
		FieldUnderTest field = Field(ibFieldView_Switch, text);
		if (ibPropertyChoices* choices = dynamic_cast<ibPropertyChoices*>(field.m_frame->GetProperty(ibString(ibT("Choices"))))) {
			ibChoicesDescription desc;
			desc.m_rows = { Item(ibValue(ibString(ibT("a"))), ibT("One")), Item(ibValue(ibString(ibT("b"))), ibT("Two")),
				Item(ibValue(ibString(ibT("c"))), ibT("Three")) };
			choices->SetValue(desc);
		}
		return field;
	}

	static ibDataNode Picked(std::int32_t place) {
		ibDataNode args;
		args.SetValue(ibProtocol::ibEvent::Member, place);
		return args;
	}

	// A slider let go at `position`.
	static ibDataNode MovedTo(std::int32_t position) {
		ibDataNode args;
		args.SetValue(ibProtocol::ibEvent::Position, position);
		return args;
	}

	// A Number attribute, Amount, holding 30 — added by the tests that need one: the fixture's three are counted.
	ibFormAttributeValue* AddAmount() {
		return form->AddAttribute(ibT("Amount"), g_valueNumberCLSID, ibValue(ibNumber(30)));
	}

	// Bound anew to `attribute` on the live road — the source set, then the field told (ibFormDesignView::SetProperty).
	static void Rebind(FieldUnderTest& field, const ibFormAttributeValue* attribute) {
		ibProperty* const source = field.m_frame->GetProperty(ibString(ibT("Source")));
		ASSERT_NE(source, nullptr);
		const ibVariant was = source->GetValue();
		field.m_factory->SetDefaultSourceType(ibSourceDescription(attribute->GetId()));
		field.m_frame->OnPropertyChanged(source, was, source->GetValue());
	}

	// The views the field takes now, by number, in the enumeration's order — each asked of the door its View is set
	// through (ibValueField::OnPropertyChanging).
	static std::vector<long> Allowed(const FieldUnderTest& field) {
		ibPropertyEnumBase* const view = field.EnumProperty(ibT("View"));
		const ibPropertyChoiceList list = view->GetEnumList();
		std::vector<long> views;
		for (unsigned int idx = 0; idx < list.GetCount(); idx++) {
			if (field.m_frame->OnPropertyChanging(view, list.GetValue(idx)))
				views.push_back(list.GetId(idx));
		}
		return views;
	}
};

} // namespace

TEST_F(FieldOnAForm, AnUnboundLabelIsShownAnUnboundFlagIsNot)
{
	FieldUnderTest label = Field(ibFieldView_Label);
	FieldUnderTest flag = Field(ibFieldView_Input);
	ASSERT_TRUE(label.IsOk() && flag.IsOk());
	flag.m_factory->SetDefaultMetaType(g_valueBooleanCLSID);   // a Flag stands on a Boolean
	flag.SetView(ibFieldView_Flag);
	ASSERT_EQ(flag.View(), (int)ibFieldView_Flag);

	EXPECT_TRUE(StateOf(label).GetValue<bool>(ibProtocol::ibForm::ibState::Visible));
	EXPECT_FALSE(StateOf(flag).GetValue<bool>(ibProtocol::ibForm::ibState::Visible));
}

TEST_F(FieldOnAForm, AFlagToggleWritesABoolean)
{
	FieldUnderTest flag = Field(ibFieldView_Flag, agreed);
	ASSERT_TRUE(flag.IsOk());
	EXPECT_FALSE(StateOf(flag).GetValue<bool>(ibProtocol::ibForm::ibState::Checked));

	ibDataNode args;
	args.SetValue(ibProtocol::ibEvent::Checked, true);
	EXPECT_TRUE(flag.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Toggle, args));
	EXPECT_TRUE(ValueOf(agreed).GetBoolean());
	EXPECT_TRUE(StateOf(flag).GetValue<bool>(ibProtocol::ibForm::ibState::Checked));
}

// A binding the form cannot write — here the whole form is open for viewing, the outer shell of the same door
// (ibValueForm::IsWritableBinding) a dotted path meets.
TEST_F(FieldOnAForm, AReadOnlyFlagIgnoresToggle)
{
	FieldUnderTest flag = Field(ibFieldView_Flag, agreed);
	ASSERT_TRUE(flag.IsOk());
	form->SetViewOnly(true);

	EXPECT_TRUE(StateOf(flag).GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));
	ibDataNode args;
	args.SetValue(ibProtocol::ibEvent::Checked, true);
	flag.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Toggle, args);
	EXPECT_FALSE(ValueOf(agreed).GetBoolean());
}

// …and one SET read-only, on a form that writes it: the field's own ReadOnly, in every view (Max, 2026-11-24: "a field
// must have ReadOnly").
TEST_F(FieldOnAForm, AFieldSetReadOnlyIgnoresToggle)
{
	FieldUnderTest flag = Field(ibFieldView_Flag, agreed);
	ASSERT_TRUE(flag.IsOk());
	EXPECT_FALSE(StateOf(flag).GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));
	ASSERT_TRUE(ibPropertyGate::SetValue(nullptr, flag.m_frame->GetProperty(ibString(ibT("ReadOnly"))), ibVariant(true)));

	EXPECT_TRUE(StateOf(flag).GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));
	ibDataNode args;
	args.SetValue(ibProtocol::ibEvent::Checked, true);
	flag.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Toggle, args);
	EXPECT_FALSE(ValueOf(agreed).GetBoolean());
}

// An Input's own rows are offered what the form reaches — the view's property climbs to the field it stands in.
TEST_F(FieldOnAForm, AnInputsChoiceParametersAreOfferedWhatTheFormReaches)
{
	FieldUnderTest input = Field(ibFieldView_Input, text);
	ASSERT_TRUE(input.IsOk());
	ibPropertyChoiceParameters* rows = dynamic_cast<ibPropertyChoiceParameters*>(input.m_frame->GetProperty(ibString(ibT("ChoiceParameters"))));
	ASSERT_NE(rows, nullptr);

	const std::vector<ibChoiceHolderField> sources = ibChoiceHolderFields(rows->GetPropertyObject());
	ASSERT_EQ(sources.size(), 2u);   // Agreed and Counterparty — never Text, the field's own
	EXPECT_TRUE(sources[0].m_spelled == ibString(ibT("Agreed")));
	EXPECT_TRUE(sources[1].m_spelled == ibString(ibT("Counterparty")));
}

TEST_F(FieldOnAForm, AnInputWritesItsMaskIntoTheState)
{
	FieldUnderTest input = Field(ibFieldView_Input, text);
	ASSERT_TRUE(input.IsOk());
	ibProperty* mask = input.m_frame->GetProperty(ibString(ibT("Mask")));
	ASSERT_NE(mask, nullptr);
	mask->SetValue(ibVariant(ibString(ibT("999"))));

	const ibDataNode state = StateOf(input);
	EXPECT_TRUE(state.GetValue<ibString>(ibProtocol::ibForm::ibState::Mask) == ibInputMask::Parse(ibString(ibT("999"))).Render());
	EXPECT_FALSE(state.GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));
}

//*************************************************************************************************
//*                                        Switch                                                 *
//*************************************************************************************************

TEST_F(FieldOnAForm, ASwitchShowsItsItemsAndMarksTheChosen)
{
	FieldUnderTest field = AbcSwitch();
	ASSERT_TRUE(field.IsOk());
	text->SetValueByPath({}, ibValue(ibString(ibT("b"))));

	const ibDataNode state = StateOf(field);
	const auto choices = state.FindChild(ibProtocol::ibForm::ibState::Choices);
	ASSERT_TRUE(choices);
	ASSERT_EQ(choices->Children().size(), 3u);
	EXPECT_TRUE(choices->Children()[0].GetValue<ibString>(ibProtocol::ibForm::ibState::Caption) == ibString(ibT("One")));
	EXPECT_FALSE(choices->Children()[0].GetValue<bool>(ibProtocol::ibForm::ibState::Selected));
	EXPECT_TRUE(choices->Children()[1].GetValue<bool>(ibProtocol::ibForm::ibState::Selected));
	EXPECT_FALSE(choices->Children()[2].GetValue<bool>(ibProtocol::ibForm::ibState::Selected));
}

// A SWITCH OF NO ITEMS is drawn still — one item saying it is empty, which picks nothing (Max, 2026-11-23).
TEST_F(FieldOnAForm, ASwitchOfNoItemsSaysSo)
{
	FieldUnderTest field = Field(ibFieldView_Switch);   // no source, no type: nothing to lay the items from
	ASSERT_TRUE(field.IsOk());

	const ibDataNode state = StateOf(field);
	const auto choices = state.FindChild(ibProtocol::ibForm::ibState::Choices);
	ASSERT_TRUE(choices);
	ASSERT_EQ(choices->Children().size(), 1u);
	EXPECT_FALSE(choices->Children()[0].GetValue<ibString>(ibProtocol::ibForm::ibState::Caption).IsEmpty());
	EXPECT_TRUE(state.GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));
	// The pick is the switch's, taken or not (a flag's toggle is the flag's): it says so, and nothing is written.
	EXPECT_TRUE(field.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(0)));
}

TEST_F(FieldOnAForm, ASwitchChangeWritesTheItemsValue)
{
	FieldUnderTest field = AbcSwitch();
	ASSERT_TRUE(field.IsOk());

	EXPECT_TRUE(field.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(2)));
	EXPECT_TRUE(ValueOf(text).GetString() == ibString(ibT("c")));
}

TEST_F(FieldOnAForm, ASwitchRefusesAPlaceItDoesNotHave)
{
	// The pick is the switch's even where it names nothing — what it refuses is the write.
	FieldUnderTest empty = Field(ibFieldView_Switch, text);
	ASSERT_TRUE(empty.IsOk());
	EXPECT_TRUE(empty.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(0)));
	EXPECT_TRUE(ValueOf(text).GetString() == ibString(ibT("Acme")));

	FieldUnderTest abc = AbcSwitch();
	ASSERT_TRUE(abc.IsOk());
	EXPECT_TRUE(abc.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(3)));
	EXPECT_TRUE(abc.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(-1)));
	EXPECT_TRUE(ValueOf(text).GetString() == ibString(ibT("Acme")));
}

TEST_F(FieldOnAForm, AReadOnlySwitchIgnoresChange)
{
	FieldUnderTest field = AbcSwitch();
	ASSERT_TRUE(field.IsOk());
	form->SetViewOnly(true);

	EXPECT_TRUE(StateOf(field).GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));
	EXPECT_TRUE(field.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(0)));   // taken, not written
	EXPECT_TRUE(ValueOf(text).GetString() == ibString(ibT("Acme")));
}

TEST_F(FieldOnAForm, ASwitchKeepsItsItemsThroughSaveAndLoad)
{
	FieldUnderTest written = AbcSwitch();
	ASSERT_TRUE(written.IsOk());
	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));

	FieldUnderTest read = Field(ibFieldView_Input, text);
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	EXPECT_EQ(read.View(), (int)ibFieldView_Switch);
	const ibPropertyChoices* choices = dynamic_cast<const ibPropertyChoices*>(read.m_frame->GetProperty(ibString(ibT("Choices"))));
	ASSERT_NE(choices, nullptr);
	ASSERT_EQ(choices->GetValueAsChoicesDesc().m_rows.size(), 3u);
	EXPECT_TRUE(choices->GetValueAsChoicesDesc().m_rows[2].m_presentation == ibString(ibT("Three")));

	EXPECT_TRUE(read.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(0)));
	EXPECT_TRUE(ValueOf(text).GetString() == ibString(ibT("a")));
}

// A SCRIPT READS THE ITEMS AS A VALUE LIST, AND WRITES ONE (Max, 2026-11-24: "it returns the list of values"): each
// item its value and its text; a list written is the items in its order, a text that is the value's own kept as none.
TEST_F(FieldOnAForm, AScriptReadsAndWritesTheItemsAsAValueList)
{
	FieldUnderTest field = AbcSwitch();
	ASSERT_TRUE(field.IsOk());
	const long member = field.m_frame->FindProp(ibString(ibT("Choices")));
	ASSERT_NE(member, ibNOT_FOUND);

	ibValue read;
	ASSERT_TRUE(field.m_frame->GetPropVal(member, read));
	ibValueValueList* list = nullptr;
	ASSERT_TRUE(read.ConvertToValue(list));
	ASSERT_EQ(list->GetRowCount(), 3);
	EXPECT_TRUE(list->Storage().GetCell(1, ibValueValueList::eValue).GetString() == ibString(ibT("b")));
	EXPECT_TRUE(list->Storage().GetCell(1, ibValueValueList::ePresentation).GetString() == ibString(ibT("Two")));

	// The last one taken out, one added with no text of its own — written back, the switch offers what the list holds.
	list->Delete(list->Get(2));
	list->Add(ibValue(ibString(ibT("d"))), ibString(), false, ibValue());
	field.m_frame->SetPropVal(member, read);

	const ibPropertyChoices* choices = dynamic_cast<const ibPropertyChoices*>(field.m_frame->GetProperty(ibString(ibT("Choices"))));
	ASSERT_NE(choices, nullptr);
	const std::vector<ibChoiceDescription>& rows = choices->GetValueAsChoicesDesc().m_rows;
	ASSERT_EQ(rows.size(), 3u);
	EXPECT_TRUE(rows[0].m_presentation == ibString(ibT("One")));
	EXPECT_TRUE(rows[2].m_value == Item(ibValue(ibString(ibT("d"))), ibT("")).m_value);
	EXPECT_TRUE(rows[2].m_presentation.IsEmpty());

	EXPECT_TRUE(field.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(2)));
	EXPECT_TRUE(ValueOf(text).GetString() == ibString(ibT("d")));
}

// AN ITEM IS HELD TO THE FIELD'S QUALIFIERS, as the value it writes is — so the item picked is the item marked
// (2026-11-24: 3.257 from a script's value list, on a field of two digits, wrote 3.26 and the switch marked none). Every
// road ends in one SetValue; here a script's.
TEST_F(FieldOnAForm, AnItemIsHeldToTheFieldsQualifiers)
{
	const ibFormAttributeValue* const amount = AddAmount();
	FieldUnderTest field = Field(ibFieldView_Switch, amount);
	ASSERT_TRUE(field.IsOk());
	const ibValue third(ibNumber(1) / ibNumber(3));   // more digits than a field holds
	const ibValue held = field.m_factory->AdjustValue(third);
	ASSERT_FALSE(held.GetNumber() == third.GetNumber());

	const long member = field.m_frame->FindProp(ibString(ibT("Choices")));
	ASSERT_NE(member, ibNOT_FOUND);
	ibValue list;
	ASSERT_TRUE(field.m_frame->GetPropVal(member, list));
	ibValueValueList* items = nullptr;
	ASSERT_TRUE(list.ConvertToValue(items));
	items->Add(third, ibString(ibT("A third")), false, ibValue());
	field.m_frame->SetPropVal(member, list);

	const ibPropertyChoices* choices = dynamic_cast<const ibPropertyChoices*>(field.m_frame->GetProperty(ibString(ibT("Choices"))));
	ASSERT_NE(choices, nullptr);
	ASSERT_EQ(choices->GetValueAsChoicesDesc().m_rows.size(), 1u);
	EXPECT_TRUE(ibDeserializeValue(choices->GetValueAsChoicesDesc().m_rows[0].m_value).GetNumber() == held.GetNumber());

	EXPECT_TRUE(field.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, Picked(0)));
	EXPECT_TRUE(ValueOf(amount).GetNumber() == held.GetNumber());
	const auto marked = StateOf(field).FindChild(ibProtocol::ibForm::ibState::Choices);
	ASSERT_TRUE(marked);
	ASSERT_EQ(marked->Children().size(), 1u);
	EXPECT_TRUE(marked->Children()[0].GetValue<bool>(ibProtocol::ibForm::ibState::Selected));
}

// MCP SETS THE ITEMS IN THE SHAPE IT READS THEM — [{value, presentation}], a number as JSON writes one — and refuses a
// value of another type, the items left as they were (2026-11-24, a form driven over MCP: numbers sent, every value lost
// as an empty node while the call answered ok).
TEST_F(FieldOnAForm, MCPSetsTheItemsInTheShapeItReadsThem)
{
	FieldUnderTest field = Field(ibFieldView_Switch, AddAmount());
	ASSERT_TRUE(field.IsOk());
	ASSERT_EQ(field.View(), (int)ibFieldView_Switch);
	ibProperty* const property = field.m_frame->GetProperty(ibString(ibT("Choices")));
	const ibPropertyChoices* const choices = dynamic_cast<const ibPropertyChoices*>(property);
	ASSERT_NE(choices, nullptr);

	const auto item = [](const ibDataValue& value, const ibChar* presentation) {
		auto node = std::make_shared<ibDataNode>();
		node->AddField(ibT("value"), value);
		node->AddField(ibT("presentation"), ibDataValue::String(ibString(presentation)));
		return ibDataValue::Child(node);
	};

	ibDataNode params;
	params.AddField(ibMcpValueArgument().Name(),
		ibDataValue::Array({ item(ibDataValue::Number(ibNumber(7)), ibT("Seven")), item(ibDataValue::Number(ibNumber(12)), ibT("")) }));
	ibDataNode result;
	ibString refusal;
	ASSERT_TRUE(ibMcpSetProperty(property, params, result, refusal));
	const std::vector<ibChoiceDescription>& rows = choices->GetValueAsChoicesDesc().m_rows;
	ASSERT_EQ(rows.size(), 2u);
	EXPECT_TRUE(ibDeserializeValue(rows[0].m_value).GetNumber() == ibNumber(7));
	EXPECT_TRUE(rows[0].m_presentation == ibString(ibT("Seven")));
	EXPECT_TRUE(ibDeserializeValue(rows[1].m_value).GetNumber() == ibNumber(12));

	ibDataNode wrong;
	wrong.AddField(ibMcpValueArgument().Name(), ibDataValue::Array({ item(ibDataValue::String(ibT("abc")), ibT("Text")) }));
	EXPECT_FALSE(ibMcpSetProperty(property, wrong, result, refusal));
	EXPECT_FALSE(refusal.IsEmpty());
	EXPECT_EQ(choices->GetValueAsChoicesDesc().m_rows.size(), 2u);
}

// THE ITEMS FOLLOW THE TYPE (Max, 2026-11-23): bound anew to a Boolean, a switch of strings is cleared — "it was a
// string, it became a Boolean: they are simply cleared". The field's change reaches its view (ibValueField::
// OnPropertyChanged).
TEST_F(FieldOnAForm, ASwitchIsClearedWhenTheTypeChanges)
{
	FieldUnderTest field = AbcSwitch();
	ASSERT_TRUE(field.IsOk());
	Rebind(field, agreed);

	const ibPropertyChoices* choices = dynamic_cast<const ibPropertyChoices*>(field.m_frame->GetProperty(ibString(ibT("Choices"))));
	ASSERT_NE(choices, nullptr);
	EXPECT_TRUE(choices->GetValueAsChoicesDesc().m_rows.empty());
}

// …while items still of the field's type stay as they are — the change was not of the type.
TEST_F(FieldOnAForm, ASwitchKeepsItemsStillOfItsType)
{
	FieldUnderTest field = AbcSwitch();
	ASSERT_TRUE(field.IsOk());
	Rebind(field, text);

	const ibPropertyChoices* choices = dynamic_cast<const ibPropertyChoices*>(field.m_frame->GetProperty(ibString(ibT("Choices"))));
	ASSERT_NE(choices, nullptr);
	EXPECT_EQ(choices->GetValueAsChoicesDesc().m_rows.size(), 3u);
}

//*************************************************************************************************
//*                                  The view by the type                                         *
//*************************************************************************************************

// THE VIEW IS ALLOWED BY THE SOURCE'S TYPE (Max, 2026-11-23): Input and Label for any, a Flag for a Boolean, a Gauge and
// a Slider for a Number, a Switch for one type its items can be of — a composite type is typed into or shown.
TEST_F(FieldOnAForm, TheViewsAllowedAreTheSourcesTypes)
{
	using Views = std::vector<long>;
	EXPECT_EQ(Allowed(Field(ibFieldView_Input, text)), (Views{ ibFieldView_Input, ibFieldView_Label, ibFieldView_Switch }));
	EXPECT_EQ(Allowed(Field(ibFieldView_Input, agreed)),
		(Views{ ibFieldView_Input, ibFieldView_Flag, ibFieldView_Label, ibFieldView_Switch }));
	EXPECT_EQ(Allowed(Field(ibFieldView_Input, AddAmount())),
		(Views{ ibFieldView_Input, ibFieldView_Label, ibFieldView_Switch, ibFieldView_Gauge, ibFieldView_Slider }));

	FieldUnderTest composite = Field(ibFieldView_Input);
	ASSERT_TRUE(composite.IsOk());
	composite.m_factory->SetDefaultMetaType(std::vector<ibClassID>{ g_valueStringCLSID, g_valueNumberCLSID });
	EXPECT_EQ(Allowed(composite), (Views{ ibFieldView_Input, ibFieldView_Label }));
}

// …AND THE INSPECTOR OFFERS THOSE ALONE (Max, 2026-11-24: "a number may be a switch, a Boolean a flag") — the View's
// filter is the field's own question (ibPropertyEnum<…, &ibValueField::IsViewAllowed>), asked as the row is drawn: bound
// anew, the list is the new type's.
TEST_F(FieldOnAForm, TheInspectorOffersTheViewsTheSourcesTypeAllows)
{
	const auto offered = [](const FieldUnderTest& field) {
		ibPropertyChoiceList list;
		field.EnumProperty(ibT("View"))->GetValueList(list);
		std::vector<long> views;
		for (unsigned int idx = 0; idx < list.GetCount(); idx++)
			views.push_back(list.GetId(idx));
		return views;
	};
	using Views = std::vector<long>;
	EXPECT_EQ(offered(Field(ibFieldView_Input, AddAmount())),
		(Views{ ibFieldView_Input, ibFieldView_Label, ibFieldView_Switch, ibFieldView_Gauge, ibFieldView_Slider }));

	FieldUnderTest field = Field(ibFieldView_Input, text);
	ASSERT_TRUE(field.IsOk());
	EXPECT_EQ(offered(field), (Views{ ibFieldView_Input, ibFieldView_Label, ibFieldView_Switch }));
	Rebind(field, agreed);
	EXPECT_EQ(offered(field), (Views{ ibFieldView_Input, ibFieldView_Flag, ibFieldView_Label, ibFieldView_Switch }));
}

// …and one it does not allow is refused at the door the inspector and MCP write through.
TEST_F(FieldOnAForm, AViewTheSourcesTypeDoesNotAllowIsRefused)
{
	FieldUnderTest field = Field(ibFieldView_Input, text);
	ASSERT_TRUE(field.IsOk());

	ibString refusal;
	EXPECT_FALSE(ibPropertyGate::SetValue(nullptr, field.EnumProperty(ibT("View")), ibVariant(static_cast<int>(ibFieldView_Flag)), &refusal));
	EXPECT_FALSE(refusal.IsEmpty());
	EXPECT_EQ(field.View(), (int)ibFieldView_Input);
}

// …A SCRIPT'S WRITE THROUGH THE SAME DOOR (ibValueFrame::SetPropVal): refused to it, said — the field stays as it was.
TEST_F(FieldOnAForm, AScriptsViewTheSourcesTypeDoesNotAllowIsRefused)
{
	FieldUnderTest flag = Field(ibFieldView_Flag, agreed);
	FieldUnderTest field = Field(ibFieldView_Input, text);
	ASSERT_TRUE(flag.IsOk() && field.IsOk());
	ibValue flagView;   // FieldView.Flag, as a script holds it
	ASSERT_TRUE(flag.m_frame->GetPropVal(flag.m_frame->FindProp(ibString(ibT("View"))), flagView));

	const long view = field.m_frame->FindProp(ibString(ibT("View")));
	ASSERT_NE(view, ibNOT_FOUND);
	EXPECT_THROW(field.m_frame->SetPropVal(view, flagView), ibCoreException);
	EXPECT_EQ(field.View(), (int)ibFieldView_Input);
}

// AN ATTRIBUTE DROPPED ON THE FORM — an Input bound to it, made by the studio — takes the default view (Max,
// 2026-11-23): a Boolean a Flag. A view a paste brought is kept.
TEST_F(FieldOnAForm, ADroppedBooleanIsAFlagAPastedViewStays)
{
	FieldUnderTest dropped = Field(ibFieldView_Input, agreed);
	FieldUnderTest pasted = Field(ibFieldView_Label, agreed);
	ASSERT_TRUE(dropped.IsOk() && pasted.IsOk());
	dropped.m_frame->OnCreated(nullptr, true);
	pasted.m_frame->OnCreated(nullptr, true);
	EXPECT_EQ(dropped.View(), (int)ibFieldView_Flag);
	EXPECT_EQ(pasted.View(), (int)ibFieldView_Label);
}

// A TITLE SET TO NONE — no caption drawn (Max, 2026-11-23).
TEST_F(FieldOnAForm, ATitleSetToNoneHasNoCaption)
{
	FieldUnderTest field = Field(ibFieldView_Input, text);
	ASSERT_TRUE(field.IsOk());
	EXPECT_FALSE(StateOf(field).GetValue<ibString>(ibProtocol::ibForm::ibState::Caption).IsEmpty());
	ASSERT_TRUE(ibPropertyGate::SetValue(nullptr, field.EnumProperty(ibT("TitleLocation")),
		ibVariant(static_cast<int>(ibTitleLocation::eNoTitle))));
	EXPECT_TRUE(StateOf(field).GetValue<ibString>(ibProtocol::ibForm::ibState::Caption).IsEmpty());
}

// A SOURCE OF ANOTHER TYPE makes a field whose view it does not allow an Input — its child the Input's; a view it allows
// stays. At once, never drawn: MCP edits a form nobody draws (2026-11-24, a field bound back to a String read View '').
TEST_F(FieldOnAForm, ASourceOfAnotherTypeMakesTheFieldAnInput)
{
	FieldUnderTest flag = Field(ibFieldView_Flag, agreed);
	ASSERT_TRUE(flag.IsOk());
	Rebind(flag, text);
	EXPECT_EQ(flag.View(), (int)ibFieldView_Input);
	EXPECT_NE(flag.Event(ibT("StartChoice")), nullptr);

	FieldUnderTest label = Field(ibFieldView_Label, agreed);
	ASSERT_TRUE(label.IsOk());
	Rebind(label, text);
	EXPECT_EQ(label.View(), (int)ibFieldView_Label);
}

// …as one set past the door does: a form the studio's client hands back is read with no veto (ReadData), nor does
// this helper ask one.
TEST_F(FieldOnAForm, AViewSetPastTheDoorGivesWayToAnInput)
{
	FieldUnderTest field = Field(ibFieldView_Input, text);
	ASSERT_TRUE(field.IsOk());

	field.SetView(ibFieldView_Flag);
	StateOf(field);
	EXPECT_EQ(field.View(), (int)ibFieldView_Input);
	EXPECT_NE(field.Event(ibT("StartChoice")), nullptr);   // the Input's child, not a Flag's
}

// …and as one saved before its source's type changed: read as it was saved, drawn as an Input.
TEST_F(FieldOnAForm, ASavedViewTheTypeNoLongerAllowsLoadsAsAnInput)
{
	FieldUnderTest written = Field(ibFieldView_Input, text);
	ASSERT_TRUE(written.IsOk());
	ibDataNode node;
	ASSERT_TRUE(written.m_frame->WriteData(node));
	node.SetField(ibString(ibT("View")), ViewNode(ibFieldView_Gauge));   // a Gauge over a String

	FieldUnderTest read = Field(ibFieldView_Input);
	ASSERT_TRUE(read.IsOk());
	ASSERT_TRUE(read.m_frame->ReadData(node));
	StateOf(read);
	EXPECT_EQ(read.View(), (int)ibFieldView_Input);
	EXPECT_NE(read.Event(ibT("StartChoice")), nullptr);
}

//*************************************************************************************************
//*                                     Gauge and Slider                                          *
//*************************************************************************************************

TEST_F(FieldOnAForm, AGaugeShowsItsValueWithinItsRange)
{
	ibFormAttributeValue* const amount = AddAmount();
	FieldUnderTest gauge = Field(ibFieldView_Gauge, amount);
	ASSERT_TRUE(gauge.IsOk());
	EXPECT_EQ(StateOf(gauge).GetValue<std::int32_t>(ibProtocol::ibForm::ibState::Position), 30);
	EXPECT_TRUE(StateOf(gauge).GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));   // shown, not edited

	amount->SetValueByPath({}, ibValue(ibNumber(250)));
	EXPECT_EQ(StateOf(gauge).GetValue<std::int32_t>(ibProtocol::ibForm::ibState::Position), 100);   // full at its range
}

TEST_F(FieldOnAForm, ASliderWritesANumberWithinItsEnds)
{
	ibFormAttributeValue* const amount = AddAmount();
	FieldUnderTest slider = Field(ibFieldView_Slider, amount);
	ASSERT_TRUE(slider.IsOk());
	EXPECT_EQ(StateOf(slider).GetValue<std::int32_t>(ibProtocol::ibForm::ibState::Position), 30);

	EXPECT_TRUE(slider.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, MovedTo(42)));
	EXPECT_EQ(ValueOf(amount).GetNumber().ToInt(), 42);
	EXPECT_TRUE(slider.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, MovedTo(500)));
	EXPECT_EQ(ValueOf(amount).GetNumber().ToInt(), 100);   // held at its end
}

TEST_F(FieldOnAForm, AReadOnlySliderIgnoresChange)
{
	ibFormAttributeValue* const amount = AddAmount();
	FieldUnderTest slider = Field(ibFieldView_Slider, amount);
	ASSERT_TRUE(slider.IsOk());
	form->SetViewOnly(true);

	EXPECT_TRUE(StateOf(slider).GetValue<bool>(ibProtocol::ibForm::ibState::ReadOnly));
	slider.m_frame->OnClientEvent(ibProtocol::ibEvent::ibKind::Change, MovedTo(42));
	EXPECT_EQ(ValueOf(amount).GetNumber().ToInt(), 30);
}
