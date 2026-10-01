// =============================================================================
// Frontend GUI harness — the FORM + FRAME path.
//
// Stands a live main frame up (via FrontendFormFix) so ibSession::CurrentFrame()
// resolves, then drives the supported form-creation path
// (ibBackendValueForm::CreateNewForm). The full ShowForm -> visual-host build
// needs a document manager (which the concrete enterprise/designer frames build
// in CreateGUI); the trivial test frame has none, so that path is exercised
// directly in test_frontendVisualHost instead, not through ShowForm here.
// =============================================================================

#include "frontendFormFix.h"

// The frame is bound: backend code reaching for the process UI window resolves
// it through the session, not a process-global singleton.
TEST_F(FrontendFormFix, CurrentFrameResolvesToBoundFrame)
{
	if (!frameReady) GTEST_SKIP();
	EXPECT_NE(ibSession::CurrentFrame(), nullptr)
		<< "a bound GUI session must expose its frame through CurrentFrame()";
}

// CreateNewForm routes through the bound frame and hands back a form value.
TEST_F(FrontendFormFix, CreateNewFormThroughFrame)
{
	if (!frameReady) GTEST_SKIP();

	ibValueForm* form = NewForm();
	ASSERT_NE(form, nullptr)
		<< "CreateNewForm must route through the bound frame and hand back a form";
	EXPECT_EQ(form->GetClassType(), g_controlFormCLSID);
	EXPECT_FALSE(form->IsShown()) << "a freshly created form is not shown yet";
}
