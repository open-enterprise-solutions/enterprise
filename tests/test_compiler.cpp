// =============================================================================
// OES Enterprise — compiler pipeline tests
//
// Covers the lex → compile → bytecode emission stages without running the
// interpreter. Tests do NOT require appData / activeMetaData / database —
// they construct ibCompileCode directly and inspect the produced bytecode
// (m_cByteCode field).
//
// Runtime execution is exercised in test_runtime.cpp.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/debug.h>   // wxSetAssertHandler — no modal dialogs in a batch run
#include <wx/log.h>     // wxLogStderr — the default target FLUSHES MODALLY
#include <wx/init.h>    // wxInitializer — held for the whole RUN, see below
#include <memory>       // unique_ptr — the initializer must NOT be a static-init member
#ifdef _WIN32
#include <crtdbg.h>
#endif

#include "backend/compiler/compileCode.h"
#include "backend/compiler/compileContext.h"   // CODE_VES / CODE_CES
#include "backend/compiler/codeDef.h"
#include "backend/compiler/byteCode.h"
#include "backend/compiler/value.h"

// Every OES script source in this binary's compiling suites (CompilerTest /
// LexerTest / ClosureCapture / CompilerAOT / RuntimeTest) is written in VES
// (Function...EndFunction, If...Then...EndIf). The compiler's code style is a
// PROCESS-GLOBAL (compileCode.cpp gs_codeStyle) that defaults to CODE_CES;
// under CES the VES block-fence keywords (Then/Do/EndIf/EndFunction/...) are
// filtered out (ibTranslateCode::IsAllowedKey) and these sources fail to parse.
// Force VES once for the whole test binary so the suites are deterministic
// regardless of test order or which tests --gtest_filter selects. (The global
// env's SetUp runs even under a filter, so single-test runs get it too.)
namespace {
class VesCodeStyleEnvironment : public ::testing::Environment {
public:
	void SetUp() override { ibCompileCode::SetCodeStyle(CODE_VES); }
};
const ::testing::Environment* const g_vesCodeStyleEnv =
	::testing::AddGlobalTestEnvironment(new VesCodeStyleEnvironment);

// NO MODAL DIALOGS IN A TEST BINARY.
//
// A Debug build turns a wxASSERT into a message box, and a message box in a
// test run is not a failure — it is a HANG: the suite stops, the runner waits
// out its timeout, and the report says "timed out" rather than "assertion in
// this function". One such assert cost a two-minute wait and told us nothing.
//
// The GUI suite has had this since it was written (tests/frontendFix.h); the
// backend suite never did, because until something asserted nobody noticed.
// Same treatment, one place, applied whatever --gtest_filter selects.
class HeadlessAssertEnvironment : public ::testing::Environment {
public:
	// wx STAYS UP FOR THE WHOLE TEST RUN, and this is the reason the settings
	// below hold.
	//
	// ⚠ IT IS CREATED IN SetUp, NOT AS A PLAIN MEMBER. This object is built by a
	// static initialiser (AddGlobalTestEnvironment(new …) below), so a
	// `wxInitializer` member would run wxEntryStart BEFORE main and wxEntryCleanup
	// during static DESTRUCTION, with part of the environment already gone. Under
	// GTK that is fatal: `--gtest_list_tests` — which runs no test and no
	// environment — died with "pure virtual method called", and CMake's
	// gtest_discover_tests failed the whole build on Linux while Windows and macOS
	// stayed green. SetUp/TearDown bracket the RUN, which is what was wanted.
	//
	// Fixtures bring wx up per test (a `wxInitializer` member — eight files do
	// it). wxInitialize/wxUninitialize are REFERENCE COUNTED (gs_nInitCount,
	// wx/src/common/init.cpp), so the LAST one out runs wxEntryCleanup — in the
	// middle of the suite. That teardown wipes wx's image handlers
	// (wxImageModule::OnExit -> wxImage::CleanUpHandlers) AND destroys the active
	// log target, after which wx falls back to wxLogOutputBest, which prefers a
	// MessageBox. So a later test decoding an icon raised a warning that became a
	// MODAL DIALOG — a hang with a green suite behind it.
	//
	// Holding one initializer here keeps the count off zero for the run, so no
	// mid-suite cleanup happens at all. Registering harder, or re-arming per
	// fixture, treats the symptom; nothing may be re-established on a hot path
	// in the ENGINE to satisfy a test binary.
	std::unique_ptr<wxInitializer> m_wxInit;

	void TearDown() override { m_wxInit.reset(); }

	void SetUp() override {
		m_wxInit = std::make_unique<wxInitializer>();

#ifdef _WIN32
		_CrtSetReportMode(_CRT_ERROR,  _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ERROR,  _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_WARN,   _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_WARN,   _CRTDBG_FILE_STDERR);
#endif
		// wxASSERT -> nothing. The condition still tells us something in a
		// debugger; in a batch run it must not block.
		wxSetAssertHandler(nullptr);

		// THE THIRD DOOR, and the one that actually hung the runner. A wx
		// WARNING is not an assert: with no log target set, wx installs one
		// that BUFFERS and flushes into a modal box when the target goes away.
		// The GUI fixture handles its own case (tests/frontendFix.h, after
		// wxEntryStart, because a wxApp installs wxLogGui of its own), but a
		// backend test that makes wx warn had nothing at all. stderr has no
		// OK button.
		delete wxLog::SetActiveTarget(new wxLogStderr());

		// Image handlers are NOT registered here. backend.dll already does it from
		// a static ctor at DLL load (picturePredefined.cpp), and the initializer
		// above is what makes that hold — with wx up for the whole run, nothing
		// calls wxEntryCleanup mid-suite to wipe them. Registering again only
		// prints a screenful of "Adding duplicate image handler".
	}
};
const ::testing::Environment* const g_headlessAssertEnv =
	::testing::AddGlobalTestEnvironment(new HeadlessAssertEnvironment);
} // namespace

namespace {

bool TryCompile(ibCompileCode& cc, const wxString& src) {
	try {
		return cc.Compile(src);
	} catch (...) {
		return false;
	}
}

// Find an opcode in the bytecode body. Returns the count of matches.
// Kept for the bytecode-shape assertions even while no live test calls it.
[[maybe_unused]] size_t CountOpcode(const ibByteCode& bc, short oper) {
	size_t n = 0;
	for (const auto& u : bc.m_listCode)
		if (u.m_numOper == oper) ++n;
	return n;
}

// Same, but type-tier agnostic. Opcodes are typed-specialized: a numeric /
// string / date / boolean operand shifts the base opcode by k*TYPE_DELTA1
// (codeDef.h, AddTypeSet). E.g. an If on a boolean condition emits
// OPER_IF + TYPE_DELTA4, not bare OPER_IF. Strip the delta via % TYPE_DELTA1
// (base opcodes are all < TYPE_DELTA1) so the base is matched in any tier.
size_t CountOpcodeAnyType(const ibByteCode& bc, short baseOper) {
	// NB: TYPE_DELTA1 expands to `1 * (OPER_END + 1)` WITHOUT outer parens, so
	// `x % TYPE_DELTA1` would parse as `(x % 1) * (OPER_END+1)` == 0. Bind the
	// value first so the modulo divides by the real tier stride (OPER_END+1).
	const short delta1 = TYPE_DELTA1;
	size_t n = 0;
	for (const auto& u : bc.m_listCode)
		if ((u.m_numOper % delta1) == baseOper) ++n;
	return n;
}

} // namespace

// ===========================================================================
// Lexer (ibTranslateCode::PrepareLexem)
// ===========================================================================

TEST(LexerTest, EmptySourceProducesEndProgramOnly) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT(""));
	ASSERT_TRUE(cc.PrepareLexem());
	// At least the ENDPROGRAM sentinel — exact number depends on lexer
	// implementation, but it must be non-empty.
	EXPECT_GE(cc.GetLexemCount(), 1u);
}

TEST(LexerTest, SimpleAssignmentTokenizes) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT("a = 1;"));
	ASSERT_TRUE(cc.PrepareLexem());
	// Identifier 'a', '=', number '1', ';', ENDPROGRAM — at least 5.
	EXPECT_GE(cc.GetLexemCount(), 4u);
}

TEST(LexerTest, StringLiteralLexes) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	cc.Load(wxT("s = \"hello\";"));
	ASSERT_TRUE(cc.PrepareLexem());
	EXPECT_GE(cc.GetLexemCount(), 4u);
}

// ===========================================================================
// Compile — bytecode emission shape (does not execute)
// ===========================================================================

TEST(CompilerTest, EmptyModuleCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc, wxT("")));
	// Emitter typically appends an OPER_RET at module exit.
	// Empty module => zero instructions OR a single trailing return.
	EXPECT_LE(cc.m_cByteCode.m_listCode.size(), 2u);
}

TEST(CompilerTest, SimpleAssignmentEmitsBytecode) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("a = 1 + 2;")));
	EXPECT_GT(cc.m_cByteCode.m_listCode.size(), 0u);
	// Constant 1 and 2 should land in the const pool. Either both as
	// distinct const entries, or shared via dedup — either way at
	// least one numeric constant is emitted.
	EXPECT_GE(cc.m_cByteCode.m_listConst.size(), 1u);
}

TEST(CompilerTest, AssignmentRegistersVariable) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("a = 42;")));
	// Variable 'a' must appear in m_listVar (either as Local or Export).
	bool foundA = false;
	for (const auto& v : cc.m_cByteCode.m_listVar) {
		if (v.m_strRealName.IsSameAs(wxT("a"), false)) {
			foundA = true;
			EXPECT_TRUE(v.IsLocal() || v.IsExport());
			break;
		}
	}
	EXPECT_TRUE(foundA) << "variable 'a' not found in m_listVar";
}

TEST(CompilerTest, ExportVariableMarkedAsExport) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc, wxT("var publicVar public;")));
	bool found = false;
	for (const auto& v : cc.m_cByteCode.m_listVar) {
		if (v.m_strRealName.IsSameAs(wxT("publicVar"), false)) {
			found = true;
			EXPECT_TRUE(v.IsExport()) << "expected ibVarKind::Export";
			break;
		}
	}
	EXPECT_TRUE(found) << "publicVar not found";
}

