////////////////////////////////////////////////////////////////////////////
//	Author		: Maxim Kornienko
//	Description : main app
////////////////////////////////////////////////////////////////////////////

#include "mainApp.h"
#include "backend/appData.h"
#include "backend/backend_exception.h"   // DrainLastErrors for the startup-failure dialog
#include "backend/backend_mainFrame.h"
#include "backend/debugger/debugClientBridge.h"
#include "frontend/session/guiSession.h"   // transitively pulls backend/session/session.h
#include "backend/session/sessionRegistry.h"
#include "mainFrame/debugger/debugClientImpl.h"

#include <wx/clipbrd.h>
#include <wx/cmdline.h>
#include <wx/fs_arc.h>
#include <wx/fs_filter.h>
#include <wx/fs_mem.h>
#include <wx/stdpaths.h>

#ifdef __WXMSW__
#include <windows.h>   // DisableProcessWindowsGhosting
#include "backend/system/value/valueOLE.h"   // ibValueOLE::ReleaseComObjects in normal OnExit
#endif

#include "resources/splashLogo.xpm"

#if wxVERSION_NUMBER >= 2905 && wxVERSION_NUMBER <= 3100
#include <wx/xrc/xh_auinotbk.h>
#elif wxVERSION_NUMBER > 3100
#include <wx/xrc/xh_aui.h>
#endif

#if defined(DEBUG) && defined(__WXMSW__)
#include <crtdbg.h>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <dbghelp.h>
#include <psapi.h>
#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "psapi.lib")

//////////////////////////////////////////////////////////////////////////////
//                       Leak attribution (Debug / MSW)                     //
//////////////////////////////////////////////////////////////////////////////
//
// The "Detected memory leaks!" dump wxWidgets prints on exit names every surviving block by
// size and by an allocation ordinal:
//
//     {4429052} normal block at 0x09D87E00, 116 bytes long.
//
// It never says WHO allocated it, which is the only thing worth knowing, and the ordinal is
// worthless across runs — it only lines up if the process allocates identically twice. Two
// tools live here. Both are Debug/MSW-only and both stay dormant unless an environment
// variable turns them on, so an ordinary debug run pays nothing at all.
//
//     set OES_BREAK_ALLOC=4429052    break in the debugger ON that allocation (one run only)
//     set OES_TRACK_SIZE=116,32,64   record a call stack for every allocation of these sizes
//     set OES_TRACK_ALL=1            same for every size — complete, and much slower
//     set OES_TRACK_TAIL=1           print a stack for every allocation made AFTER the report,
//                                    i.e. during CRT teardown, where no other mode can reach
//
// OES_TRACK_SIZE is the one that answers the question. Sizes come straight off the CRT dump
// ("116 bytes long"), the tracker keeps a stack for each such allocation, drops it again when
// the block is freed, and prints whatever is left over — grouped by call site, biggest first.
// No ordinals, no second run, no matching by hand.

