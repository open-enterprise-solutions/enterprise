// =============================================================================
// OES Enterprise — the SCRIPT CORPUS: real sources, compiled and RUN
//
// Every other compiler test in this tree feeds the compiler two to five lines
// written to exercise one rule. This one feeds it `tests/scripts/*.txt` — the
// files a person actually wrote to try the language out: a 13 KB LINQ suite
// with twenty-odd numbered cases, closure tests, string and math suites.
//
// The gap it closes is the one between "the rules pass" and "a module works".
// A synthetic case exercises a rule in isolation; a real file exercises the
// rules TOGETHER — a query inside a foreach inside a function that declares a
// typed local and calls a lambda — and that is where a reader that reads each
// construct correctly can still fall apart, because the constructs meet.
//
// Two questions, in the order they can be answered: does a real module COMPILE,
// and does it RUN to the end without raising. What each line computed is the
// next question, and the scripts already carry the answers in comments.
// =============================================================================

#include <gtest/gtest.h>

#include <wx/dir.h>
#include <wx/filename.h>
#include <wx/textfile.h>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/system/systemManager.h"
#include "backend/appData.h"   // a built-in reads appData before it dispatches

#include <wx/init.h>
#include <wx/image.h>  // wxInitAllImageHandlers — wx decodes nothing until registered
#include <wx/log.h>    // wxLogStderr — a wx warning must not become a modal box
#include <wx/tokenzr.h>   // wxStringTokenize — prefix bisection

#include <cstdio>

// =============================================================================
// WHERE OUTPUT GOES — the context value that declares `Message`, overridden
//
// A test needs no frame and no session for this. `Message` is a method of the
// context value a host binds as the global API, so the test binds ITS OWN
// version of that value and keeps what the script said.
//
// It has to be a SUBCLASS of that value and not a second context beside it.
// Context methods are registered into the compile context by NAME with upsert
// semantics, and the context map is walked alphabetically — so a separate
// `Output` context declaring `Message` is silently overwritten by `System`'s
// when the latter registers, and its handler never runs. Overriding the object
// that OWNS the name is the version with no ordering to get wrong.
// =============================================================================

class ibValueCorpusOutput : public ibValueSystemFunction {
public:

	// Everything except `Message` is the real global API — Sqrt, Round, Format
	// and the other ninety-odd — because a script that takes a square root has
	// to compile and run exactly as it does in a host.
	virtual bool CallAsProc(const long lMethodNum, ibValue** paParams,
		const long lSizeArray) override {

		if (IsMessage(lMethodNum) && lSizeArray > 0 && paParams[0] != nullptr) {
			const wxString strText = paParams[0]->GetString();
			m_listMessage.push_back(strText);
			std::printf("%s\n", (const char*)strText.ToUTF8());
			return true;
		}

		return ibValueSystemFunction::CallAsProc(lMethodNum, paParams, lSizeArray);
	}

	const std::vector<wxString>& GetMessages() const { return m_listMessage; }
	void ClearMessages()                            { m_listMessage.clear(); }

private:

	// BY NAME, not by the number the enum happens to have. The method number is
	// an index into the member table, and a table that gains an entry above
	// `Message` would silently move it — a test that then captured `Alert` would
	// still pass, which is the worst kind of green.
	bool IsMessage(const long lMethodNum) const {
		return const_cast<ibValueCorpusOutput*>(this)
			->GetMethodName(lMethodNum).IsSameAs(wxT("Message"), false);
	}

	std::vector<wxString> m_listMessage;
};

namespace {

// The corpus lives beside the tests; CMake runs the binary from the build tree,
// so the path is resolved from the source location rather than the cwd.
wxString ScriptDir()
{
	wxFileName here(wxString::FromUTF8(__FILE__));
	here.SetFullName(wxEmptyString);
	here.AppendDir(wxT("scripts"));
	return here.GetPath();
}

wxString ReadWhole(const wxString& strPath)
{
	// UTF-8 NAMED, not guessed. The string suite asserts on `Upper("Hello Мир")`
	// and `StrLen("привет")`, so a mis-detected encoding does not fail as an
	// encoding problem — it fails as a wrong answer from a correct function.
	wxTextFile file;
	if (!file.Open(strPath, wxConvUTF8))
		return wxEmptyString;

	wxString strText;
	for (size_t i = 0; i < file.GetLineCount(); i++)
		strText << file.GetLine(i) << wxT("\n");

	return strText;
}

std::vector<wxString> CorpusFiles()
{
	std::vector<wxString> listFile;

	wxDir dir(ScriptDir());
	if (!dir.IsOpened())
		return listFile;

	wxString strName;
	for (bool ok = dir.GetFirst(&strName, wxT("*.txt"), wxDIR_FILES); ok;
	     ok = dir.GetNext(&strName))
		listFile.push_back(ScriptDir() + wxFileName::GetPathSeparator() + strName);

	std::sort(listFile.begin(), listFile.end());
	return listFile;
}

// THE HOST a script is compiled against, in one place because both cases need
// exactly the same one and a difference between them would be a difference
// between "it compiles" and "it runs" that says nothing about the language.
//
// `ibValueSystemFunction` carries the global API — Sqrt, Round, Format, Message
// and the other ninety-odd. A host binds it as a transparent scope, exactly as
// it binds Manager: without it a script that adds two numbers compiles and one
// that takes a square root does not, which measures the host and not the reader.
struct ibCorpusHost {
	ibValueCorpusOutput m_valueSystem;