TEST(CompilerTest, FunctionDeclarationRegistersInListFunc) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Add(x, y)\n")
		wxT("  Return x + y;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	bool foundAdd = false;
	for (const auto& fn : cc.m_cByteCode.m_listFunc) {
		if (fn.m_strRealName.IsSameAs(wxT("Add"), false)) {
			foundAdd = true;
			EXPECT_TRUE(fn.m_bCodeRet) << "Function (not Procedure) expected";
			EXPECT_EQ(fn.m_listParam.size(), 2u);
			EXPECT_EQ(fn.m_listParam[0].m_strName, wxT("x"));
			EXPECT_TRUE(fn.IsLocal() || fn.IsExport());
			break;
		}
	}
	EXPECT_TRUE(foundAdd) << "function 'Add' missing from m_listFunc";
}

TEST(CompilerTest, ExportFunctionFlagged) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function PublicFn() Public\n")
		wxT("  Return 1;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	bool found = false;
	for (const auto& fn : cc.m_cByteCode.m_listFunc) {
		if (fn.m_strRealName.IsSameAs(wxT("PublicFn"), false)) {
			found = true;
			EXPECT_TRUE(fn.IsExport()) << "expected ibFnKind::Export";
			break;
		}
	}
	EXPECT_TRUE(found) << "PublicFn missing";
}

TEST(CompilerTest, ProcedureRecognized) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Procedure DoIt()\n")
		wxT("  a = 1;\n")
		wxT("EndProcedure\n");
	ASSERT_TRUE(TryCompile(cc, src));

	bool found = false;
	for (const auto& fn : cc.m_cByteCode.m_listFunc) {
		if (fn.m_strRealName.IsSameAs(wxT("DoIt"), false)) {
			found = true;
			EXPECT_FALSE(fn.m_bCodeRet) << "Procedure (no return)";
			break;
		}
	}
	EXPECT_TRUE(found) << "procedure 'DoIt' missing";
}

TEST(CompilerTest, IfStatementEmitsBranch) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("a = 0;\n")
		wxT("If a > 0 Then\n")
		wxT("  a = 1;\n")
		wxT("EndIf;\n");
	ASSERT_TRUE(TryCompile(cc, src));
	// Conditional bytecode → at least one OPER_IF (typed-specialized for the
	// boolean condition, so match any type tier).
	EXPECT_GE(CountOpcodeAnyType(cc.m_cByteCode, OPER_IF), 1u);
}

TEST(CompilerTest, WhileLoopEmitsGoto) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("a = 0;\n")
		wxT("While a < 10 Do\n")
		wxT("  a = a + 1;\n")
		wxT("EndDo;\n");
	ASSERT_TRUE(TryCompile(cc, src));
	EXPECT_GE(CountOpcodeAnyType(cc.m_cByteCode, OPER_IF),   1u);
	EXPECT_GE(CountOpcodeAnyType(cc.m_cByteCode, OPER_GOTO), 1u);
}

TEST(CompilerTest, SyntaxErrorReturnsFalse) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// Unterminated assignment — expect compile to fail (return false
	// or throw). TryCompile collapses both into false.
	EXPECT_FALSE(TryCompile(cc, wxT("a = ")));
}

// ===========================================================================
// Compile + AOT round-trip — checks the AOT layer with real compiler output
// ===========================================================================

#include "backend/fileSystem/fs.h"

TEST(CompilerAOT, RealCompileOutputRoundTrips) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// Var declaration + function declaration. NB: the parameter is named
	// `delta`, not `by` — `By` is a reserved LINQ keyword (group/order ... by),
	// so using it as an identifier fails to lex ("Identifier expected").
	const wxString src =
		wxT("var counter public;\n")
		wxT("Function Increment(delta)\n")
		wxT("  counter = counter + delta;\n")
		wxT("  Return counter;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const ibByteCode& src_bc = cc.m_cByteCode;

	ibWriterMemory w;
	ASSERT_TRUE(src_bc.SerializeAOT(w));
	wxMemoryBuffer blob = w.buffer();
	EXPECT_GT(blob.GetDataLen(), 0u);

	ibReaderMemory r(blob);
	ibByteCode dst;
	ASSERT_TRUE(dst.DeserializeAOT(r));

	EXPECT_EQ(dst.m_listCode.size(), src_bc.m_listCode.size());
	EXPECT_EQ(dst.m_listConst.size(), src_bc.m_listConst.size());
	EXPECT_EQ(dst.m_listVar.size(),  src_bc.m_listVar.size());
	EXPECT_EQ(dst.m_listFunc.size(), src_bc.m_listFunc.size());

	bool foundCounter = false, foundIncrement = false;
	for (const auto& v : dst.m_listVar)
		if (v.m_strRealName.IsSameAs(wxT("counter"), false)) {
			foundCounter = true;
			EXPECT_TRUE(v.IsExport());
			break;
		}
	for (const auto& fn : dst.m_listFunc)
		if (fn.m_strRealName.IsSameAs(wxT("Increment"), false)) {
			foundIncrement = true;
			EXPECT_EQ(fn.m_listParam.size(), 1u);
			break;
		}
	EXPECT_TRUE(foundCounter)   << "exported 'counter' lost in AOT round-trip";
	EXPECT_TRUE(foundIncrement) << "function 'Increment' lost in AOT round-trip";
}

// ===========================================================================
// Closure capture — Phase A (compile-side emit only; runtime not wired)
// ===========================================================================
//
// Verifies:
//   * Lambda body references outer-fn local — compile succeeds (was
//     ERROR_VAR_NOT_FOUND pre-Phase-A).
//   * Outer fn's ibByteFunction.m_needsHeapFrame = true after lambda
//     captures one of its locals.
//   * Bytecode dump shows OPER_GET inside lambda body reading at
//     m_numArray ≥ 1 (m_pppArrayList depth past lambda's own frame).
//   * Functions with NO inner lambda stay at m_needsHeapFrame = false.

namespace {

// Find an ibByteFunction by real name (case-insensitive).
const ibByteCode::ibByteFunction* FindFnByName(const ibByteCode& bc, const wxString& name) {
	for (const auto& fn : bc.m_listFunc)
		if (fn.m_strRealName.IsSameAs(name, false))
			return &fn;
	return nullptr;
}

// Find a lambda (kind == Lambda) — returns first match.
const ibByteCode::ibByteFunction* FindFirstLambda(const ibByteCode& bc) {
	for (const auto& fn : bc.m_listFunc)
		if (fn.IsLambda())
			return &fn;
	return nullptr;
}

} // namespace

TEST(ClosureCapture, LambdaReferencingOuterLocalCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function makeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	EXPECT_TRUE(TryCompile(cc, src)) << "lambda capturing outer 'n' should compile";
}

TEST(ClosureCapture, OuterFnMarkedNeedsHeapFrameAfterCapture) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function makeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const auto* outer = FindFnByName(cc.m_cByteCode, wxT("makeAdder"));
	ASSERT_NE(outer, nullptr) << "makeAdder missing from m_listFunc";
	EXPECT_TRUE(outer->m_needsHeapFrame)
		<< "makeAdder must be marked: its 'n' is captured by inner lambda";
}

TEST(ClosureCapture, NoCaptureLeavesNeedsHeapFrameFalse) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	// No inner lambda → no capture → no heap-frame requirement.
	const wxString src =
		wxT("Function plainAdd(a, b)\n")
		wxT("  Return a + b;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const auto* fn = FindFnByName(cc.m_cByteCode, wxT("plainAdd"));
	ASSERT_NE(fn, nullptr);
	EXPECT_FALSE(fn->m_needsHeapFrame)
		<< "fn without inner lambda must NOT be heap-promoted";
}

TEST(ClosureCapture, LambdaBodyEmitsDepthGreaterOrEqualOneForCapturedVar) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function makeAdder(n)\n")
		wxT("  Return Function(x)\n")
		wxT("           Return x + n;\n")
		wxT("         EndFunction;\n")
		wxT("EndFunction\n");
	ASSERT_TRUE(TryCompile(cc, src));

	const auto* lambda = FindFirstLambda(cc.m_cByteCode);
	ASSERT_NE(lambda, nullptr) << "lambda missing from m_listFunc";

	// Walk lambda body opcodes [m_lCodeLine .. m_lCodeLine + (range)].
	// EmitFunctionBody stamps body IPs into m_listCode; the lambda's
	// reference to outer 'n' must produce an opcode whose source-slot
	// parameter has m_numArray ≥ 1 (= depth past own frame).
	const long startIp  = lambda->m_lCodeLine;
	const long lastCode = (long)cc.m_cByteCode.m_listCode.size();
	bool sawDepthGEOne = false;
	for (long ip = startIp; ip < lastCode; ++ip) {
		const ibByteUnit& u = cc.m_cByteCode.m_listCode[ip];
		if (u.m_numOper == OPER_ENDLFUNC) break;
		// Any of the four params reading from an outer frame (depth ≥ 1)
		// counts; OPER_ADD's source operands sit on m_param2/m_param3.
		if (u.m_param1.m_numArray >= 1 ||
		    u.m_param2.m_numArray >= 1 ||
		    u.m_param3.m_numArray >= 1 ||
		    u.m_param4.m_numArray >= 1) {
			sawDepthGEOne = true;
			break;
		}
	}
	EXPECT_TRUE(sawDepthGEOne)
		<< "lambda body must reference captured outer at depth ≥ 1";
}

// ===========================================================================
// Diagnostics — a failure leaves the engine as DATA, not as a sentence
//
// The message a person reads is assembled FROM the record (compiler-pipeline.md
// §7), so these tests pin the record, not the wording: a translated or reworded
// message must not change what a build step or an assistant reads.
// ===========================================================================

#include "backend/backend_diagnostic.h"

namespace {

// Collects everything published while it is subscribed. Unsubscribes itself,
// so a test that throws cannot leave a dangling sink behind.
class CollectingSink : public ibDiagnosticSink {
public:
	CollectingSink() { ibDiagnostics::Subscribe(this); }
	~CollectingSink() override { ibDiagnostics::Unsubscribe(this); }