namespace {

constexpr unsigned int kLeakMaxFrames = 32;   // ~10 go to allocator/STL boilerplate, the rest is ours
constexpr unsigned int kLeakSkipFrames = 2;   // the hook itself and CaptureStackBackTrace
constexpr unsigned int kLeakMinSlots = 1024;
// Call sites the report can hold. Sized to not truncate in practice rather than to look tidy:
// at 256 it dropped ~480 blocks per run, and the blocks that survive to the CRT dump are exactly
// the rare ones that lose the race for a slot. The table is a function-local static (a diagnostic
// that runs once, at exit, on one thread), so this costs bss, not stack.
constexpr unsigned int kLeakMaxGroups = 4096;
// Per group, so a big group cannot bury its stack. Sized to survive cross-referencing: the dump's
// ordinals are looked up here, and a group that prints only its first few hides any survivor that
// happens to sit further down its list.
constexpr unsigned int kLeakMaxOrdinals = 96;

// Which groups get a symbolised stack — see the note at the print loop.
constexpr unsigned int kLeakStackBigCount = 20;    // ≥ this many blocks: a candidate for growth
constexpr unsigned int kLeakStackRareCount = 8;    // ≤ this many: the shape that survives to exit

struct ibLeakRecord {
	long         m_request;                  // 0 = free slot, -1 = tombstone
	unsigned int m_frames;
	void*        m_stack[kLeakMaxFrames];
};

// Bookkeeping lives on a PRIVATE Win32 heap, never on the CRT heap. Two reasons, both fatal
// otherwise: records must not appear in the very leak dump they exist to explain, and they
// must not re-enter the allocation hook that writes them.
HANDLE        g_leakHeap = nullptr;
ibLeakRecord* g_leakTable = nullptr;
unsigned int  g_leakMask = 0;                // capacity - 1; capacity is always a power of two
unsigned int  g_leakUsed = 0;                // occupied slots, tombstones included
size_t        g_leakSizes[8] = {};
unsigned int  g_leakSizeCount = 0;
bool          g_leakTrackAll = false;
bool          g_leakEnabled = false;
// Tail mode. Blocks allocated AFTER the report cannot be caught by any of the mechanisms above:
// the table is already printed, and they are invisible to OES_TRACK_ORDINAL because asking by
// ordinal needs a previous run to read the number from — and the ask itself moves the number.
// The answer is to stop being clever and print each one as it happens.
bool          g_leakTail = false;         // requested
bool          g_leakTailLive = false;     // report is out; print from here on
unsigned long g_leakRecorded = 0;
unsigned long g_leakReleased = 0;
// Every way a free can fail to reach the table gets its own counter. Two rounds were lost to
// guessing which step was silently swallowing everything; the report now says which one.
// Ordinals asked for by name (OES_TRACK_ORDINAL). The CRT dump identifies a surviving block by
// its ordinal and nothing else, so this is the direct question: "who allocated {423758}?".
// Answering it skips grouping entirely — no cap, no ranking, no chance of the one block that
// matters falling off the end of a list.
long          g_leakWanted[32] = {};
unsigned int  g_leakWantedCount = 0;

unsigned long g_leakFreeSeen = 0;            // _HOOK_FREE invocations
unsigned long g_leakFreeNoRequest = 0;       // …of those: the block carried request number 0
unsigned long g_leakFreeUnknown = 0;         // …a good number, but nothing of ours matched it

// Reading the CRT's own block header is the only way to learn WHICH block is being freed. The
// request number the hook receives on _HOOK_FREE is not the one it received on _HOOK_ALLOC, and
// the size is not passed on free at all — measured 2026-07-30, both cost a round.
//
// So the header is not declared, it is MEASURED. Three allocations in a row carry three
// consecutive request numbers, which makes the slot identifiable with no knowledge of the
// layout, the bitness or the toolset: it is the one that reads n, n+1, n+2. Anything hard-coded
// here would just be the previous guess wearing a different hat.
int g_leakRequestOffset = 0;   // byte offset from the user pointer, negative; 0 = not derived

bool ibLeakDeriveRequestOffset()
{
	void* const probe[3] = { std::malloc(64), std::malloc(64), std::malloc(64) };
	bool derived = false;

	if (probe[0] != nullptr && probe[1] != nullptr && probe[2] != nullptr) {
		for (int offset = -48; offset <= -4 && !derived; offset += 4) {
			long value[3] = {};
			for (int i = 0; i < 3; ++i)
				std::memcpy(&value[i], static_cast<const unsigned char*>(probe[i]) + offset,
					sizeof(value[i]));

			if (value[0] > 0 && value[1] == value[0] + 1 && value[2] == value[0] + 2) {
				g_leakRequestOffset = offset;
				derived = true;
			}
		}
	}

	for (int i = 2; i >= 0; --i)
		std::free(probe[i]);

	return derived;
}

void ibLeakInsert(long request, void* const* stack, unsigned int frames);

unsigned int ibLeakSlot(long request) {
	return (static_cast<unsigned int>(request) * 2654435761u) & g_leakMask;
}

// Rehash into a table sized off the LIVE count, not the old capacity: the churn here is
// tombstones, not growth, so a compaction into the same size is the common outcome and the
// table stays bounded even under OES_TRACK_ALL.
bool ibLeakRehash()
{
	unsigned int live = 0;
	const unsigned int oldCapacity = g_leakTable != nullptr ? g_leakMask + 1 : 0;
	for (unsigned int i = 0; i < oldCapacity; ++i)
		if (g_leakTable[i].m_request > 0) ++live;

	unsigned int capacity = kLeakMinSlots;
	while (capacity < live * 4) capacity *= 2;

	ibLeakRecord* fresh = static_cast<ibLeakRecord*>(
		HeapAlloc(g_leakHeap, HEAP_ZERO_MEMORY, sizeof(ibLeakRecord) * capacity));
	if (fresh == nullptr)
		return false;

	ibLeakRecord* old = g_leakTable;
	g_leakTable = fresh;
	g_leakMask = capacity - 1;
	g_leakUsed = 0;

	for (unsigned int i = 0; i < oldCapacity; ++i)
		if (old[i].m_request > 0)
			ibLeakInsert(old[i].m_request, old[i].m_stack, old[i].m_frames);

	if (old != nullptr)
		HeapFree(g_leakHeap, 0, old);
	return true;
}

void ibLeakInsert(long request, void* const* stack, unsigned int frames)
{
	if (g_leakTable == nullptr)
		return;
	if (g_leakUsed * 4 >= (g_leakMask + 1) * 3 && !ibLeakRehash())
		return;                              // out of room: stop recording, never crash

	unsigned int slot = ibLeakSlot(request);
	for (;;) {
		ibLeakRecord& record = g_leakTable[slot];
		if (record.m_request <= 0) {         // free slot or tombstone
			if (record.m_request == 0) ++g_leakUsed;
			record.m_request = request;
			record.m_frames = frames;
			for (unsigned int i = 0; i < frames; ++i)
				record.m_stack[i] = stack[i];
			return;
		}
		if (record.m_request == request)     // ordinal reused: keep the first stack
			return;
		slot = (slot + 1) & g_leakMask;
	}
}

bool ibLeakErase(long request)
{
	if (g_leakTable == nullptr)
		return false;
	unsigned int slot = ibLeakSlot(request);
	for (unsigned int probe = 0; probe <= g_leakMask; ++probe) {
		ibLeakRecord& record = g_leakTable[slot];
		if (record.m_request == 0)
			return false;                    // free slot ends the probe chain: not tracked
		if (record.m_request == request) {
			record.m_request = -1;
			++g_leakReleased;
			return true;
		}
		slot = (slot + 1) & g_leakMask;
	}
	return false;
}

bool ibLeakWatched(size_t size)
{
	if (g_leakTrackAll)
		return true;
	for (unsigned int i = 0; i < g_leakSizeCount; ++i)
		if (g_leakSizes[i] == size)
			return true;
	return false;
}

// The block being freed identifies itself through its own header, at the offset derived above.
long ibLeakRequestOnFree(const void* userData)
{
	if (g_leakRequestOffset == 0 || userData == nullptr)
		return 0;

	// Request 0 is legitimate — the earliest CRT blocks carry it. It just means "not one of
	// the ones we recorded", which is a miss, not a fault.
	long request = 0;
	std::memcpy(&request, static_cast<const unsigned char*>(userData) + g_leakRequestOffset,
		sizeof(request));
	if (request <= 0)
		++g_leakFreeNoRequest;
	return request;
}

bool ibLeakFormatFrame(void* address, char* text, size_t textSize);

// Tail mode's whole body: symbolise and print one allocation on the spot. Called from inside the
// hook, under its re-entry guard, so the dbghelp allocations it makes cannot come back round.
void ibLeakTailPrint(long request, size_t size)
{
	void* stack[kLeakMaxFrames];
	const USHORT frames = CaptureStackBackTrace(kLeakSkipFrames, kLeakMaxFrames, stack, nullptr);

	char line[1024];
	_snprintf_s(line, _TRUNCATE, "[leak-track tail] {%ld} %zu byte(s)\n", request, size);
	OutputDebugStringA(line);

	// RAW ADDRESSES ONLY — no dbghelp here, deliberately. Symbolising at this point kills the
	// process: dbghelp runs against a CRT that exit() has already begun to dismantle, and it
	// reaches abort() (measured three times, 2026-07-30 — c0000409, and __fastfail bypasses SEH,
	// so there is nothing to catch and no second chance to print). The names are recoverable
	// afterwards from the module table above; the run is not recoverable at all. So the tail
	// records, and something that is still alive does the naming.
	for (USHORT frame = 0; frame < frames; ++frame) {
		_snprintf_s(line, _TRUNCATE, frame + 1 == frames ? "%p\n" : "%p ", stack[frame]);
		OutputDebugStringA(line);
	}
}

// realloc is deliberately ignored: it never fires for C++ objects, which allocate through
// operator new.
int __cdecl ibLeakAllocHook(int allocType, void* userData, size_t size, int blockType,
	long request, const unsigned char* /*fileName*/, int /*line*/)
{
	// _CRT_BLOCK is the CRT's own bookkeeping — hooking it is how you get infinite recursion.
	if (!g_leakEnabled || blockType == _CRT_BLOCK)
		return TRUE;

	static thread_local bool inHook = false;
	if (inHook)
		return TRUE;
	inHook = true;

	// No size filter given means "print everything" — during teardown that is a handful of blocks,
	// and the point of the mode is to catch what nothing else can see.
	if (allocType == _HOOK_ALLOC && g_leakTailLive &&
		((g_leakSizeCount == 0 && !g_leakTrackAll) || ibLeakWatched(size)))
		ibLeakTailPrint(request, size);

	if (allocType == _HOOK_ALLOC && ibLeakWatched(size)) {
		void* stack[kLeakMaxFrames];
		const USHORT frames = CaptureStackBackTrace(kLeakSkipFrames, kLeakMaxFrames, stack, nullptr);
		ibLeakInsert(request, stack, frames);
		++g_leakRecorded;
	}
	else if (allocType == _HOOK_FREE) {
		++g_leakFreeSeen;
		const long freed = ibLeakRequestOnFree(userData);
		if (freed > 0 && !ibLeakErase(freed))
			++g_leakFreeUnknown;
	}

	inHook = false;
	return TRUE;
}

// Identity of a call site. Two records with the same frames are the same leak, printed once.
unsigned long long ibLeakStackHash(const ibLeakRecord& record)
{
	unsigned long long hash = 1469598103934665603ull;   // FNV-1a over the frame addresses
	for (unsigned int frame = 0; frame < record.m_frames; ++frame) {
		hash ^= reinterpret_cast<uintptr_t>(record.m_stack[frame]);
		hash *= 1099511628211ull;
	}
	return hash;
}

// A frame belongs to the toolchain, not to us: the CRT allocator entry points and everything
// under the MSVC headers. Every stack starts with ten of these (malloc → operator new → the
// std::vector / std::basic_string internals), which is exactly the run that buried the useful
// line in the first report.
bool ibLeakIsToolchainFrame(const char* symbolName, const char* fileName)
{
	if (fileName != nullptr &&
		(std::strstr(fileName, "\\VC\\Tools\\MSVC\\") != nullptr ||
		 std::strstr(fileName, "\\vctools\\crt\\") != nullptr))
		return true;

	return symbolName != nullptr &&
		(std::strcmp(symbolName, "malloc") == 0 ||
		 std::strcmp(symbolName, "calloc") == 0 ||
		 std::strcmp(symbolName, "malloc_dbg") == 0 ||
		 std::strcmp(symbolName, "calloc_base") == 0 ||
		 std::strcmp(symbolName, "malloc_base") == 0 ||
		 std::strcmp(symbolName, "_malloc_dbg") == 0);
}

// Formats one frame. Returns false while the frame is still toolchain boilerplate, so the
// caller can drop the leading run and start the stack at the first line that is ours.
bool ibLeakFormatFrame(void* address, char* text, size_t textSize)
{
	alignas(8) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
	SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
	symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
	symbol->MaxNameLen = MAX_SYM_NAME;

	const HANDLE process = GetCurrentProcess();
	const DWORD64 value = reinterpret_cast<DWORD64>(address);

	DWORD64 symbolOffset = 0;
	if (!SymFromAddr(process, value, &symbolOffset, symbol)) {
		_snprintf_s(text, textSize, _TRUNCATE, "        0x%p\n", address);
		return true;
	}

	IMAGEHLP_LINE64 lineInfo = {};
	lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
	DWORD lineOffset = 0;
	const bool haveLine = SymGetLineFromAddr64(process, value, &lineOffset, &lineInfo) != FALSE;

	if (haveLine)
		_snprintf_s(text, textSize, _TRUNCATE, "        %s  (%s:%lu)\n",
			symbol->Name, lineInfo.FileName, lineInfo.LineNumber);
	else
		_snprintf_s(text, textSize, _TRUNCATE, "        %s\n", symbol->Name);

	return !ibLeakIsToolchainFrame(symbol->Name, haveLine ? lineInfo.FileName : nullptr);
}

} // namespace