	void Bind(ibCompileCode& compiler) {
		compiler.AddContextVariable(wxT("System"), &m_valueSystem, true);
	}

	const std::vector<wxString>& GetMessages() const { return m_valueSystem.GetMessages(); }
};

// WHICH DIALECT the file is in is a property of the FILE, not of the run. CES
// hides the VES block fences (EndFunction / EndIf / …), so a source written with
// them cannot even lex under it. Trying both and reporting which one took is
// data; assuming one is a guess.
//
// Returns true when some dialect compiled it, and leaves the compiler holding
// that dialect's bytecode. `strError` is the LAST attempt's message — keeping
// the first reports CES complaining about a VES source, a true sentence about
// the wrong question.
bool CompileEitherDialect(ibCompileCode& compiler, ibCorpusHost& host,
	const wxString& strText, wxString& strError)
{
	for (short style : { (short)CODE_CES, (short)CODE_VES }) {

		ibCompileCode::SetCodeStyle(style);
		compiler.Reset();
		host.Bind(compiler);

		wxString strTry;
		try {
			if (compiler.Compile(strText))
				return true;
		} catch (const ibBackendException& err) {
			strTry = err.GetErrorDescription();
		} catch (...) {
			strTry = wxT("unknown exception");
		}

		strError = strTry;
	}

	return false;
}

// The corpus is written in CES — braces and `;`, `foreach (v in q) { … }` — with
// a VES minority, which the probe above sorts out per file.
struct ScriptCorpus : ::testing::Test {
	// wxBase, then application data — in that order, and both before a script
	// runs. A built-in reads `appData` on entry (ibValueSystemFunction::
	// CallAsFunc), so without this every `Message(...)` in the corpus faults
	// instead of printing, and the run reads as a language failure.
	wxInitializer m_wxInit;

	short m_styleSaved = CODE_CES;

	void SetUp() override {
		if (!m_wxInit.IsOk())
			GTEST_SKIP() << "wxBase init failed (no wxApp host)";

		// Image handlers and the log target used to be re-armed HERE, per
		// fixture. Both are process facts, so they moved to the one place each
		// belongs: the log target to the global environment in test_compiler.cpp
		// (it covers every backend test, whatever --gtest_filter picks), and the
		// handler re-registration to tests/frontendFix.h, which runs on each wx
		// cycle — the cycle is what wipes them (wxImageModule::OnExit).

		if (ibApplicationData::Get() == nullptr
		 && !ibApplicationData::CreateAppDataEnv(ibRunMode::eRUNTIME_MODE))
			GTEST_SKIP() << "appData env unavailable headless";

		m_styleSaved = ibCompileCode::GetCodeStyle();
		ibCompileCode::SetCodeStyle(CODE_CES);
	}
	void TearDown() override {
		ibCompileCode::SetCodeStyle(m_styleSaved);
	}
};

} // namespace

TEST_F(ScriptCorpus, TheCorpusIsThere)
{
	// A silent empty corpus would turn every case below into a pass that proves
	// nothing — the failure mode a data-driven suite has and a hand-written one
	// does not.
	ASSERT_FALSE(CorpusFiles().empty())
		<< "no scripts found in " << ScriptDir().ToStdString();
}

TEST_F(ScriptCorpus, EveryScriptCompiles)
{
	const std::vector<wxString> listFile = CorpusFiles();
	ASSERT_FALSE(listFile.empty());

	size_t numOk = 0;
	wxString strFailures;

	for (const wxString& strPath : listFile) {

		const wxString strText = ReadWhole(strPath);
		if (strText.IsEmpty()) {
			strFailures << wxT("\n  [unreadable] ") << strPath;
			continue;
		}

		ibCorpusHost host;
		ibCompileCode compiler(wxFileName(strPath).GetName(), wxT("corpus"));

		wxString strError;
		if (CompileEitherDialect(compiler, host, strText, strError)) {
			EXPECT_FALSE(compiler.m_cByteCode.m_listCode.empty())
				<< wxFileName(strPath).GetFullName().ToStdString();
			numOk++;
			continue;
		}

		strFailures << wxT("\n  ") << wxFileName(strPath).GetFullName();
		if (!strError.IsEmpty())
			strFailures << wxT(" — ") << strError;
	}

	EXPECT_EQ(numOk, listFile.size())
		<< numOk << " of " << listFile.size() << " compiled; failed:"
		<< strFailures.ToStdString();
}

