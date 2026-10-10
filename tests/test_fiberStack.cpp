// A server session runs on a fiber. The recursion guard has to be the thing
// that stops a script, an expression and a JSON value — a guard page must
// not be. These run on a fiber and require the refusal, not a dead process.

#include <gtest/gtest.h>

#include "backend/backend_exception.h"
#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/query/queryParser.h"
#include "backend/session/session.h"
#include "core/exception.h"
#include "core/fiber/fiber.h"
#include "core/fileSystem/fs.h"
#include "core/serialize/dataBuilder.h"
#include "core/serialize/jsonProvider.h"

#include <functional>
#include <string>
#include <vector>

namespace {

struct ibFiberRun {
	std::function<void()> fn;
};

void FiberEntry(void* arg)
{
	static_cast<ibFiberRun*>(arg)->fn();
}

// Runs `fn` on a fresh fiber and rethrows whatever escaped it. The fiber's
// own entry stores that exception; it does not cross the switch.
void RunOnFiber(const std::function<void()>& fn, std::size_t reserve = ibFiber::kStackReserve)
{
	ibFiber::ConvertThread();
	struct Release {
		~Release() { ibFiber::ReleaseThread(); }
	} release;

	ibFiberRun call;
	call.fn = fn;
	ibFiber* fiber = ibFiber::Create(&FiberEntry, &call, reserve);
	ibFiber::Scheduler()->SwitchTo(fiber);
	const std::exception_ptr escaped = fiber->TakeException();
	const bool finished = fiber->Finished();
	ibFiber::Destroy(fiber);
	if (!finished)
		throw std::runtime_error("fiber returned without finishing");
	if (escaped)
		std::rethrow_exception(escaped);
}

wxString Refusal(const std::function<void()>& fn, std::size_t reserve = ibFiber::kStackReserve)
{
	try {
		RunOnFiber(fn, reserve);
	}
	catch (const ibBackendException& err) {
		return err.GetErrorDescription();
	}
	catch (const ibCoreException& err) {
		return err.GetErrorDescription();
	}
	return wxString();
}

void RunDepth(int n, int& out)
{
	ibCompileCode cc(wxT("test"), wxT("memory"), false);
	const wxString src =
		wxT("Function Depth(n) Public\n")
		wxT("  If n <= 0 Then\n")
		wxT("    Return 0;\n")
		wxT("  EndIf;\n")
		wxT("  Return Depth(n - 1) + 1;\n")
		wxT("EndFunction\n");
	if (!cc.Compile(src))
		throw std::runtime_error("Depth did not compile");
	ibProcUnit pu;
	pu.Execute(cc.m_cByteCode);
	ibValue ret;
	ibValue arg(n);
	pu.CallAsFunc(wxT("Depth"), ret, arg);
	out = static_cast<int>(ret.GetInteger());
}

wxString Parens(int n)
{
	wxString expr = wxT("1");
	for (int i = 0; i < n; ++i)
		expr = wxT("(") + expr + wxT(")");
	return wxT("Function Nest() Public\n  Return ") + expr + wxT(";\nEndFunction\n");
}

std::string NestedObjects(int depth)
{
	std::string open;
	std::string close;
	open.reserve(static_cast<std::size_t>(depth) * 5);
	close.reserve(static_cast<std::size_t>(depth));
	for (int i = 0; i < depth; ++i) {
		open += "{\"a\":";
		close += '}';
	}
	return open + '0' + close;
}

void ReadJson(const std::string& json)
{
	std::vector<char> bytes(json.begin(), json.end());
	ibReaderMemory reader(bytes.data(), static_cast<int>(bytes.size()));
	ibDataNode root;
	ibJsonProvider provider;
	if (!provider.Read(reader, root))
		throw std::runtime_error("JSON read returned false");
}

} // namespace

TEST(FiberStack, StackRemaining_OnAFiber_IsInsideTheReserve)
{
	std::size_t left = 0;
	RunOnFiber([&] { left = ibFiber::StackRemaining(); });
	EXPECT_GT(left, ibFiber::kRecursionSlack);
	EXPECT_LT(left, ibFiber::kStackReserve);
}

// 200 is inside MAX_REC_COUNT. Depth(200) is 201 frames and the guard
// refuses on the frame after that, so this is the deepest call the
// counter still allows. It has to return: a stack check that fired
// earlier would be a different limit than the one a script sees.
TEST(FiberStack, Recursion_UnderTheGuard_Returns)
{
	int got = -1;
	RunOnFiber([&] { RunDepth(200, got); });
	EXPECT_EQ(got, 200);
}