// Prints what the CRT dump cannot: the call site behind every surviving block of a watched
// size. Called from OnExit once the application data is gone, so anything still held here has
// outlived the whole metadata tree and every session.
void ibLeakTrackReport()
{
	if (!g_leakEnabled || g_leakTable == nullptr)
		return;

	// Armed before the report rather than after it, because every branch below has its own exit
	// (including "nothing survived", which returns before symbols are even loaded) and the tail
	// must outlive all of them. Symbols stay loaded for the same reason — the SymCleanup calls
	// below are skipped while the tail is live.
	if (g_leakTail) {
		OutputDebugStringA("\n=== leak-track: tail armed - every allocation from here is printed ===\n");

		// The module table, printed while printing is still safe. Tail lines carry raw frame
		// addresses and nothing else, so this table is what turns them back into names — subtract
		// the base, then resolve the offset against the .pdb offline.
		HMODULE modules[512];
		DWORD needed = 0;
		if (EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed)) {
			const DWORD count = needed / sizeof(HMODULE);
			for (DWORD m = 0; m < count && m < 512; ++m) {
				MODULEINFO info = {};
				char name[MAX_PATH] = {};
				if (GetModuleInformation(GetCurrentProcess(), modules[m], &info, sizeof(info)) &&
					GetModuleBaseNameA(GetCurrentProcess(), modules[m], name, MAX_PATH)) {
					char text[MAX_PATH + 64];
					_snprintf_s(text, _TRUNCATE, "    module %p..%p  %s\n", info.lpBaseOfDll,
						static_cast<unsigned char*>(info.lpBaseOfDll) + info.SizeOfImage, name);
					OutputDebugStringA(text);
				}
			}
		}

		g_leakTailLive = true;
	}

	// Asked for specific blocks? Then answer exactly that and nothing else. No grouping, no
	// ranking, no cap — the whole point is that the block in question is rare, and every one of
	// those mechanisms is a way for a rare block to vanish from the report.
	if (g_leakWantedCount > 0) {
		SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
		SymInitialize(GetCurrentProcess(), nullptr, TRUE);

		char line[1024];
		for (unsigned int w = 0; w < g_leakWantedCount; ++w) {
			const ibLeakRecord* found = nullptr;
			for (unsigned int i = 0; i <= g_leakMask && found == nullptr; ++i)
				if (g_leakTable[i].m_request == g_leakWanted[w])
					found = &g_leakTable[i];

			if (found == nullptr) {
				_snprintf_s(line, _TRUNCATE,
					"\n=== leak-track {%ld}: not recorded (freed, untracked size, or allocated"
					" before the hook was armed) ===\n", g_leakWanted[w]);
				OutputDebugStringA(line);
				continue;
			}

			_snprintf_s(line, _TRUNCATE, "\n=== leak-track {%ld} ===\n", g_leakWanted[w]);
			OutputDebugStringA(line);

			bool ours = false;
			for (unsigned int frame = 0; frame < found->m_frames; ++frame) {
				const bool isOurs = ibLeakFormatFrame(found->m_stack[frame], line, sizeof(line));
				ours = ours || isOurs;
				if (ours)
					OutputDebugStringA(line);
			}
		}

		if (!g_leakTail)
			SymCleanup(GetCurrentProcess());
		OutputDebugStringA("=== leak-track: end ===\n");
		return;
	}

	// Group identical stacks — a leak is a call site repeated N times, and printing N copies of
	// the same twenty frames buries exactly the fact that matters.
	struct ibLeakGroup { unsigned long long m_hash; unsigned int m_count; const ibLeakRecord* m_sample; };
	static ibLeakGroup groups[kLeakMaxGroups] = {};
	unsigned int groupCount = 0;
	unsigned int aliveCount = 0;
	unsigned int droppedBlocks = 0;   // blocks whose call site did not fit the group table

	for (unsigned int i = 0; i <= g_leakMask; ++i) {
		const ibLeakRecord& record = g_leakTable[i];
		if (record.m_request <= 0)
			continue;
		++aliveCount;

		const unsigned long long hash = ibLeakStackHash(record);

		unsigned int group = 0;
		while (group < groupCount && groups[group].m_hash != hash) ++group;
		if (group == groupCount) {
			if (groupCount == kLeakMaxGroups) {
				++droppedBlocks;                            // more shapes than we can hold
				continue;
			}
			groups[groupCount++] = { hash, 0, &record };
		}
		++groups[group].m_count;
	}

	// `released` next to `recorded` is the self-check: if free tracking is broken, every
	// allocation looks like a leak and the whole report is noise. The breakdown underneath says
	// WHICH step swallowed the frees, so a bad run costs one line instead of another round of
	// guessing.
	char text[512];
	_snprintf_s(text, _TRUNCATE,
		"\n=== leak-track: %u block(s) alive in %u call site(s) "
		"(recorded %lu, released %lu%s) ===\n",
		aliveCount, groupCount, g_leakRecorded, g_leakReleased,
		g_leakRequestOffset != 0 ? "" : ", REQUEST OFFSET NOT DERIVED");
	OutputDebugStringA(text);

	// Never let the group cap truncate in silence: a report that looks complete and is not is
	// worse than no report. If this is non-zero the listing below is partial — ask for the exact
	// blocks by ordinal (OES_TRACK_ORDINAL) instead of reading the ranking.
	if (droppedBlocks > 0) {
		_snprintf_s(text, _TRUNCATE,
			"    PARTIAL: %u block(s) not listed - more than %u distinct call sites;"
			" use OES_TRACK_ORDINAL=<n,n,...> to ask for specific blocks\n",
			droppedBlocks, kLeakMaxGroups);
		OutputDebugStringA(text);
	}

	_snprintf_s(text, _TRUNCATE,
		"    request offset %d; frees seen %lu: no request %lu, not ours %lu, matched %lu\n",
		g_leakRequestOffset, g_leakFreeSeen,
		g_leakFreeNoRequest, g_leakFreeUnknown, g_leakReleased);
	OutputDebugStringA(text);

	if (aliveCount == 0) {
		OutputDebugStringA("=== leak-track: nothing survived - clean teardown ===\n");
		return;
	}

	// Symbols only now: dbghelp enumerates the modules that are loaded when it is initialised,
	// and backend.dll / frontend.dll arrive long after static init.
	SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
	SymInitialize(GetCurrentProcess(), nullptr, TRUE);

	// Biggest site first — selection sort over at most kLeakMaxGroups entries.
	for (unsigned int i = 0; i < groupCount; ++i) {
		unsigned int best = i;
		for (unsigned int j = i + 1; j < groupCount; ++j)
			if (groups[j].m_count > groups[best].m_count) best = j;
		if (best != i) {
			const ibLeakGroup swap = groups[i];
			groups[i] = groups[best];
			groups[best] = swap;
		}

		// Every ordinal, not just the first. The CRT dump identifies a surviving block by its
		// ordinal and nothing else, and this report lists far more than survives (it prints
		// before window teardown) — so one ordinal per group leaves no way to ask "which group
		// holds {423758}?". With them all here it is a text search. Capped so a 1500-block
		// group does not bury the stack that follows it.
		_snprintf_s(text, _TRUNCATE, "  [%u]  %u block(s), ordinals:", i + 1, groups[i].m_count);
		OutputDebugStringA(text);

		unsigned int printed = 0;
		for (unsigned int slot = 0; slot <= g_leakMask && printed < kLeakMaxOrdinals; ++slot) {
			const ibLeakRecord& record = g_leakTable[slot];
			if (record.m_request <= 0 || ibLeakStackHash(record) != groups[i].m_hash)
				continue;
			_snprintf_s(text, _TRUNCATE, " {%ld}", record.m_request);
			OutputDebugStringA(text);
			++printed;
		}
		OutputDebugStringA(printed < groups[i].m_count ? " ...\n" : "\n");

		// Stacks for the two ends only. Symbolising every one of a few thousand groups costs a
		// dbghelp lookup per frame and would stretch exit into minutes for output nobody reads.
		// The ends are where the answers are: the big groups carry growth, and the rare ones are
		// what survives to the CRT dump. The middle keeps its header, so its ordinals still
		// cross-reference and its stack is one narrowed run away.
		const bool bigEnough = groups[i].m_count >= kLeakStackBigCount;
		const bool rareEnough = groups[i].m_count <= kLeakStackRareCount;
		if (!bigEnough && !rareEnough)
			continue;

		char frameText[1024];
		bool ours = false;   // drop the leading run of allocator / STL-header frames
		for (unsigned int frame = 0; frame < groups[i].m_sample->m_frames; ++frame) {
			const bool isOurs = ibLeakFormatFrame(
				groups[i].m_sample->m_stack[frame], frameText, sizeof(frameText));
			ours = ours || isOurs;
			if (ours)
				OutputDebugStringA(frameText);
		}
	}

	if (!g_leakTail)
		SymCleanup(GetCurrentProcess());
	OutputDebugStringA("=== leak-track: end ===\n");
}