// =============================================================================
// RUNNING them — the question "does it work", asked for the first time
//
// Compiling proves the reader understood the text. It proves nothing about the
// bytecode: a query that emits a call with the wrong arity, a binding that was
// silently dropped, a lambda whose frame is one slot short — all compile, and
// all fail the moment a row goes through them. Until this test there was no
// place in the tree where a real module was executed at all.
//
// The binder is not optional. `Message` is a CONTEXT METHOD, so its owning
// context variable is a required binding, and the no-binder Execute overload
// raises "Required binding not provided" before the first opcode. codeRunner
// does exactly what this does: Compile, CreateBinder, Execute.
// =============================================================================
TEST_F(ScriptCorpus, EveryScriptRuns)
{
	const std::vector<wxString> listFile = CorpusFiles();
	ASSERT_FALSE(listFile.empty());

	size_t numRan = 0, numCompiled = 0;
	wxString strFailures;

	for (const wxString& strPath : listFile) {

		const wxString strText = ReadWhole(strPath);
		if (strText.IsEmpty())
			continue;

		ibCorpusHost host;
		ibCompileCode compiler(wxFileName(strPath).GetName(), wxT("corpus"));

		wxString strError;
		if (!CompileEitherDialect(compiler, host, strText, strError))
			continue;   // reported by the case above; not this one's question

		numCompiled++;

		wxString strRunError;
		try {
			ibByteBinder binder = compiler.CreateBinder();
			ibProcUnit   unit;
			unit.Execute(compiler.m_cByteCode, binder);
		} catch (const ibBackendException& err) {
			strRunError = err.GetErrorDescription();
		} catch (...) {
			strRunError = wxT("unknown exception");
		}

		if (!strRunError.IsEmpty()) {
			strFailures << wxT("\n  ") << wxFileName(strPath).GetFullName()
			            << wxT(" — ") << strRunError;
			continue;
		}

		numRan++;

		// A module that ran and said nothing either has no Message in it — one
		// file is deliberately compile-only — or never reached the ones it has.
		// Reported rather than asserted: which of the two it is belongs to the
		// case that checks WHAT was printed, and that case is not written yet.
		if (host.GetMessages().empty())
			std::printf("[silent] %s\n",
				(const char*)wxFileName(strPath).GetFullName().ToUTF8());
	}

	EXPECT_EQ(numRan, numCompiled)
		<< numRan << " of " << numCompiled << " compiled scripts ran; failed:"
		<< strFailures.ToStdString();
}

TEST_F(ScriptCorpus, EveryScriptDeclaresWhatItUses)
{
	// A module that compiles but registers no symbols would mean the tree was
	// read and then thrown away — the failure the m_listVar omission produced,
	// which no other test caught until the binder asked.
	for (const wxString& strPath : CorpusFiles()) {

		const wxString strText = ReadWhole(strPath);
		if (strText.IsEmpty()) continue;

		ibCorpusHost host;
		ibCompileCode compiler(wxFileName(strPath).GetName(), wxT("corpus"));

		wxString strError;
		if (!CompileEitherDialect(compiler, host, strText, strError))
			continue;   // reported by the case above

		const ibByteCode& bc = compiler.m_cByteCode;
		EXPECT_FALSE(bc.m_listVar.empty() && bc.m_listFunc.empty())
			<< wxFileName(strPath).GetFullName().ToStdString()
			<< ": compiled but declares neither a variable nor a function";
	}
}