void ExpectCleanInterpreter()
{
	// The refusal unwound. A catch that swallowed the overflow would
	// leave the depth and a run context pointing at a frame that is gone.
	ibProcUnitState* const state = ibSession::PUStateOf(ibSession::Current());
	ASSERT_NE(state, nullptr);
	EXPECT_EQ(state->m_recCount, 0);
	EXPECT_EQ(state->GetCountRunContext(), 0u);
}

TEST(FiberStack, Recursion_PastTheGuard_Raises)
{
	const wxString why = Refusal([&] {
		int ignored = 0;
		RunDepth(400, ignored);
	});
	EXPECT_NE(why.Find(wxT("recursive")), wxNOT_FOUND) << why.ToStdString();
	ExpectCleanInterpreter();
}

// A reserve that cannot hold 200 frames used to die on the guard page.
// The same refusal, reached early, is what a tight fiber must do.
TEST(FiberStack, Recursion_OnATightReserve_Raises)
{
	// Below the depth counter. A 1 MB reserve still reaches the count of 200
	// in Release before the stack check, so the refusal then says "recursive"
	// and proves nothing about StackLow. 384 KB leaves about 256 KB under
	// the 128 KB slack — a few frames, not 200.
	const std::size_t tight = 384u * 1024u;
	int shallow = -1;
	RunOnFiber([&] { RunDepth(4, shallow); }, tight);
	EXPECT_EQ(shallow, 4);

	const wxString why = Refusal([&] {
		int ignored = 0;
		RunDepth(400, ignored);
	}, tight);
	EXPECT_NE(why.Find(wxT("native stack")), wxNOT_FOUND) << why.ToStdString();
	ExpectCleanInterpreter();
}

TEST(FiberStack, AQueryNestedPastTheStack_Raises)
{
	wxString expr = wxT("1");
	for (int i = 0; i < 400; ++i)
		expr = wxT("(") + expr + wxT(")");
	const wxString query = wxT("SELECT ") + expr;
	const wxString why = Refusal([&] {
		ibQueryParser parser;
		parser.Parse(query);
	}, 256u * 1024u);
	EXPECT_NE(why.Find(wxT("stack")), wxNOT_FOUND) << why.ToStdString();
}

TEST(FiberStack, ABinaryValueNestedPastTheStack_Raises)
{
	ibDataNode root;
	ibDataNode* cursor = &root;
	for (int i = 0; i < 400; ++i)
		cursor = &cursor->AddChild(1, i);
	ibWriterMemory writer;
	ASSERT_TRUE(ibBinaryProvider().Write(root, writer));
	const wxMemoryBuffer blob = writer.buffer();
	const wxString why = Refusal([&] {
		ibDataNode read;
		ibReaderMemory reader(blob);
		ibBinaryProvider().Read(reader, read);
	}, 256u * 1024u);
	EXPECT_NE(why.Find(wxT("stack")), wxNOT_FOUND) << why.ToStdString();
}

// 399 parentheses sit on the ceiling (the check is depth > 400, and the
// return's own frame is the extra one). It has to compile.
TEST(FiberStack, Expression_UnderTheCeiling_Compiles)
{
	bool compiled = false;
	RunOnFiber([&] {
		ibCompileCode cc(wxT("test"), wxT("memory"), false);
		compiled = cc.Compile(Parens(399));
	});
	EXPECT_TRUE(compiled);
}

TEST(FiberStack, Expression_PastTheCeiling_Raises)
{
	const wxString why = Refusal([&] {
		ibCompileCode cc(wxT("test"), wxT("memory"), false);
		cc.Compile(Parens(450));
	});
	EXPECT_NE(why.Find(wxT("nested")), wxNOT_FOUND) << why.ToStdString();
}

TEST(FiberStack, Json_ShallowOnAFiber_Reads)
{
	bool read = false;
	RunOnFiber([&] {
		ReadJson(NestedObjects(32));
		read = true;
	});
	EXPECT_TRUE(read);
}

TEST(FiberStack, Json_DeeperThanTheStack_Raises)
{
	// The depth count refuses first and Read returns false. A reserve that
	// runs out before the count still raises. Either answer is the refusal.
	bool refused = false;
	wxString why;
	try {
		RunOnFiber([&] {
			const std::string json = NestedObjects(100000);
			std::vector<char> bytes(json.begin(), json.end());
			ibReaderMemory reader(bytes.data(), static_cast<int>(bytes.size()));
			ibDataNode root;
			ibJsonProvider provider;
			if (!provider.Read(reader, root))
				refused = true;
		}, 256u * 1024u);
	}
	catch (const ibBackendException& err) {
		why = err.GetErrorDescription();
	}
	catch (const ibCoreException& err) {
		why = err.GetErrorDescription();
	}
	if (why.Find(wxT("nested deeper than the stack")) != wxNOT_FOUND)
		refused = true;
	EXPECT_TRUE(refused) << why.ToStdString();
}