// Static init on purpose: both hooks have to be armed before wxWidgets allocates anything,
// which rules out OnInit.
static struct ibLeakTrackArm {
	ibLeakTrackArm() {
		if (const char* env = std::getenv("OES_BREAK_ALLOC")) {
			const long alloc = std::strtol(env, nullptr, 10);
			if (alloc > 0)
				_CrtSetBreakAlloc(alloc);
		}

		if (const char* wanted = std::getenv("OES_TRACK_ORDINAL")) {
			for (const char* cursor = wanted; *cursor != '\0' && g_leakWantedCount < 32; ) {
				char* end = nullptr;
				const long ordinal = std::strtol(cursor, &end, 10);
				if (end == cursor)
					break;
				if (ordinal > 0)
					g_leakWanted[g_leakWantedCount++] = ordinal;
				cursor = (*end == ',') ? end + 1 : end;
			}
		}

		const char* trackAll = std::getenv("OES_TRACK_ALL");
		g_leakTrackAll = trackAll != nullptr && *trackAll != '\0' && *trackAll != '0';

		const char* trackTail = std::getenv("OES_TRACK_TAIL");
		g_leakTail = trackTail != nullptr && *trackTail != '\0' && *trackTail != '0';

		if (const char* sizes = std::getenv("OES_TRACK_SIZE")) {
			for (const char* cursor = sizes; *cursor != '\0' && g_leakSizeCount < 8; ) {
				char* end = nullptr;
				const unsigned long size = std::strtoul(cursor, &end, 10);
				if (end == cursor)
					break;
				if (size > 0)
					g_leakSizes[g_leakSizeCount++] = static_cast<size_t>(size);
				cursor = (*end == ',') ? end + 1 : end;
			}
		}

		if (!g_leakTrackAll && g_leakSizeCount == 0 && !g_leakTail)
			return;                          // nothing asked for: stay completely out of the way

		// Measure the header BEFORE the hook goes in, so the probe allocations do not walk
		// through it. Without the offset the free side is blind and every allocation would be
		// reported as a leak — better to say so up front than to hand over a plausible lie.
		if (!ibLeakDeriveRequestOffset())
			OutputDebugStringA("[leak-track] request offset not derived - frees cannot be matched\n");

		g_leakHeap = HeapCreate(0, 0, 0);
		if (g_leakHeap == nullptr || !ibLeakRehash())
			return;

		g_leakEnabled = true;
		_CrtSetAllocHook(ibLeakAllocHook);

		// Report from atexit, not from OnExit. Every atexit handler runs after main returns —
		// so after wxEntry has torn down the windows and cleaned up wx's own caches. Reporting
		// from OnExit put the print BEFORE all of that and inflated "alive" by three orders of
		// magnitude (4618 against the dump's 2273, later 17820 against 30), which left the one
		// question that matters — who allocated the blocks that actually survived — answerable
		// only by matching ordinals between two runs. That does not work: the ordinal is a
		// count of allocations since start, and setting an environment variable to ask about it
		// changes the environment block the CRT copies at start, which moves the very numbers
		// being asked about. Printing at the same moment as the CRT dump removes the question.
		extern void ibLeakTrackReport();
		std::atexit(&ibLeakTrackReport);
	}
} s_leakTrackArm;
#endif