// =============================================================================
// BISECTING A FAILING SCRIPT — the shortest prefix that stops running
//
// Four hand-built repros of `test_aggregations_selector.txt`'s failure passed in
// isolation: the Message, the selector, the capture, the joined `var k = 3`. So
// the line the error names is not where it happens — the site is taken from the
// last line stamp — and constructing repros from it is guesswork.
//
// This asks the file itself. It runs prefix after prefix and reports the FIRST
// line whose addition stops the module from executing, which is a fact about the
// script rather than a theory about the engine.
//
//   oes_tests --gtest_also_run_disabled_tests --gtest_filter=*BisectFirst*
// =============================================================================
TEST_F(ScriptCorpus, DISABLED_BisectFirstFailingLine)
{
	for (const wxString& strPath : CorpusFiles()) {

		const wxString strWhole = ReadWhole(strPath);
		if (strWhole.IsEmpty()) continue;

		wxArrayString listLine = wxStringTokenize(strWhole, wxT("\n"), wxTOKEN_RET_EMPTY_ALL);

		// Does the WHOLE file run? If so there is nothing to bisect.
		{
			ibCorpusHost host;
			ibCompileCode compiler(wxFileName(strPath).GetName(), wxT("corpus"));
			wxString strError;
			if (!CompileEitherDialect(compiler, host, strWhole, strError))
				continue;   // a compile failure is the other case's business
			try {
				ibByteBinder binder = compiler.CreateBinder();
				ibProcUnit unit;
				unit.Execute(compiler.m_cByteCode, binder);
				continue;   // runs whole — nothing to report
			} catch (...) { /* fall through and bisect */ }
		}

		for (size_t n = 1; n <= listLine.GetCount(); n++) {

			wxString strPrefix;
			for (size_t i = 0; i < n; i++)
				strPrefix << listLine[i] << wxT("\n");

			ibCorpusHost host;
			ibCompileCode compiler(wxFileName(strPath).GetName(), wxT("corpus"));

			wxString strError;
			if (!CompileEitherDialect(compiler, host, strPrefix, strError))
				continue;   // an incomplete prefix often cannot compile; keep going

			bool bRan = false;
			try {
				ibByteBinder binder = compiler.CreateBinder();
				ibProcUnit unit;
				unit.Execute(compiler.m_cByteCode, binder);
				bRan = true;
			} catch (const ibBackendException& err) {
				strError = err.GetErrorDescription();
			} catch (...) {
				strError = wxT("unknown exception");
			}

			if (!bRan) {
				std::printf("%s: first failing prefix is %u lines\n  line %u: %s\n  %s\n",
					(const char*)wxFileName(strPath).GetFullName().ToUTF8(),
					(unsigned)n, (unsigned)n,
					(const char*)listLine[n - 1].ToUTF8(),
					(const char*)strError.ToUTF8());

				// NOW SHRINK IT. Five hand-written reconstructions of this line
				// all ran; the line needs its neighbours and guessing which ones
				// is what kept failing. So the prefix drops one line at a time and
				// KEEPS the drop whenever the failure survives — what is left is
				// the smallest script that still breaks, which is the repro.
				//
				// A drop that stops the module compiling is not a reduction: the
				// line goes back.
				std::vector<bool> listKeep(n, true);

				for (size_t drop = 0; drop + 1 < n; drop++) {

					listKeep[drop] = false;

					wxString strTry;
					for (size_t i = 0; i < n; i++)
						if (listKeep[i]) strTry << listLine[i] << wxT("\n");

					ibCorpusHost hostTry;
					ibCompileCode compilerTry(wxFileName(strPath).GetName(), wxT("corpus"));

					// THREE OUTCOMES, NOT TWO. "Did not compile" is not the same
					// answer as "compiled and ran fine", and lumping them together
					// is how a reduction step silently turns a runtime defect into
					// a syntax error nobody sees. The compile message is READ and
					// reported, never swallowed.
					wxString strWhy;
					bool bStillFails = false;

					if (!CompileEitherDialect(compilerTry, hostTry, strTry, strWhy)) {
						std::printf("  drop line %u -> DOES NOT COMPILE: %s\n",
							(unsigned)(drop + 1), (const char*)strWhy.ToUTF8());
					}
					else {
						try {
							ibByteBinder binder = compilerTry.CreateBinder();
							ibProcUnit unit;
							unit.Execute(compilerTry.m_cByteCode, binder);
						}
						catch (const ibBackendException& err) {
							bStillFails = true; strWhy = err.GetErrorDescription();
						}
						catch (...) {
							bStillFails = true; strWhy = wxT("unknown exception");
						}
					}

					// THE SAME FAILURE, not merely A failure. Accepting any raise
					// is how a reduction walks off onto a different defect: drop
					// the lines that fill an array and the script still throws —
					// from the empty-array path, which is not the bug being
					// reduced. The message is the identity of the failure here,
					// and it is the only one available without a code.
					if (!bStillFails || strWhy != strError)
						listKeep[drop] = true;   // load-bearing, or a different bug
				}

				std::printf("  --- minimal failing script ---\n");
				for (size_t i = 0; i < n; i++)
					if (listKeep[i])
						std::printf("  | %s\n", (const char*)listLine[i].ToUTF8());
				std::printf("  ------------------------------\n");
				break;
			}
		}
	}
	SUCCEED();
}