	void OnDiagnostic(const ibDiagnostic& diagnostic) override {
		m_seen.push_back(diagnostic);
	}

	std::vector<ibDiagnostic> m_seen;
};

} // namespace

TEST(Diagnostics, CompileFailurePublishesTheRecord) {
	CollectingSink sink;

	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc, wxT("a = ")));

	ASSERT_FALSE(sink.m_seen.empty()) << "a refused compile published nothing";

	const ibDiagnostic& diagnostic = sink.m_seen.front();
	EXPECT_EQ(ibDiagnosticKind::Compile, diagnostic.m_kind)
		<< "text that never ran must not be reported as a runtime failure";
	EXPECT_TRUE(diagnostic.IsOk());
	EXPECT_FALSE(diagnostic.m_message.IsEmpty());
	// The position is the whole point: a caller must be able to jump there
	// without reading the sentence.
	EXPECT_GT(diagnostic.m_line, 0u);
	// ibCompileCode(moduleName, docPath, onlyFunction) — the FIRST argument is the module's name.
	EXPECT_EQ(wxT("test"), diagnostic.m_moduleName);
	EXPECT_EQ(wxT("memory"), diagnostic.m_docPath);
}

TEST(Diagnostics, TheMessageCarriesNoDecoration) {
	// "{memory(1)}: " belongs to the DISPLAY string. Putting it in the record
	// too would mean every consumer strips it back off — and would make the
	// record depend on how the dialog happens to format today.
	CollectingSink sink;

	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc, wxT("a = ")));

	ASSERT_FALSE(sink.m_seen.empty());
	EXPECT_EQ(wxNOT_FOUND, sink.m_seen.front().m_message.Find(wxT("{memory(")))
		<< "the record must not carry the display decoration";
}

TEST(Diagnostics, SubscribingTwiceDeliversOnce) {
	// Otherwise a plugin that re-registers on reload silently doubles every
	// report, and the doubling is invisible until someone counts.
	CollectingSink sink;
	ibDiagnostics::Subscribe(&sink);   // the ctor already did this

	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc, wxT("a = ")));

	EXPECT_EQ(1u, sink.m_seen.size());
}

TEST(Diagnostics, UnsubscribedSinkHearsNothing) {
	std::size_t seenAfterLeaving = 0;
	{
		CollectingSink sink;
		ibCompileCode warmUp(wxT("test"), wxT("memory"), false);
		EXPECT_FALSE(TryCompile(warmUp, wxT("a = ")));
		ASSERT_FALSE(sink.m_seen.empty());
		seenAfterLeaving = sink.m_seen.size();
	}   // unsubscribed here

	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc, wxT("b = ")));

	EXPECT_EQ(1u, seenAfterLeaving);   // and nothing crashed publishing to a dead sink
}

// ===========================================================================
// Plugin host — the capability boundary (pluginApi.h ABI 2 / pluginHost.h)
//
// The host hands a plugin ONE C struct and a `query` function; everything else
// is asked for by name and version. These tests pin that contract, because it
// is the piece a third-party binary depends on and therefore the piece that
// must not drift silently.
// ===========================================================================

#include "backend/plugin/pluginApi.h"
#include "backend/plugin/pluginHost.h"
#include "backend/plugin/pluginManager.h"

TEST(PluginHost, HandsOutTheCapabilitiesItAdvertises) {
	const ibPluginHost* host = ibPluginHostInstance();
	ASSERT_NE(nullptr, host);
	EXPECT_EQ(IB_PLUGIN_ABI_VERSION, host->abi_version);
	ASSERT_NE(nullptr, host->query);

	EXPECT_NE(nullptr, host->query(host, ibCapabilityDiagnostics, 1));
	EXPECT_NE(nullptr, host->query(host, ibCapabilityScript, 1));
	EXPECT_NE(nullptr, host->query(host, ibCapabilityMetadata, 1));
}

TEST(PluginHost, RefusesUnknownNamesAndWrongVersions) {
	const ibPluginHost* host = ibPluginHostInstance();
	ASSERT_NE(nullptr, host);

	EXPECT_EQ(nullptr, host->query(host, "nothing-like-this", 1));
	EXPECT_EQ(nullptr, host->query(host, nullptr, 1));
	// A version the host does not implement must answer NULL rather than hand
	// back a layout the caller was not built against — that is the whole reason
	// capabilities are versioned one by one.
	EXPECT_EQ(nullptr, host->query(host, ibCapabilityScript, 2));
	EXPECT_EQ(nullptr, host->query(host, ibCapabilityScript, 0));
}

TEST(PluginHost, ScriptCheckReportsWhereTheCodeIsWrong) {
	const ibPluginHost* host = ibPluginHostInstance();
	const auto* script = static_cast<const ibPluginScript*>(host->query(host, ibCapabilityScript, 1));
	ASSERT_NE(nullptr, script);

	const std::vector<ibDiagnostic> found = script->Check(wxT("a = "), wxT("draft"));

	ASSERT_FALSE(found.empty()) << "a broken text must not check out clean";
	EXPECT_EQ(ibDiagnosticKind::Compile, found.front().m_kind);
	EXPECT_GT(found.front().m_line, 0u);
	EXPECT_EQ(wxT("draft"), found.front().m_moduleName)
		<< "the caller's module name is what the report must speak of";
}

TEST(PluginHost, ScriptCheckPassesValidCode) {
	const ibPluginHost* host = ibPluginHostInstance();
	const auto* script = static_cast<const ibPluginScript*>(host->query(host, ibCapabilityScript, 1));
	ASSERT_NE(nullptr, script);

	// VES dialect — the suite's global environment sets CODE_VES.
	const std::vector<ibDiagnostic> found = script->Check(
		wxT("Procedure DoNothing()\nEndProcedure\n"), wxT("draft"));

	EXPECT_TRUE(found.empty()) << "valid code reported "
		<< found.size() << " diagnostic(s); first: "
		<< (found.empty() ? wxString() : found.front().m_message).ToStdString();
}

TEST(PluginHost, MetadataAnswersEmptyWithNoConfiguration) {
	// A plugin may load long before — or entirely without — an open
	// configuration. Answering empty is the contract; crashing or asserting
	// would make every headless host a special case.
	const ibPluginHost* host = ibPluginHostInstance();
	const auto* metadata = static_cast<const ibPluginMetadata*>(host->query(host, ibCapabilityMetadata, 1));
	ASSERT_NE(nullptr, metadata);

	if (metadata->IsConfigurationOpen())
		GTEST_SKIP() << "a configuration is open in this run — nothing to assert here";

	EXPECT_TRUE(metadata->List(wxT("Catalog")).empty());
	EXPECT_TRUE(metadata->Describe(wxT("Catalog"), wxT("Anything")).IsEmpty());
}

// ===========================================================================
// Declared types — the grammar is `[modifier] Type name [= default]`
//
// Before 2026-08-04 only the five primitives were accepted in that position.
// Now any REGISTERED type is, including the dotted metadata form, because a
// reference type's registered name already IS "<Kind>Ref.<Name>" (objCtor.h).
//
// The tests below pin the two halves of the contract: the new forms compile,
// and — more importantly — code that was valid before still means the same
// thing. An identifier that happens to share a name with a type must stay an
// identifier.
// ===========================================================================

TEST(DeclaredTypes, PrimitiveStillDeclares) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc, wxT("Boolean flag;\nflag = True;\n")));
}

TEST(DeclaredTypes, RegisteredValueClassDeclares) {
	// "Array" is registered by VALUE_TYPE_REGISTER — the same name `New Array`
	// resolves through. One registry, one spelling.
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc, wxT("Array rows;\n")));
}

TEST(DeclaredTypes, ATypeNameAloneIsStillAnIdentifier) {
	// THE COMPATIBILITY TEST. `Array` followed by anything that is not an
	// identifier must keep meaning what it meant: a variable name. Deciding on
	// the type name alone was safe with five reserved primitives and stops being
	// safe once every registered class qualifies.
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc, wxT("var Array;\nArray = 5;\n")));
}

TEST(DeclaredTypes, UnknownDottedNameIsRefused) {
	// A metadata type that does not exist must be an error at COMPILE time —
	// the whole point of naming a type is that the name is checked.
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc, wxT("CatalogRef.NoSuchThing item;\n")));
}

TEST(DeclaredTypes, TypedParameterCompiles) {
	// The form that matters most for tooling: a signature that states what it
	// takes. One declaration explains every call site.
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc,
		wxT("Procedure Handle(Boolean cancel, Number depth)\n")
		wxT("EndProcedure\n")));
}

TEST(DeclaredTypes, TypeNamesAreCaseInsensitive) {
	// The language is case-insensitive everywhere else; the type slot has to
	// agree, or a declared `Array` and a written `array` become two types.
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc, wxT("array rows;\n")));
}

// ===========================================================================
// THE GATE (ibCtorAbstractType::AllowValue)
//
// A type is the only thing that knows what it accepts, and the list of types is
// open — a plugin can register one. So the question "is this type allowed where
// this type is declared" is asked of the TYPE, by class id.
//
// The default is the plain comparison; a barrier admits a whole family.
// ===========================================================================

#include "backend/system/value/valueArray.h"   // a concrete registered type to hold up to a gate

TEST(TypeGate, DefaultIsThePlainComparison) {
	const ibCtorAbstractType* array = ibValue::GetAvailableCtor(wxT("Array"));
	const ibCtorAbstractType* number = ibValue::GetAvailableCtor(wxT("Number"));
	ASSERT_NE(nullptr, array);
	ASSERT_NE(nullptr, number);

	EXPECT_TRUE(array->AllowValue(array->GetClassType()))
		<< "a type must allow its own class";
	EXPECT_FALSE(array->AllowValue(number->GetClassType()))
		<< "and nothing else";
}