wxIMPLEMENT_APP(ibAppDesigner);

//////////////////////////////////////////////////////////////////////////////////

//mainFrame
#include "mainFrame/mainFrameDesigner.h"

//////////////////////////////////////////////////////////////////////////////////
#if wxUSE_CMDLINE_PARSER
#include <wx/cmdline.h>

void ibAppDesigner::OnInitCmdLine(wxCmdLineParser& parser)
{
	// Same layout as enterprise.exe — short names legacy, long names match
	// wenterprise-server so RunApplication emits flags that parse in all bins.
	parser.AddOption(wxT("file"),   wxT("file"),     "Database file path",      wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("srv"),    wxT("server"),   "Database server address", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("p"),      wxT("dbport"),   "Database server port",    wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("db"),     wxT("db"),       "Database name",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("usr"),    wxT("user"),     "Database user",           wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("pwd"),    wxT("password"), "Database password",       wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_usr"), wxT("ibuser"),   "IB user",                 wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("ib_pwd"), wxT("ibpwd"),    "IB password",             wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
	parser.AddOption(wxT("lc"),     wxT("locale"),   "UI locale",               wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);

	return wxApp::OnInitCmdLine(parser);
}

bool ibAppDesigner::OnCmdLineParsed(wxCmdLineParser& parser)
{
	// FILE ENTRY 
	parser.Found(wxT("file"), &m_strFile);

	// SERVER ENTRY
	parser.Found(wxT("srv"), &m_strServer);
	parser.Found(wxT("p"), &m_strPort);
	parser.Found(wxT("db"), &m_strDatabase);
	parser.Found(wxT("usr"), &m_strUser);
	parser.Found(wxT("pwd"), &m_strPassword);

	// USER  
	parser.Found(wxT("ib_usr"), &m_strIBUser);
	parser.Found(wxT("ib_pwd"), &m_strIBPassword);

	// LOCALE
	parser.Found(wxT("lc"), &m_strLocale);

	return wxApp::OnCmdLineParsed(parser);
}
#endif

//////////////////////////////////////////////////////////////////////////////////

// No exe-specific session class — see enterprise/mainApp.cpp. The pair's
// designer half lives entirely in ibFrontendMainFrameDesigner (which asks
// about an unsaved configuration and runs no session scripts); the
// session half is the same ibGUISession the enterprise client uses.

int ibAppDesigner::DoOnRun()
{
	// Get the data directory
	bool ret = false;

	if (m_strFile.IsEmpty() && m_strServer.IsEmpty()) {
		// No command-line args — show folder picker
		wxDirDialog dlg(nullptr, _("Select configuration database folder"),
			wxStandardPaths::Get().GetDocumentsDir(),
			wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST | wxDD_NEW_DIR_BUTTON);
		if (dlg.ShowModal() == wxID_OK) {
			m_strFile = dlg.GetPath();
		} else {
			return 0; // user cancelled
		}
	}

	if (m_strFile.IsEmpty()) {
		ret = appDataCreateServer(ibRunMode::eDESIGNER_MODE,
			m_strServer, m_strPort, m_strUser, m_strPassword, m_strDatabase, m_strLocale
		);
	}
	else {
		ret = appDataCreateFile(ibRunMode::eDESIGNER_MODE,
			m_strFile, m_strLocale
		);
	}

	if (!ret) {
		// Same chain-aware reporting as enterprise.exe — see there.
		const std::vector<wxString> chain = ibBackendException::DrainLastErrors();
		if (!chain.empty()) {
			wxString combined;
			for (std::size_t i = 0; i < chain.size(); ++i) {
				if (!combined.IsEmpty()) combined += wxT("\n--\n");
				combined += chain[i];
			}
			wxMessageBox(combined, _("OES Designer - startup error"),
				wxOK | wxICON_ERROR);
		}
		return 1;
	}

	ibProcessSplashScreen* splashScreenLoader =
		new ibProcessSplashScreen(wxBitmap(splashLogo_xpm),
			wxSPLASH_CENTRE_ON_SCREEN,
			-1, nullptr, -1, wxDefaultPosition, wxDefaultSize,
			wxBORDER_SIMPLE
		);

	// Init handlers
	wxInitAllImageHandlers();

	wxXmlResource::Get()->InitAllHandlers();
#if wxVERSION_NUMBER >= 2905 && wxVERSION_NUMBER <= 3100
	wxXmlResource::Get()->AddHandler(new wxAuiNotebookXmlHandler);
#elif wxVERSION_NUMBER > 3100
	wxXmlResource::Get()->AddHandler(new wxAuiXmlHandler);
#endif

#ifdef __WXMSW__
	::DisableProcessWindowsGhosting();
#endif 

#if DEBUG 
	wxLog::AddTraceMask(wxTRACE_MemAlloc);
	wxLog::AddTraceMask(wxTRACE_ResAlloc);
#if wxUSE_LOG
#if defined(__WXGTK__)
	wxLog::AddTraceMask("clipboard");
#elif defined(__WXMSW__)
	wxLog::AddTraceMask(wxTRACE_OleCalls);
#endif
#endif // wxUSE_LOG
#endif

	// Log to stderr while working on the command line
	delete wxLog::SetActiveTarget(new wxLogStderr);

	// Message output to the same as the log target
	delete wxMessageOutput::Set(new wxMessageOutputLog);

#if wxUSE_LIBPNG
	wxImage::AddHandler(new wxPNGHandler);
#endif

	// Support loading files from memory
	// Used to load the XRC preview, but could be useful elsewhere
	wxFileSystem::AddHandler(new wxMemoryFSHandler);

	// Support for loading files from archives
	wxFileSystem::AddHandler(new wxArchiveFSHandler);
	wxFileSystem::AddHandler(new wxFilterFSHandler);

#if wxUSE_LIBPNG
	wxImage::AddHandler(new wxPNGHandler);
#endif
	// Flow (designer IDE):
	//   1. CreateSession — registry Add (DesignerExclusivePolicy veto
	//      happens inside the registry Connect), returns the holder.
	//   2. Open — creds, standalone login dialog on failure.
	//   3. new ibFrontendMainFrameDesigner(std::move(holder)) — the window
	//      takes ownership of the session.
	//   4. Show — Designer kind → EnsureRuntime no-op; AllowRun passes
	//      unconditionally (no session scripts here).

	// AccessMode was set by appData's ctor based on runMode. Registry
	// listeners (wired in appData ctor) handle BindSessionToThread,
	// LoadMetadata, CreateRoot + CompileRoot through OnFirstConnect /
	// OnAuthenticated — nothing to compose here.
	// Holder on the stack until the designer window takes it — see
	// enterprise/mainApp.cpp for the rationale. Dropping it closes the
	// session, so every failure path below is complete as written.
	ibSessionHolder holder;
	wxString openError;
	ibSession::OpenResult openResult = ibSession::OpenResult::Failed;
	try {
		holder = appData->CreateSession<ibGUISession>();
		if (holder) {
			openResult = holder->Open(m_strIBUser, m_strIBPassword);
			if (openResult != ibSession::OpenResult::Authenticated)
				holder.Reset();
		}
	} catch (const ibBackendException& e) {
		openError = e.GetErrorDescription();
		holder.Reset();
		openResult = ibSession::OpenResult::Failed;
	} catch (const std::exception& e) {
		openError = wxString::FromUTF8(e.what());
		holder.Reset();
		openResult = ibSession::OpenResult::Failed;
	}

	if (!holder) {
		if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();
		// Same Cancel-vs-Failed split as enterprise.exe — see enterprise/
		// mainApp.cpp for the rationale.
		if (openResult == ibSession::OpenResult::Cancelled)
			return 0;
		const wxString message = openError.IsEmpty()
			? wxString(_("Authentication failed"))
			: openError;
		wxMessageBox(message, _("OES Designer"), wxOK | wxICON_ERROR);
		return 1;
	}

	// Wire the debug-client bridge AFTER m_session->Open(): debugClient
	// (the global ms_debugClient) is constructed inside metadataCreate
	// which fires from the OnFirstConnect listener during NotifyAuthenticated.
	// Calling SetDebuggerClientBridge before that point silently no-ops
	// (debugClient == nullptr branch in debugClientBridge.cpp), so
	// ibDebuggerClient's adapter never gets the bridge and OnEnterLoop /
	// OnSessionStart fire into the void — F5 then hits a breakpoint that
	// the IDE never displays.
	ibDebuggerClientBridge::SetDebuggerClientBridge(
		new ibDebuggerClientBridgeDesigner);

	if (splashScreenLoader != nullptr) splashScreenLoader->Destroy();

	// Same shape as enterprise.exe: the window takes the holder and owns
	// the session. On a failed Show the Destroy is delayed and no event
	// loop will ever prune it, so the session is removed by
	// registry->Stop() in OnExit — see the comment in enterprise/mainApp.cpp.
	auto* frame = new ibFrontendMainFrameDesigner(std::move(holder));
	if (!frame->Show()) {
		frame->Destroy();
		return 1;
	}
	return wxApp::OnRun();
}

int ibAppDesigner::OnExit()
{
	//release all created com-objects
#ifdef __WXMSW__
	ibValueOLE::ReleaseComObjects();
#endif

	if (wxSocketBase::IsInitialized())
		wxSocketBase::Shutdown();

	// Tear every session down through the session manager BEFORE
	// wxApp::OnExit. registry->Stop() submits Remove@Urgent for each
	// session in m_own and drains the queue — OnDisconnect listeners
	// fire while the wx event loop is still alive, so any frame-Destroy
	// scheduled from there gets dispatched. Without this the event
	// loop dies first and the Destroy events stay queued. Idempotent —
	// ~ibApplicationData calls Stop again best-effort.
	if (auto* registry = ibApplicationData::GetSessionRegistry())
		registry->Stop();

	bool success_exit = wxApp::OnExit();

	appDataDestroy();

	// The leak report used to print here. It now runs from atexit — registered where the hook is
	// armed — so that it sees the same heap the CRT dump sees. See the note there.

	// Allow clipboard data to persist after close
	if (wxTheClipboard->Open()) {
		wxTheClipboard->Flush();
		wxTheClipboard->Close();
	}

	// The last two blocks in the CRT dump were a wxLogOutputBest (8 bytes) and its wxLogFormatter
	// (4). wxEntryCleanup deletes the active log target and then deliberately LEAVES
	// auto-vivification on — its own comment says leaking is better than losing a message logged
	// from a static dtor. So the next log call after cleanup builds a fresh target that nothing
	// will ever delete, and something does log that late (registrar teardown runs there).
	//
	// We take the other side of that trade, and it costs nothing: with auto-creation off,
	// wxLog::GetMainThreadActiveTarget falls back to a STATIC wxLogOutputBest (log.cpp), so late
	// messages are still printed — they just stop coming from the heap.
	wxLog::DontCreateOnDemand();

	return success_exit;
}