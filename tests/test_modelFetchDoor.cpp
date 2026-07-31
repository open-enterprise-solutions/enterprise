// =============================================================================
// The model's fetch door — ibDataViewModel::SubmitFetchAsync + GuardFetch.
//
// One door, three promises, and the control leans on all three without knowing
// which kind of model it is holding:
//
//   1. the work LEAVES the calling thread — that is the whole point of the door;
//      the caller may be a UI thread with a window to paint or a web session's
//      worker with an answer to finish, and neither may block on a read;
//   2. two units NEVER overlap — a sort click is a reset, and a portion still on
//      its way back when the next one starts would land in the same buffer;
//   3. the model WAITS for its read when it goes — a read walking a model that
//      is being destroyed is a use-after-free, and no caller should have to
//      arrange that.
//
// Pure backend: no appData, no session, no pool. The base door answers with a
// thread of its own, and that is exactly what is under test here. The RENTED
// answer (ibValueModel — a background job) needs a live application and belongs
// to the tenancy target.
// =============================================================================

#include <gtest/gtest.h>

#include "backend/modelView.h"

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace {

// The smallest thing that IS a model. The door lives on the base; these three
// pure virtuals are the entire price of becoming concrete.
class ibStubFetchModel : public ibDataViewModel {
public:
	void GetValue(wxVariant& WXUNUSED(variant),
	              const ibDataViewItem& WXUNUSED(item),
	              unsigned int WXUNUSED(col)) const override {}

	bool SetValue(const wxVariant& WXUNUSED(variant),
	              const ibDataViewItem& WXUNUSED(item),
	              unsigned int WXUNUSED(col)) override { return false; }

	ibDataViewItem GetParent(const ibDataViewItem& WXUNUSED(item)) const override {
		return ibDataViewItem();
	}
};

// A model that answers the door the way a RUNTIME one does: somewhere else
// entirely, on a thread this class does not own or join. It is the shape that
// makes the lock matter — the base's own thread serialises by joining, so a test
// over the base alone could never tell whether GuardFetch does anything.
class ibDetachedFetchModel : public ibStubFetchModel {
public:
	void SubmitFetchAsync(std::function<void()> work) override {
		m_running.push_back(std::async(std::launch::async, GuardFetch(std::move(work))));
	}

	// Wait every submitted unit out. Tests must not race the assertions against
	// work still in flight.
	void Settle() {
		for (auto& f : m_running)
			if (f.valid()) f.wait();
		m_running.clear();
	}

private:
	std::vector<std::future<void>> m_running;
};

// Records the deepest nesting ever observed inside the work: 1 means the units
// took turns, 2 means two of them were inside at once.
struct ibOverlapProbe {
	std::atomic<int> depth { 0 };
	std::atomic<int> peak  { 0 };
	std::atomic<int> ran   { 0 };

	void Enter() {
		const int now = depth.fetch_add(1) + 1;
		int seen = peak.load();
		while (now > seen && !peak.compare_exchange_weak(seen, now)) {}
	}
	void Leave() { depth.fetch_sub(1); ran.fetch_add(1); }
};

} // namespace

// ---------------------------------------------------------------------------
// 1. The work leaves the calling thread
// ---------------------------------------------------------------------------

TEST(ModelFetchDoor, WorkRunsOffTheCallingThread)
{
	ibStubFetchModel model;

	std::promise<std::thread::id> where;
	std::future<std::thread::id> ran = where.get_future();

	model.SubmitFetchAsync([&where]() {
		where.set_value(std::this_thread::get_id());
	});

	ASSERT_EQ(ran.wait_for(std::chrono::seconds(5)), std::future_status::ready)
		<< "the unit of work never ran";
	EXPECT_NE(ran.get(), std::this_thread::get_id())
		<< "the door must take the read OFF the thread that asked — that is the "
		   "only reason it exists";
}

// An empty unit is not an error: the control builds its work from state that may
// have gone stale between the decision and the dispatch.
TEST(ModelFetchDoor, EmptyWorkIsIgnored)
{
	ibStubFetchModel model;
	EXPECT_NO_THROW(model.SubmitFetchAsync(nullptr));
}

// ---------------------------------------------------------------------------
// 2. One portion at a time — the door's single lock
// ---------------------------------------------------------------------------

// The base answers with a thread of its own and joins the previous read before
// starting the next, so two units cannot overlap even before the lock is
// considered. Pinned because the control relies on it: a second dispatch while
// the first is out must not produce two readers of one buffer.
TEST(ModelFetchDoor, BaseThreadRunsOneUnitAtATime)
{
	ibStubFetchModel model;
	ibOverlapProbe probe;

	for (int i = 0; i < 8; ++i) {
		model.SubmitFetchAsync([&probe]() {
			probe.Enter();
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
			probe.Leave();
		});
	}

	// Drain: Run() joins the previous unit before starting the next, so one more
	// submission is enough to know every unit above has finished.
	model.SubmitFetchAsync([]() {});

	EXPECT_EQ(probe.ran.load(), 8)   << "every submitted unit must run";
	EXPECT_EQ(probe.peak.load(), 1)
		<< "two reads were inside the model at the same time";
}

// The real assertion of the lock: a model that runs each unit somewhere of its
// own — which is what a runtime table does with its rented job — still gets one
// at a time, because GuardFetch wraps the work rather than the thread.
TEST(ModelFetchDoor, GuardFetchSerialisesEvenWhenTheModelRunsThemInParallel)
{
	ibDetachedFetchModel model;
	ibOverlapProbe probe;

	constexpr int kUnits = 6;
	for (int i = 0; i < kUnits; ++i) {
		model.SubmitFetchAsync([&probe]() {
			probe.Enter();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			probe.Leave();
		});
	}
	model.Settle();

	EXPECT_EQ(probe.ran.load(), kUnits)  << "every submitted unit must run";
	EXPECT_EQ(probe.peak.load(), 1)
		<< "GuardFetch is the ONE lock in the path — units that run on independent "
		   "threads must still take their turn";
}

// ---------------------------------------------------------------------------
// 3. A read never outlives the model it is walking
// ---------------------------------------------------------------------------

TEST(ModelFetchDoor, DestructorWaitsForTheReadInFlight)
{
	// Shared with the work, so the flag outlives the model on purpose — that is
	// the whole question being asked: is the work over by the time the model is?
	auto finished = std::make_shared<std::atomic<bool>>(false);

	auto* model = new ibStubFetchModel();
	model->SubmitFetchAsync([finished]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(150));
		finished->store(true);
	});

	delete model;   // must join the read, not race it

	EXPECT_TRUE(finished->load())
		<< "the model was destroyed while its read was still walking it";
}