// ===========================================================================
// Barrier types — a whole FAMILY declared by name
//
// `Any`, `AnyRef`, `AnyControl` are registered types that create nothing; their
// gate admits a KIND. The metatype families (`CatalogRef`, …) arrive with their
// metatype and admit the references that registered into them. See typeAny.cpp
// and metaCtor.h.
// ===========================================================================


TEST(BarrierTypes, AreRegisteredLikeAnyOtherType) {
	// The point of making them types: every declaration site resolves them
	// through the ordinary registry, with no special case anywhere.
	for (const wchar_t* name : { L"Any", L"AnyRef", L"AnyControl", L"AnyValue" }) {
		EXPECT_TRUE(ibValue::IsRegisterCtor(wxString(name)))
			<< "barrier type not registered: " << wxString(name).ToStdString();
		EXPECT_NE(nullptr, ibValue::GetAvailableCtor(wxString(name)));
	}
}

TEST(BarrierTypes, CreateNothing) {
	// No value is ever "an AnyRef" — the name exists to be declared.
	const ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(wxT("AnyRef"));
	ASSERT_NE(nullptr, ctor);
	EXPECT_EQ(nullptr, ctor->CreateObject());
}

TEST(BarrierTypes, CarryOrdinaryIdsOfTheirOwn) {
	// No special encoding: a barrier has a normal registered id, and the family
	// it stands for lives in its gate, not in its id.
	const ibCtorAbstractType* anyRef = ibValue::GetAvailableCtor(wxT("AnyRef"));
	const ibCtorAbstractType* array = ibValue::GetAvailableCtor(wxT("Array"));
	ASSERT_NE(nullptr, anyRef);
	ASSERT_NE(nullptr, array);

	EXPECT_NE(anyRef->GetClassType(), array->GetClassType());
	EXPECT_NE(0u, anyRef->GetClassType());
}

TEST(BarrierTypes, UnrestrictedAcceptsEverything) {
	const ibCtorAbstractType* any = ibValue::GetAvailableCtor(wxT("Any"));
	const ibCtorAbstractType* array = ibValue::GetAvailableCtor(wxT("Array"));
	ASSERT_NE(nullptr, any);
	ASSERT_NE(nullptr, array);

	EXPECT_TRUE(any->AllowValue(array->GetClassType()));
	EXPECT_TRUE(any->AllowValue(g_valueUndefinedCLSID));
}

TEST(BarrierTypes, EmptyAlwaysFits) {
	// A declared parameter nobody passed, a reference not yet filled in — the
	// declaration says what the value IS when there is one; it does not promise
	// there is one.
	const ibCtorAbstractType* anyControl = ibValue::GetAvailableCtor(wxT("AnyControl"));
	ASSERT_NE(nullptr, anyControl);
	EXPECT_TRUE(anyControl->AllowValue(g_valueUndefinedCLSID));
}

TEST(BarrierTypes, WrongKindIsRefused) {
	const ibCtorAbstractType* anyControl = ibValue::GetAvailableCtor(wxT("AnyControl"));
	const ibCtorAbstractType* array = ibValue::GetAvailableCtor(wxT("Array"));
	ASSERT_NE(nullptr, anyControl);
	ASSERT_NE(nullptr, array);

	// An Array is a value class, not a control.
	EXPECT_FALSE(anyControl->AllowValue(array->GetClassType()));
}

TEST(BarrierTypes, AMetatypeFamilyExistsWithoutAConfiguration) {
	// `CatalogRef` is registered BESIDE THE METATYPE `Catalog` — on the metatype's
	// own registration event, not by any catalog. So the name is declarable in a
	// configuration that has no catalogs yet, and in a test binary that has no
	// configuration at all.
	const ibCtorAbstractType* catalogRef = ibValue::GetAvailableCtor(wxT("CatalogRef"));
	ASSERT_NE(nullptr, catalogRef) << "the family did not arrive with its metatype";

	EXPECT_EQ(nullptr, catalogRef->CreateObject())
		<< "no value is ever a CatalogRef";
}

TEST(BarrierTypes, AMetatypeFamilyIsEmptyUntilItsReferencesRegister) {
	// Membership is RECORDED, not derived: a catalog reference joins the family
	// as it registers (objCtor.h). With no configuration open nothing has joined,
	// so only EMPTY passes — and anything that is not a reference never would.
	const ibCtorAbstractType* catalogRef = ibValue::GetAvailableCtor(wxT("CatalogRef"));
	const ibCtorAbstractType* number = ibValue::GetAvailableCtor(wxT("Number"));
	ASSERT_NE(nullptr, catalogRef);
	ASSERT_NE(nullptr, number);

	EXPECT_TRUE(catalogRef->AllowValue(g_valueUndefinedCLSID));
	EXPECT_FALSE(catalogRef->AllowValue(number->GetClassType()));
}

TEST(BarrierTypes, OnlyReferenceBearingMetatypesGetAFamily) {
	// The metatype answers for itself (s_hasReference), so this needs no list:
	// a register has no references and therefore no family name.
	EXPECT_NE(nullptr, ibValue::GetAvailableCtor(wxT("DocumentRef")));
	EXPECT_EQ(nullptr, ibValue::GetAvailableCtor(wxT("InformationRegisterRef")));
}
TEST(BarrierTypes, AKindBarrierAdmitsEveryMemberOfItsKind) {
	// The whole point: membership is the KIND, so a type registered later is
	// admitted without anybody editing the barrier.
	const ibCtorAbstractType* anyValue = ibValue::GetAvailableCtor(wxT("AnyValue"));
	ASSERT_NE(nullptr, anyValue);

	for (const wchar_t* name : { L"Array", L"Structure", L"Container" }) {
		const ibCtorAbstractType* ctor = ibValue::GetAvailableCtor(wxString(name));
		if (ctor == nullptr) continue;
		EXPECT_TRUE(anyValue->AllowValue(ctor->GetClassType()))
			<< wxString(name).ToStdString() << " should be an AnyValue";
	}
}
TEST(AnyKinds, DeclarationCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc, wxT("AnyValue payload;\n")));
}

TEST(AnyKinds, TypedParameterOfAKindCompiles) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_TRUE(TryCompile(cc,
		wxT("Procedure OnClick(AnyControl element)\n")
		wxT("EndProcedure\n")));
}

// ===========================================================================
// Value serialization — the EXISTING envelope, now with composites
//
// The platform already had the shape: ibMetaData::Serialize writes the class id
// and the payload, ibMetaData::Deserialize reads the class id, CREATES the
// value and hands it the rest to sort out. What it lacked was composites — an
// array or a structure had no payload form, so a settings blob could hold a
// number but not a list of them.
//
// Now each class packs its own contents (valueArray.cpp / valueMap.cpp /
// reference.cpp) and the envelope nests. These tests pin the round trip and the
// refusals, in the ONE format the platform uses — there is no second one.
// ===========================================================================

#include "backend/metaData.h"
#include "backend/system/value/valueArray.h"
#include "backend/system/value/valueMap.h"
#include "backend/system/value/valueColour.h"
#include "backend/serialize/dataBuilder.h"
#include "backend/backend_exception.h"

namespace {
// text of the mechanism, not of any format: node out, node in.
ibValue RoundTrip(const ibValue& original)
{
	ibDataNode node(original.GetClassType(), 0);
	if (!original.Serialize(node))
		return ibValue();
	return ibValue::FromNode(node);
}
} // namespace

TEST(ValueSerialize, PrimitivesRoundTrip) {
	const std::vector<ibValue> samples = {
		ibValue(true),
		ibValue(false),
		ibValue(wxT("text with ;;; separators and |pipes|")),
		ibValue(42),
	};

	for (const ibValue& original : samples) {
		ibDataNode node(original.GetClassType(), 0);
		ASSERT_TRUE(original.Serialize(node))
			<< "nothing written for " << original.GetClassName().ToStdString();

		const ibValue restored = ibValue::FromNode(node);
		EXPECT_EQ(original.GetType(), restored.GetType());
		EXPECT_EQ(original.GetString(), restored.GetString());
	}
}

TEST(ValueSerialize, SeparatorsInsideAStringDoNotBreakTheParse) {
	// THE BUG THE LENGTH FIELD EXISTS TO PREVENT. The envelope declares L:<n>
	// and the reader now honours it; splitting on ";;;" instead cut this value
	// in half and called the remainder garbage.
	const ibValue original(wxT("a;;;b;;;c"));
	const ibValue restored = RoundTrip(original);
	EXPECT_EQ(wxT("a;;;b;;;c"), restored.GetString());
}

TEST(ValueSerialize, ExactNumbersSurvive) {
	const ibValue original(ibNumber(wxT("12345678901234.1234567891")));
	const ibValue restored = RoundTrip(original);
	EXPECT_EQ(wxT("12345678901234.1234567891"), restored.GetString());
}

TEST(ValueSerialize, ArraysNestAndSurvive) {
	ibValuePtr<ibValueArray> inner(new ibValueArray());
	inner->Add(ibValue(1));
	inner->Add(ibValue(wxT("two")));

	ibValuePtr<ibValueArray> outer(new ibValueArray());
	outer->Add(ibValue(true));
	outer->Add(ibValue(inner));

	const ibValue restored = RoundTrip(ibValue(outer));
	ibValueArray* array = nullptr;
	ASSERT_TRUE(const_cast<ibValue&>(restored).ConvertToValue(array));
	ASSERT_NE(nullptr, array);
	EXPECT_EQ(2u, array->Count());

	// The nested array must come back as an array, not as its text.
	ibValue nested;
	ASSERT_TRUE(array->GetAt(ibValue(1), nested));
	ibValueArray* innerRestored = nullptr;
	ASSERT_TRUE(nested.ConvertToValue(innerRestored));
	ASSERT_NE(nullptr, innerRestored);
	EXPECT_EQ(2u, innerRestored->Count());
}

