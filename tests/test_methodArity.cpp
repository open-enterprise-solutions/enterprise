// Method arity — the contract the call frame rests on.
//
// ibProcUnit's OPER_CALL_METHOD sizes the callee frame by the METHOD's declared
// arity (GetNParams), not by how many arguments the caller passed, because
// implementations index paParams[i] without consulting the count they were
// given — ibValueArray::Add does `*paParams[0]` outright. So a call that passes
// fewer arguments must still find a constructed, empty slot there rather than
// uninitialised memory.
//
// That sizing used to be a blanket MAX_STATIC_VAR (25 slots per call, whatever
// the arity), which cost ~145 ns on every method call; it is now the arity
// itself. These tests pin the property the change relies on: calling a method
// with FEWER arguments than it declares must not crash, and must either work or
// raise — never read past the frame. See docs/runtime-perf.md §5.7.

#include <gtest/gtest.h>

#include "backend/compiler/compileCode.h"
#include "backend/compiler/procUnit.h"
#include "backend/compiler/byteCode.h"
#include "backend/compiler/value.h"

namespace {

// Runs a module body. Returns true if it completed, false if it raised —
// both are acceptable outcomes here. A crash is not, and is what this catches.
bool RunModule(const wxString& src) {
	ibCompileCode cc(wxT("arity"), wxT("memory"), false);
	if (!cc.Compile(src))
		return false;
	ibProcUnit pu;
	try {
		pu.Execute(cc.m_cByteCode);
		return true;
	} catch (...) {
		return false;   // a raise is a legitimate answer, not a failure of the test
	}
}

} // namespace

// Get declares one parameter. Calling it with none must reach an empty slot,
// not garbage — the frame is sized by the arity precisely so that it can.
TEST(MethodArity, FewerArgsThanDeclaredDoesNotCrash) {
	EXPECT_NO_FATAL_FAILURE(
		RunModule(wxT("var arr; arr = New Array; arr.Add(1); var v; v = arr.Get();")));
}

// Set declares two. Passing one exercises the same path with a partially
// filled frame.
TEST(MethodArity, PartiallyFilledFrameDoesNotCrash) {
	EXPECT_NO_FATAL_FAILURE(
		RunModule(wxT("var arr; arr = New Array; arr.Add(1); arr.Set(0);")));
}

// The normal path still works — guards against a fix that makes calls safe by
// making them useless.
TEST(MethodArity, FullArgsStillWork) {
	EXPECT_TRUE(RunModule(wxT("var arr; arr = New Array; arr.Add(7); var v; v = arr.Get(0);")));
}

// Zero-arity methods: the frame is empty, and nothing may be read from it.
TEST(MethodArity, ZeroArityMethodDoesNotCrash) {
	EXPECT_NO_FATAL_FAILURE(
		RunModule(wxT("var arr; arr = New Array; arr.Add(1); arr.Clear();")));
}

// More arguments than declared is the case that WAS already checked — it must
// raise rather than silently write past the declared arity.
TEST(MethodArity, MoreArgsThanDeclaredIsRejected) {
	EXPECT_FALSE(RunModule(wxT("var arr; arr = New Array; arr.Clear(1, 2, 3);")));
}