TEST(ValueSerialize, StructuresComeBackAsStructures) {
	ibValuePtr<ibValueStructure> structure(new ibValueStructure());
	structure->Insert(ibValue(wxT("Name")), ibValue(wxT("Widget")));
	structure->Insert(ibValue(wxT("Count")), ibValue(7));

	const ibValue original(structure);
	const ibValue restored = RoundTrip(original);

	EXPECT_EQ(original.GetClassType(), restored.GetClassType())
		<< "a structure must not come back as a plain container";

	ibValueContainer* container = nullptr;
	ASSERT_TRUE(const_cast<ibValue&>(restored).ConvertToValue(container));
	ASSERT_NE(nullptr, container);
	EXPECT_EQ(2u, container->Count());
}

TEST(ValueSerialize, EmptyArrayRoundTrips) {
	ibValuePtr<ibValueArray> empty(new ibValueArray());
	const ibValue restored = RoundTrip(ibValue(empty));

	ibValueArray* array = nullptr;
	ASSERT_TRUE(const_cast<ibValue&>(restored).ConvertToValue(array));
	ASSERT_NE(nullptr, array);
	EXPECT_EQ(0u, array->Count());
}

TEST(ValueSerialize, AValueWithNoPackedFormIsRefused) {
	// A colour is a perfectly ordinary registered value with no payload form.
	// Refusing is the contract: writing it as something else would produce a
	// blob that restores into the wrong thing.
	ibValuePtr<ibValueColour> colour(new ibValueColour());
	ibDataNode node(colour->GetClassType(), 0);
	EXPECT_FALSE(ibValue(colour).Serialize(node));
}

TEST(ValueSerialize, ANodeWithoutATypeReadsAsUndefined) {
	// NO TYPE IN THE NODE IS NOT AN ERROR — it is Undefined. There is no such
	// thing as a value with class id 0: a value whose type is nothing IS
	// Undefined, so that is what comes back. (A node naming a type NOBODY has is
	// the error, and it still raises — see UnknownTypeRaises.)
	ibDataNode node(0, 0);
	const ibValue restored = ibValue::FromNode(node);
	EXPECT_EQ(ibValueTypes::TYPE_EMPTY, restored.GetType());
}
TEST(ValueSerialize, NodePathIsTheMechanism) {
	// The node pair is the whole mechanism; text is that same node rendered by a
	// provider. Pinning the node path directly means a change of text format
	// cannot quietly break what it is a rendering of.
	ibDataNode node(g_valueStringCLSID, 0);
	const ibValue original(wxT("through the node"));

	ASSERT_TRUE(original.Serialize(node));

	const ibValue restored = ibValue::FromNode(node);
	EXPECT_EQ(original.GetType(), restored.GetType());
	EXPECT_EQ(original.GetString(), restored.GetString());
}

TEST(ValueSerialize, NodeCarriesTheTypeFirst) {
	// The header is the base's job and is written for everybody, so a child can
	// neither forget it nor spell it differently. A reader that cannot see the
	// type has nothing to create.
	ibDataNode node(g_valueNumberCLSID, 0);
	ASSERT_TRUE(ibValue(7).Serialize(node));

	EXPECT_EQ(wxString::Format(wxT("%llu"), (unsigned long long)g_valueNumberCLSID),
		node.GetValue<wxString>(wxT("t")));
}

TEST(ValueSerialize, UnknownTypeInANodeIsRefused) {
	// THE END OF THE LINE. The metadata tried its own registry and sent the type
	// down here; if the value factory does not have it either, nobody does. An
	// empty would be indistinguishable from a value that legitimately IS empty,
	// and would surface far away as a blank field nobody can explain.
	ibDataNode node(0, 0);
	node.SetValue(wxT("t"), wxString(wxT("8613303245920547140")));   // no such registered type

	EXPECT_THROW(ibValue::FromNode(node), ibBackendException);
}

// ===========================================================================
// The metadata is the DOOR — bytes in, a live value out
// ===========================================================================
//
// The real configuration needs a database, a metadata tree and a load pass.
// None of that is what this is testing: what is being tested is the CONTRACT of
// the door — a blob in, a value out, an exception when there is no value. So
// the configuration is STOOD IN FOR here, which is enough precisely because the
// door is narrow: five pure virtuals nobody on this path calls, plus the ctor
// registry, which the stand-in answers for itself.
//
// That is also how the redirect gets tested at all. A stand-in that CLAIMS a
// class id proves the metadata creates it from its own registry; one that
// claims nothing proves everything else falls through to the value factory.

namespace {

class StandInMetaData : public ibMetaData {
public:
	// The five the base leaves open. Nothing on the serialization path calls
	// them — they are what makes a metadata a *configuration*, and this is not
	// pretending to be one.
	void SetVersion(const ibVersionID& version) override { m_version = version; }
	ibVersionID GetVersion() const override { return m_version; }
	wxString GetLangCode() const override { return wxT("en"); }
	bool RunDatabase(int flags = defaultFlag) override { return true; }
	bool CloseDatabase(int flags = defaultFlag) override { return true; }

	// THE CONFIGURATION'S OWN REGISTRY, stood in for: a real one holds the
	// reference and enum types whose ids come from metaIDs. Claiming a class id
	// here is the same shape of answer, without a configuration behind it.
	//
	// ADDS to the base rather than replacing it — the base is where the fall
	// through to ibValue's registry lives, and a configuration that answered
	// only for its own types would break every built-in one.
	bool IsRegisterCtor(const ibClassID& clsid) const override {
		return (m_claimed != 0 && clsid == m_claimed) || ibMetaData::IsRegisterCtor(clsid);
	}

	void Claim(const ibClassID& clsid) { m_claimed = clsid; }

private:
	ibVersionID m_version = 0;
	ibClassID m_claimed = 0;
};

} // namespace

TEST(MetaDataSerialize, RoundTripsThroughTheDoor) {
	StandInMetaData metaData;

	const std::vector<ibValue> samples = {
		ibValue(true),
		ibValue(wxT("there and back")),
		ibValue(42),
	};

	for (const ibValue& original : samples) {
		ibDataNode node(original.GetClassType(), 0);
		metaData.Serialize(original, node);

		const ibValue restored = metaData.Deserialize(node);
		EXPECT_EQ(original.GetType(), restored.GetType());
		EXPECT_EQ(original.GetString(), restored.GetString());
	}
}

TEST(MetaDataSerialize, ACompositeSurvivesTheTree) {
	// The point of the node underneath: a composite has no flat form, and this
	// is the shape that used to be impossible to pack at all.
	StandInMetaData metaData;

	ibValuePtr<ibValueArray> source(new ibValueArray());
	source->Add(ibValue(wxT("first")));
	source->Add(ibValue(2));

	ibDataNode node(source->GetClassType(), 0);
	metaData.Serialize(ibValue(source), node);
	const ibValue restored = metaData.Deserialize(node);

	ibValueArray* array = nullptr;
	ASSERT_TRUE(const_cast<ibValue&>(restored).ConvertToValue(array));
	ASSERT_NE(nullptr, array);
	ASSERT_EQ(2u, array->Count());

	ibValue element;
	ASSERT_TRUE(array->GetAt(ibValue(0), element));
	EXPECT_EQ(wxT("first"), element.GetString());
	ASSERT_TRUE(array->GetAt(ibValue(1), element));
	EXPECT_EQ(wxT("2"), element.GetString());
}

TEST(MetaDataSerialize, TheTreeIS_WhatTravels) {
	// Bytes are the provider's business, not the door's: the same tree written
	// through the binary provider is the blob a caller stores or sends, and
	// reading it back lands on the same value. A second pair of methods taking a
	// buffer would be this choice made twice.
	StandInMetaData metaData;
	const ibValue original(wxT("out through a provider"));

	ibDataNode written(original.GetClassType(), 0);
	metaData.Serialize(original, written);

	ibWriterMemory writer;
	ASSERT_TRUE(ibBinaryProvider().Write(written, writer));

	// HELD, NOT BORROWED FROM A TEMPORARY. buffer() hands back a copy by value and
	// the reader keeps only its pointer, so passing the call directly would leave
	// the reader on freed bytes (fs.h deletes that overload for this reason).
	const wxMemoryBuffer blob = writer.buffer();
	ASSERT_GT(blob.GetDataLen(), 0u);

	ibDataNode read;
	ibReaderMemory reader(blob);
	ASSERT_TRUE(ibBinaryProvider().Read(reader, read));

	EXPECT_EQ(original.GetString(), metaData.Deserialize(read).GetString());
}

TEST(MetaDataSerialize, WhatItDoesNotKnowGoesToTheValueFactory) {
	// The redirect, from the side where it is visible: this stand-in claims
	// nothing, so every type on the way through is the factory's answer —
	// reached inside IsRegisterCtor / CreateObjectRef, not around them.
	StandInMetaData metaData;
	const ibValue original(wxT("through the factory"));

	ibDataNode node(original.GetClassType(), 0);
	metaData.Serialize(original, node);

	EXPECT_EQ(wxT("through the factory"), metaData.Deserialize(node).GetString());
}

TEST(MetaDataSerialize, ItsOwnRegistryIsAskedFirst) {
	// Claiming the id sends creation through the metadata's own registry —
	// where, in a real configuration, a catalog reference lives. The value still
	// reads its own contents afterwards.
	StandInMetaData metaData;
	metaData.Claim(g_valueStringCLSID);

	const ibValue original(wxT("through the configuration"));
	ibDataNode node(original.GetClassType(), 0);
	metaData.Serialize(original, node);

	EXPECT_EQ(wxT("through the configuration"), metaData.Deserialize(node).GetString());
}

TEST(MetaDataSerialize, ATypeNOBODYHasIsRefused) {
	// Not "not mine" — not anywhere. The configuration asked its own registry,
	// the factory asked its own, and there is no such type: the caller wanted a
	// value and there is none to give.
	StandInMetaData metaData;

	ibDataNode node(0, 0);
	node.SetValue(wxT("t"), wxString(wxT("8613303245920547140")));

	EXPECT_THROW(metaData.Deserialize(node), ibBackendException);
}

TEST(MetaDataSerialize, AValueWithNoPackedFormIsRefused) {
	// The refusal reaches the caller as an exception rather than as a tree that
	// would restore into nothing.
	StandInMetaData metaData;
	ibValuePtr<ibValueColour> colour(new ibValueColour());

	ibDataNode node(colour->GetClassType(), 0);
	EXPECT_THROW(metaData.Serialize(ibValue(colour), node), ibBackendException);
}

// ===========================================================================
// CompositionField — a field of a source, as a value
//
// The LEFT side of a condition. It used to be three unrelated members on a
// filter line (a path string, a leaf id, a type) plus a tree payload in the
// dialog plus a source walker — all deriving each other.
// ===========================================================================

#include "backend/system/value/composition/valueComposerField.h"
#include "backend/compositionDescription.h"
#include "backend/composition/dataComposer.h"
#include "backend/query/queryRender.h"   // ibRenderQueryExpr — the text IS the expression rendered

TEST(CompositionField, IsARegisteredType) {
	EXPECT_TRUE(ibValue::IsRegisterCtor(wxT("CompositionField")));
	EXPECT_NE(nullptr, ibValue::GetAvailableCtor(wxT("CompositionField")));
}

TEST(CompositionField, ShowsThePresentationAndFallsBackToThePath) {
	ibValuePtr<ibValueCompositionField> named(
		new ibValueCompositionField(wxT("Supplier.Region.Country"), wxT("Country")));
	EXPECT_EQ(wxT("Country"), named->GetString());

	// Built from script — no presentation, so the path is what there is to show.
	ibValuePtr<ibValueCompositionField> bare(new ibValueCompositionField(wxT("Supplier.Region")));
	EXPECT_EQ(wxT("Supplier.Region"), bare->GetString());
}

TEST(CompositionField, EmptyMeansNoPath) {
	ibValuePtr<ibValueCompositionField> nothing(new ibValueCompositionField());
	EXPECT_TRUE(nothing->IsEmpty());

	ibValuePtr<ibValueCompositionField> something(new ibValueCompositionField(wxT("Amount")));
	EXPECT_FALSE(something->IsEmpty());
}

TEST(CompositionField, TwoFieldsAreEqualByPath) {
	ibValuePtr<ibValueCompositionField> a(new ibValueCompositionField(wxT("Amount"), wxT("Sum")));
	ibValuePtr<ibValueCompositionField> b(new ibValueCompositionField(wxT("AMOUNT"), wxT("Total")));
	ibValuePtr<ibValueCompositionField> c(new ibValueCompositionField(wxT("Price")));

	// The path is the identity; the presentation is a label and the leaf id only
	// means anything against one source.
	EXPECT_TRUE(ibValue(a) == ibValue(b));
	EXPECT_FALSE(ibValue(a) == ibValue(c));
}

TEST(CompositionField, IsNotAStringThatSpellsThePath) {
	// A filter holding text where a field belongs is a different (and wrong)
	// thing; saying so is what makes that visible instead of almost-working.
	ibValuePtr<ibValueCompositionField> field(new ibValueCompositionField(wxT("Amount")));
	EXPECT_FALSE(ibValue(field) == ibValue(wxT("Amount")));
}

TEST(CompositionField, SettingThePathDropsTheBinding) {
	// The type comes from the source the field is bound to. Re-pointing the path
	// must not leave the previous field's type behind — that type would then edit
	// the wrong value.
	ibValuePtr<ibValueCompositionField> field(new ibValueCompositionField(wxT("Amount")));
	field->SetTypeInfo(42, ibTypeDescription(g_valueNumberCLSID));
	ASSERT_EQ(42, field->GetLeafId());

	field->SetPropVal(ibValueCompositionField::enPath, ibValue(wxT("Supplier")));
	EXPECT_EQ(wxNOT_FOUND, field->GetLeafId());
	EXPECT_EQ(0u, field->GetTypeDescription().GetClsidCount());
}

TEST(CompositionField, RoundTripsThroughANode) {
	ibValuePtr<ibValueCompositionField> original(
		new ibValueCompositionField(wxT("Supplier.Region"), wxT("Region")));

	ibDataNode node(original->GetClassType(), 0);
	ASSERT_TRUE(ibValue(original).Serialize(node));

	const ibValue restored = ibValue::FromNode(node);
	ibValueCompositionField* field = nullptr;
	ASSERT_TRUE(const_cast<ibValue&>(restored).ConvertToValue(field));
	ASSERT_NE(nullptr, field);
	EXPECT_EQ(wxT("Supplier.Region"), field->GetPath());
	EXPECT_EQ(wxT("Region"), field->GetPresentation());
}

// ===========================================================================
//  A FILTER IS A TREE OF DESCRIPTIONS — conditions and groups, as data
// ===========================================================================
//
// ⚠ WHAT THESE TESTS USED TO BE WRITTEN AGAINST. There was a second model of a filter beside this
// one — live `ibValueFilterGroup` / `ibValueFilterItem` objects with their own Add / AddGroup /
// GroupChildren / Ungroup — and the window edited THOSE while the file stored these. It went under
// the knife on 2026-08-23; the questions it was asked are the questions below, put to the one shape
// that is left. A test that pins a removed class pins nothing (2026-08-23: a rename left 211 hits in
// tests/ and CI fell on all three platforms — a green .sln means nothing about this tree).

TEST(FilterTree, GroupsAreTheOnlyNodes) {
	// Conditions are leaves. Only a group can hold children — which is what
	// makes the shape a tree rather than a list of lists.
	ibFilterDescription filter;
	filter.Append(wxT("Amount"), ibComparisonKind_Greater, ibValue(100));
	ibFilterDescription::AppendGroup(filter.m_nodes, ibFilterGroupKind_Or);

	ASSERT_EQ(2u, filter.m_nodes.size());
	EXPECT_EQ(ibFilterNodeKind_Condition, filter.m_nodes[0].m_kind);
	EXPECT_TRUE(filter.m_nodes[0].m_children.empty()) << "a condition is a leaf";
	EXPECT_EQ(ibFilterNodeKind_Group, filter.m_nodes[1].m_kind);
}

TEST(FilterTree, AGroupOwnsItsChildren) {
	// ⭐ THE NESTING IS THE STORAGE: a group holds its children inside itself, so nothing names a
	// parent and the two can never disagree. Copying the filter deep-copies the whole tree, which is
	// what every window relies on — edit a copy, put it back on OK.
	ibFilterDescription filter;
	filter.Append(wxT("A"), ibComparisonKind_Equal, ibValue(1));
	ibFilterNodeDescription& either = ibFilterDescription::AppendGroup(filter.m_nodes, ibFilterGroupKind_Or);
	ibFilterDescription::Append(either.m_children, wxT("B"), ibComparisonKind_Equal, ibValue(2));
	ibFilterDescription::Append(either.m_children, wxT("C"), ibComparisonKind_Equal, ibValue(3));

	ASSERT_EQ(2u, filter.m_nodes.size()) << "the children landed beside the group instead of inside it";
	ASSERT_EQ(2u, filter.m_nodes[1].m_children.size());
	EXPECT_EQ(wxT("B"), filter.m_nodes[1].m_children[0].m_left.m_path);

	const ibFilterDescription copy = filter;
	ASSERT_EQ(2u, copy.m_nodes[1].m_children.size()) << "the copy shares the children instead of owning them";
	EXPECT_EQ(filter, copy) << "a filter compares by what it says";
}

TEST(FilterTree, ASideIsAFieldOrAValue) {
	// `Price > Cost` — both sides are the same shape, which is what lets a condition compare two
	// fields and not only a field with a literal.
	ibFilterDescription filter;
	ibFilterNodeDescription& node = filter.Append(wxT("Price"), ibComparisonKind_Greater, ibValue());
	node.m_right.m_path = wxT("Cost");

	EXPECT_TRUE(node.m_left.IsField());
	EXPECT_TRUE(node.m_right.IsField());

	ibFilterDescription other;
	ibFilterNodeDescription& literal = other.Append(wxT("Price"), ibComparisonKind_Greater, ibValue(100));
	EXPECT_TRUE(literal.m_left.IsField());
	EXPECT_FALSE(literal.m_right.IsField()) << "a literal side must not read as a path";
}

TEST(FilterTree, ALineHoldsWhatThePickerResolved) {
	// What the picker resolved must survive being put into a condition: the leaf id and the type are
	// what make the OTHER side editable, and the presentation is what a person reads.
	ibFilterDescription filter;
	ibFilterNodeDescription& node = filter.Append(wxT("Amount"), ibComparisonKind_Greater, ibValue(100));
	node.m_left.m_leafId       = 7;
	node.m_left.m_type         = ibTypeDescription(g_valueNumberCLSID);
	node.m_left.m_presentation = wxT("Sum");

	EXPECT_EQ(wxT("Amount"), node.m_left.m_path);
	EXPECT_EQ(7, node.m_left.m_leafId) << "the type the picker resolved was lost on the way in";
	EXPECT_EQ(wxT("Sum"), node.m_left.m_presentation);
	EXPECT_TRUE(node.m_left.m_type.ContainType(ibValueTypes::TYPE_NUMBER));
}

// ===========================================================================
//  …AND IT IS SAVED AS A TREE
// ===========================================================================

TEST(FilterTreePersist, ANestedTreeSurvivesTheRoundTrip) {
	ibFilterDescription filter;
	ibFilterNodeDescription& first = filter.Append(wxT("Amount"), ibComparisonKind_Greater, ibValue(100));
	first.m_left.m_presentation = wxT("Sum");

	ibFilterNodeDescription& either = ibFilterDescription::AppendGroup(filter.m_nodes, ibFilterGroupKind_Or);
	ibFilterDescription::Append(either.m_children, wxT("Region"), ibComparisonKind_Equal, ibValue(wxT("north")));
	ibFilterNodeDescription& pair =
		ibFilterDescription::Append(either.m_children, wxT("Price"), ibComparisonKind_Greater, ibValue());
	pair.m_right.m_path = wxT("Cost");

	ibDataNode node(make_clsid("CompositionFilter", ibClassKind_None), 0);
	ASSERT_TRUE(ibFilterDescriptionMemory::WriteNode(node, filter));

	ibFilterDescription restored;
	ASSERT_TRUE(ibFilterDescriptionMemory::ReadNode(node, restored));

	ASSERT_EQ(2u, restored.m_nodes.size());
	EXPECT_EQ(wxT("Amount"), restored.m_nodes[0].m_left.m_path);
	EXPECT_EQ(wxT("Sum"), restored.m_nodes[0].m_left.m_presentation)
		<< "the field lost its presentation on the way through";
	EXPECT_EQ(ibComparisonKind_Greater, restored.m_nodes[0].m_comparison);

	// The nested group, and the field-with-field condition inside it.
	ASSERT_EQ(ibFilterNodeKind_Group, restored.m_nodes[1].m_kind);
	EXPECT_EQ(ibFilterGroupKind_Or, restored.m_nodes[1].m_groupKind);
	ASSERT_EQ(2u, restored.m_nodes[1].m_children.size());
	EXPECT_TRUE(restored.m_nodes[1].m_children[1].m_right.IsField())
		<< "a field on the RIGHT came back as something else";

	EXPECT_EQ(filter, restored) << "the whole tree, not merely the parts asked about above";
}

TEST(FilterTreePersist, ARecordWithoutATreeReadsAsAnEmptyFilter) {
	// A record written before the tree existed has no such child. That is not damage — the filter
	// was empty, and it reads back empty.
	ibDataNode empty(make_clsid("CompositionFilter", ibClassKind_None), 0);
	ibFilterDescription filter;
	ibFilterDescriptionMemory::ReadNode(empty, filter);
	EXPECT_FALSE(filter.IsOk());
	EXPECT_TRUE(filter.m_nodes.empty());
}

// ===========================================================================
//  …AND THE ENGINE READS AN EXPRESSION BUILT FROM IT
// ===========================================================================
//
// The expression is DERIVED and never stored: it can be run, but it cannot be edited back into the
// lines a person wrote. It is built where it is used — ibDataComposer::BuildFilterAst.

TEST(FilterAst, AConditionBecomesACompareNode) {
	ibDataDBComposer composer;
	ibFilterDescription filter;
	filter.Append(wxT("Amount"), ibComparisonKind_Greater, ibValue(100));

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::Compare, ast->m_kind);
	EXPECT_EQ(ibQueryCompareOp::Gt, ast->m_cmp);

	// A field is a COLUMN of dotted segments — what the lowering dot-walks to
	// build its joins.
	ASSERT_TRUE(ast->m_lhs != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::Column, ast->m_lhs->m_kind);
	ASSERT_EQ(1u, ast->m_lhs->m_path.size());
	EXPECT_EQ(wxT("Amount"), ast->m_lhs->m_path[0]);

	// A value is a PARAM — never inlined.
	ASSERT_TRUE(ast->m_rhs != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::Param, ast->m_rhs->m_kind);
	EXPECT_FALSE(ast->m_rhs->m_paramName.IsEmpty());
}

TEST(FilterAst, ADottedPathTravelsAsSegments) {
	ibDataDBComposer composer;
	ibFilterDescription filter;
	filter.Append(wxT("Supplier.Region.Country"), ibComparisonKind_Equal, ibValue(wxT("PL")));

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr && ast->m_lhs != nullptr);
	ASSERT_EQ(3u, ast->m_lhs->m_path.size())
		<< "the path arrived glued together — the lowering would have to split it again";
	EXPECT_EQ(wxT("Country"), ast->m_lhs->m_path[2]);
}

TEST(FilterAst, GroupsBecomeLogicalNodes) {
	ibDataDBComposer composer;
	ibFilterDescription filter;
	filter.m_rootKind = ibFilterGroupKind_And;
	filter.Append(wxT("Amount"), ibComparisonKind_Greater, ibValue(100));

	ibFilterNodeDescription& either = ibFilterDescription::AppendGroup(filter.m_nodes, ibFilterGroupKind_Or);
	ibFilterDescription::Append(either.m_children, wxT("A"), ibComparisonKind_Equal, ibValue(1));
	ibFilterDescription::Append(either.m_children, wxT("B"), ibComparisonKind_Equal, ibValue(2));

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr);
	ASSERT_EQ(ibQueryAstExprKind::Logical, ast->m_kind);
	EXPECT_FALSE(ast->m_isOr) << "the root is an AND group";

	// …and the nested OR is a Logical of its own, which is exactly what a flat
	// list could not express.
	ASSERT_TRUE(ast->m_rhs != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::Logical, ast->m_rhs->m_kind);
	EXPECT_TRUE(ast->m_rhs->m_isOr);
}

TEST(FilterAst, NotWrapsTheWholeGroup) {
	ibDataDBComposer composer;
	ibFilterDescription filter;
	ibFilterNodeDescription& negated =
		ibFilterDescription::AppendGroup(filter.m_nodes, ibFilterGroupKind_Not);
	ibFilterDescription::Append(negated.m_children, wxT("Deleted"), ibComparisonKind_Equal, ibValue(true));

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::Not, ast->m_kind);
	ASSERT_TRUE(ast->m_lhs != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::Compare, ast->m_lhs->m_kind);
}

TEST(FilterAst, ContainsIsALikeNotAComparison) {
	// LIKE is its own node kind — handing the lowering a comparison operator it
	// has no meaning for would push the problem one layer down.
	ibDataDBComposer composer;
	ibFilterDescription filter;
	filter.Append(wxT("Name"), ibComparisonKind_Contains, ibValue(wxT("ab")));

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::Like, ast->m_kind);
}

TEST(FilterAst, InAndInHierarchyAreOneNodeAndAWord) {
	// ⭐ «IN HIERARCHY» DIFFERS FROM «IN» BY THE WORD ALONE — the AST carries the unfold on the In
	// node itself, so both comparisons reuse the one set-valued operator every driver already
	// renders. An operator of its own would be a second way to ask what the language already asks.
	ibDataDBComposer composer;

	ibFilterDescription plain;
	plain.Append(wxT("Item"), ibComparisonKind_In, ibValue(wxT("x")));
	const ibQueryAstExprPtr flat = composer.BuildFilterAst(plain);
	ASSERT_TRUE(flat != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::In, flat->m_kind);
	EXPECT_EQ(ibQueryDimUnfold::Elements, flat->m_unfold);

	ibFilterDescription deep;
	deep.Append(wxT("Item"), ibComparisonKind_InHierarchy, ibValue(wxT("x")));
	const ibQueryAstExprPtr tree = composer.BuildFilterAst(deep);
	ASSERT_TRUE(tree != nullptr);
	EXPECT_EQ(ibQueryAstExprKind::In, tree->m_kind) << "a second node kind for one question";
	EXPECT_EQ(ibQueryDimUnfold::Hierarchy, tree->m_unfold);
}

TEST(FilterAst, ASwitchedOffLineSaysNothing) {
	// Not written, rather than written and then ignored: a condition switched off narrows nothing.
	ibDataDBComposer composer;
	ibFilterDescription filter;
	filter.Append(wxT("Amount"), ibComparisonKind_Greater, ibValue(100), /*use*/ false);

	EXPECT_TRUE(composer.BuildFilterAst(filter) == nullptr);
}

// …AND THE TEXT IS JUST THAT EXPRESSION RENDERED. One renderer for every query, so a filter never
// grows its own idea of where brackets go.

TEST(FilterAst, NestedGroupsRenderWithParentheses) {
	// The reason the tree exists at all: this expression has no flat form.
	ibDataDBComposer composer;
	ibFilterDescription filter;
	filter.m_rootKind = ibFilterGroupKind_And;
	filter.Append(wxT("Amount"), ibComparisonKind_Greater, ibValue(100));

	ibFilterNodeDescription& either = ibFilterDescription::AppendGroup(filter.m_nodes, ibFilterGroupKind_Or);
	ibFilterDescription::Append(either.m_children, wxT("Region"), ibComparisonKind_Equal, ibValue(wxT("north")));
	ibFilterDescription::Append(either.m_children, wxT("Region"), ibComparisonKind_Equal, ibValue(wxT("south")));

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr);
	const wxString expr = ibRenderQueryExpr(*ast);

	EXPECT_TRUE(expr.Contains(wxT("Amount >")));
	EXPECT_TRUE(expr.Contains(wxT(" AND ")));
	EXPECT_TRUE(expr.Contains(wxT(" OR ")));
	EXPECT_TRUE(expr.Contains(wxT("(Region =")))
		<< "an OR group must be parenthesised inside an AND parent: " << expr.ToStdString();
}

TEST(FilterAst, AFieldOnBothSidesRendersAsTwoPaths) {
	// `Price > Cost` — no parameter at all, because neither side is a value.
	ibDataDBComposer composer;
	ibFilterDescription filter;
	ibFilterNodeDescription& node = filter.Append(wxT("Price"), ibComparisonKind_Greater, ibValue());
	node.m_right.m_path = wxT("Cost");

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr);
	const wxString expr = ibRenderQueryExpr(*ast);
	EXPECT_TRUE(expr.Contains(wxT("Price > Cost"))) << expr.ToStdString();
	EXPECT_FALSE(expr.Contains(wxT("&"))) << "a field must render as a path, not a parameter";
}

TEST(FilterAst, AValueRendersAsAParameter) {
	// The other half of the rule above: a VALUE never reaches the text — it travels as a named
	// parameter, which is what keeps a string from being read as syntax and a date from being read
	// in somebody's locale.
	ibDataDBComposer composer;
	ibFilterDescription filter;
	filter.Append(wxT("Name"), ibComparisonKind_Equal, ibValue(wxT("O'Brien")));

	const ibQueryAstExprPtr ast = composer.BuildFilterAst(filter);
	ASSERT_TRUE(ast != nullptr);
	const wxString expr = ibRenderQueryExpr(*ast);
	EXPECT_TRUE(expr.Contains(wxT("&"))) << expr.ToStdString();
	EXPECT_FALSE(expr.Contains(wxT("O'Brien"))) << "the value was inlined into the text";
}

// ===========================================================================
//  THE SETTINGS — three lists, and every one of them is saved
// ===========================================================================
//
// ⚠ THE DEFECT THESE PIN: a sort or a grouping was set, saved, and gone — only the filter travelled.
// One pair writes all three now, so adding a part is adding a line rather than remembering one.

TEST(ListSettingsLines, ASortLineIsALineAndKeepsItsPosition) {
	ibSortDescription order;
	order.Append(wxT("Date"), true);
	order.Append(wxT("Number"), true);
	order.Append(wxT("Sum"), true);

	order.m_lines[1] = { wxT("Code"), false };   // edited in place — order is the meaning of this list

	ASSERT_EQ(3u, order.m_lines.size());
	EXPECT_EQ(wxT("Date"), order.m_lines[0].m_path);
	EXPECT_EQ(wxT("Code"), order.m_lines[1].m_path) << "the edit landed on the line it was made on";
	EXPECT_EQ(wxT("Sum"),  order.m_lines[2].m_path) << "and moved nothing";
	EXPECT_FALSE(order.m_lines[1].m_ascending);
}

TEST(ListSettingsLines, AGroupingLineCarriesItsUnfoldKind) {
	// The kind is load-bearing: a hierarchy grouping IS what makes a list a tree, so dropping it
	// reloads every tree as a flat grouping and reads as data loss.
	ibGroupDescription group;
	group.Append(wxT("Warehouse"));
	group.Append(wxT("Item"), ibQueryDimUnfold::Hierarchy);

	group.m_lines[1].m_kind = ibQueryDimUnfold::HierarchyOnly;   // the kind alone changes
	EXPECT_EQ(wxT("Item"), group.m_lines[1].m_path);
	EXPECT_EQ(ibQueryDimUnfold::HierarchyOnly, group.m_lines[1].m_kind);

	group.m_lines[1].m_path = wxT("Producer");                   // …and the field alone changes
	EXPECT_EQ(wxT("Producer"), group.m_lines[1].m_path);
	EXPECT_EQ(ibQueryDimUnfold::HierarchyOnly, group.m_lines[1].m_kind);
	EXPECT_EQ(2u, group.m_lines.size());
}

TEST(ListSettingsLines, SettingsCarryAllThreeParts) {
	ibSettingsDescription settings;
	settings.m_filter.Append(wxT("Number"), ibComparisonKind_Equal, ibValue(wxT("00001")));
	settings.m_sort.Append(wxT("Date"), false);
	settings.m_group.Append(wxT("Warehouse"), ibQueryDimUnfold::Hierarchy);

	ibDataNode node(make_clsid("CompositionSettings", ibClassKind_None), 0);
	ASSERT_TRUE(ibSettingsDescriptionMemory::WriteNode(node, settings));

	ibSettingsDescription loaded;
	ASSERT_TRUE(ibSettingsDescriptionMemory::ReadNode(node, loaded));

	ASSERT_EQ(1u, loaded.m_filter.m_nodes.size());
	EXPECT_EQ(wxT("Number"), loaded.m_filter.m_nodes[0].m_left.m_path);

	ASSERT_EQ(1u, loaded.m_sort.m_lines.size());
	EXPECT_EQ(wxT("Date"), loaded.m_sort.m_lines[0].m_path);
	EXPECT_FALSE(loaded.m_sort.m_lines[0].m_ascending);

	ASSERT_EQ(1u, loaded.m_group.m_lines.size());
	EXPECT_EQ(wxT("Warehouse"), loaded.m_group.m_lines[0].m_path);
	// The UNFOLD KIND travels with it — dropping it would reload every tree as a flat grouping.
	EXPECT_EQ(ibQueryDimUnfold::Hierarchy, loaded.m_group.m_lines[0].m_kind);

	EXPECT_EQ(settings, loaded) << "the whole of it, not merely the parts asked about above";
}

// ===========================================================================
// `Cached` — a second modifier axis beside the access one
// ===========================================================================

// A PROCEDURE has no result to keep. The modifier is REFUSED there rather than
// ignored, because a silently-dropped Cached is indistinguishable from a cache
// that simply never helps — and the only symptom would be the clock.
TEST(CompilerTest, CachedIsRefusedOnAProcedure) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc,
		wxT("Procedure DoIt(x) Public Cached\n")
		wxT("  x = 1;\n")
		wxT("EndProcedure\n")));
}

// Stated twice is a mistake worth naming, the same way two access modifiers are.
TEST(CompilerTest, CachedIsRefusedWhenStatedTwice) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc,
		wxT("Function F(x) Cached Cached\n")
		wxT("  Return x;\n")
		wxT("EndFunction\n")));
}

// The axes are independent: an access modifier still admits exactly one of its
// own even when Cached is present.
TEST(CompilerTest, TwoAccessModifiersAreStillRefusedAlongsideCached) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	EXPECT_FALSE(TryCompile(cc,
		wxT("Function F(x) Public Private Cached\n")
		wxT("  Return x;\n")
		wxT("EndFunction\n")));
}

// The modifier reaches the BYTECODE — and the call site stays an ORDINARY call.
//
// That second half is the design, not an implementation detail: `Cached` is
// applied at the callee's entry, where every road into a body arrives, so the
// caller emits what it always emitted. An opcode here would have covered only
// the calls this emitter can see — and `CommonModule.Function(...)`, which is
// how a non-global common module is normally called, is not one of them.
TEST(CompilerTest, CachedReachesTheBytecodeAndLeavesTheCallSiteAlone) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var r public;\n")
		wxT("Function Ten(x) Public Cached\n")
		wxT("  Return x * 10;\n")
		wxT("EndFunction\n")
		wxT("r = Ten(5);\n")));

	ASSERT_FALSE(cc.m_cByteCode.m_listFunc.empty());
	const auto& fn = cc.m_cByteCode.m_listFunc[0];
	EXPECT_EQ(fn.m_strRealName, wxT("Ten"));
	EXPECT_TRUE(fn.m_valueCached);
	EXPECT_EQ(fn.m_kind, ibFnKind::Export) << "Cached must not disturb the access axis";

	int numPlainCalls = 0;
	for (const auto& code : cc.m_cByteCode.m_listCode)
		if (code.m_numOper == OPER_CALL) numPlainCalls++;
	EXPECT_EQ(numPlainCalls, 1) << "the call site must stay an ordinary call";
}

// And a function WITHOUT the modifier carries the flag cleared — otherwise the
// test above would pass for a compiler that marked everything cached.
TEST(CompilerTest, AFunctionWithoutCachedCarriesTheFlagCleared) {
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var r public;\n")
		wxT("Function Ten(x) Public\n")
		wxT("  Return x * 10;\n")
		wxT("EndFunction\n")
		wxT("r = Ten(5);\n")));

	ASSERT_FALSE(cc.m_cByteCode.m_listFunc.empty());
	EXPECT_FALSE(cc.m_cByteCode.m_listFunc[0].m_valueCached);
}

// CES is the DEFAULT dialect for new configurations, so a modifier that only
// worked in VES would be broken for most users while every test here passed —
// this binary forces VES process-wide. Style is swapped back before returning,
// including on a failed assertion, so suite order stays unaffected.
TEST(CompilerTest, CachedParsesInTheBraceDialectToo) {
	struct StyleGuard {
		const short saved = ibCompileCode::GetCodeStyle();
		~StyleGuard() { ibCompileCode::SetCodeStyle(saved); }
	} guard;
	ibCompileCode::SetCodeStyle(CODE_CES);

	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	ASSERT_TRUE(TryCompile(cc,
		wxT("var r public;\n")
		wxT("Function Ten(x) Public Cached {\n")
		wxT("  return x * 10;\n")
		wxT("}\n")
		wxT("r = Ten(5);\n")));

	ASSERT_FALSE(cc.m_cByteCode.m_listFunc.empty());
	EXPECT_TRUE(cc.m_cByteCode.m_listFunc[0].m_valueCached);
}

// WHERE A MODIFIER GOES, stated as a test because the syntax helper stated it
// two different ways: Public was documented after the parameter list and
// Private/Protected before the declaration. Only one of those parses — the
// grammar reads all three in ONE place, after the closing bracket (and, for a
// variable, after the name). A leading modifier is not a dialect variant, it is
// a syntax error, and the helper's examples were unrunnable.
TEST(CompilerTest, AccessModifiersAreTrailingForEveryOneOfThem) {
	for (const wxString& access : { wxString(wxT("Public")), wxString(wxT("Private")), wxString(wxT("Protected")) }) {
		{	// trailing — the form the grammar has
			ibCompileCode cc(wxT("test"), wxT("memory"), false);
			EXPECT_TRUE(TryCompile(cc,
				wxT("Function F(x) ") + access + wxT("\n")
				wxT("  Return x;\n")
				wxT("EndFunction\n"))) << access.ToStdString() << " (trailing) must parse";
		}
		{	// leading — refused
			ibCompileCode cc(wxT("test"), wxT("memory"), false);
			EXPECT_FALSE(TryCompile(cc,
				access + wxT(" Function F(x)\n")
				wxT("  Return x;\n")
				wxT("EndFunction\n"))) << access.ToStdString() << " (leading) must not parse";
		}
	}
}

// A VARIABLE takes its modifier in the same trailing place a routine does. The
// language reference claimed the opposite — "a Var takes it in front —
// `Public Var total;` — which is exactly why the routine form is easy to get
// wrong" — and taught a form that does not compile. One parse site reads all
// three modifiers (compileCode.cpp, the Var declaration loop), and it reads
// them after the NAME.
TEST(CompilerTest, AVariableTakesItsModifierAfterTheNameToo) {
	for (const wxString& access : { wxString(wxT("Public")), wxString(wxT("Private")), wxString(wxT("Protected")) }) {
		{	// trailing — the form the grammar has
			ibCompileCode cc(wxT("test"), wxT("memory"), false);
			EXPECT_TRUE(TryCompile(cc, wxT("Var total ") + access + wxT(";\n")))
				<< access.ToStdString() << " (trailing) must parse";
		}
		{	// leading — refused
			ibCompileCode cc(wxT("test"), wxT("memory"), false);
			EXPECT_FALSE(TryCompile(cc, access + wxT(" Var total;\n")))
				<< access.ToStdString() << " (leading) must not parse";
		}
	}
}
