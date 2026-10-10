// A role handler runs privileged (ibAccessTrustScope) and may ask a person.
// Question waits in the session pool, and that wait runs the session's
// queued tasks on the same fiber. Those tasks used to see the handler's
// bypass — RLS off, session parameters writable — because both flags live
// on the session and the handler's scope was still on the stack.
//
// The frame here is the web frame's shape: ShowModalMessage parks with
// Await until the person answers. The handler is the shape of
// ApplyToSource: the trust scope and the write window, then Question.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/backend_mainFrame.h"
#include "backend/session/session.h"
#include "backend/session/workerPoolHeadless.h"
#include "backend/system/systemEnum.h"
#include "backend/system/systemManager.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>

namespace {

class ibLatch {
public:
	void Signal()
	{
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_set = true;
		}
		m_cv.notify_all();
	}
	bool Wait(std::chrono::milliseconds timeout = std::chrono::seconds(5))
	{
		std::unique_lock<std::mutex> lk(m_mtx);
		return m_cv.wait_for(lk, timeout, [this] { return m_set; });
	}
private:
	std::mutex              m_mtx;
	std::condition_variable m_cv;
	bool                    m_set = false;
};

// ibSession::Current() answers "no session" while the process holds no base,
// before it reads any binding. Question goes through CurrentFrame(), so a
// test that asks one opens a base, as every pool in the engine has by the
// time it runs a task.
class ibBaseForTest {
public:
	ibBaseForTest()
	{
		if (m_wxInit.IsOk() && ibApplicationHost::IsEmpty()) {
			ibApplicationInstance::CreateAppDataEnv(ibRunMode::eFILE_MODE);
			m_owns = !ibApplicationHost::IsEmpty();
		}
	}
	~ibBaseForTest()
	{
		if (m_owns)
			ibApplicationInstance::DestroyAppDataEnv();
	}
private:
	wxInitializer m_wxInit;
	bool          m_owns = false;
};

class ibRoleSession : public ibSession {
public:
	using ibSession::ibSession;
	ibBackendDocFrame* m_frame = nullptr;
	ibBackendDocFrame* GetFrame() const override { return m_frame; }
};

// ShowModalMessage is what Question calls. This one waits the way the web
// frame does: the session's pool, until the answer.
class ibAskFrame : public ibBackendDocFrame {
public:
	ibAskFrame(const std::shared_ptr<ibRoleSession>& session, ibWorkerPoolHeadless* pool,
		std::atomic<bool>* answered, ibLatch* asked)
		: ibBackendDocFrame(ibSessionHolder(session))
		, m_role(session.get())
		, m_pool(pool)
		, m_answered(answered)
		, m_asked(asked)
	{
		session->m_frame = this;
	}
	~ibAskFrame() override
	{
		if (m_role != nullptr)
			m_role->m_frame = nullptr;
	}

	void SetTitle(const wxString&) override {}
	void SetStatusText(const wxString&, int) override {}
	void RefreshFrame() override {}
	void RaiseFrame() override {}

	int ShowModalMessage(const wxString&, const wxString&, int) override
	{
		m_asked->Signal();
		m_pool->Await(m_role, [this] { return m_answered->load(); });
		return wxYES;
	}

private:
	ibRoleSession*         m_role;
	ibWorkerPoolHeadless*  m_pool;
	std::atomic<bool>*     m_answered;
	ibLatch*               m_asked;
};

} // namespace

TEST(FiberTrust, AQuestionFromARoleHandlerDoesNotLendItsTrust)
{
	ibBaseForTest base;
	ibWorkerPoolHeadless pool(1);
	auto session = std::make_shared<ibRoleSession>(wxT("role-question"), ibSessionKind::Designer);
	std::atomic<bool> answered { false };
	ibLatch asked;
	ibAskFrame frame(session, &pool, &answered, &asked);

	std::atomic<bool> sawTrusted { true };
	std::atomic<bool> wroteParameter { true };
	std::atomic<bool> trustedAgain { false };
	std::atomic<bool> windowAgain { false };
	std::atomic<bool> wroteAfter { false };
	std::atomic<ibSession*> currentDuring { nullptr };

	std::future<void> handler = pool.Submit(session.get(), [&]() {
		// ApplyToSource opens both around OnAccessRead / OnAccessWrite.
		// SetSessionParameters opens the same pair around the module.
		ibAccessTrustScope trust(session.get());
		ibSessionParameterWriteWindow open(session.get());

		currentDuring.store(ibSession::Current());
		const ibValue answer = ibValueSystemFunction::Question(
			wxT("allow this row?"), ibQuestionMode::ibQuestionMode_YesNo);
		(void)answer;

		// The handler is privileged again once the person has answered.
		trustedAgain.store(session->AccessTrusted());
		windowAgain.store(session->SessionParametersOpen());
		try {
			session->SetSessionParameter(wxT("Org"), ibValue(true));
			wroteAfter.store(true);
		}
		catch (const ibBackendException&) {
			wroteAfter.store(false);
		}
	});

	ASSERT_TRUE(asked.Wait()) << "Question never reached the frame";
	EXPECT_EQ(currentDuring.load(), session.get())
		<< "the role handler was not the session Question asks through";

	std::future<void> during = pool.Submit(session.get(), [&]() {
		sawTrusted.store(session->AccessTrusted());
		try {
			session->SetSessionParameter(wxT("During"), ibValue(true));
			wroteParameter.store(true);
		}
		catch (const ibBackendException&) {
			wroteParameter.store(false);
		}
	});

	ASSERT_EQ(during.wait_for(std::chrono::seconds(5)), std::future_status::ready)
		<< "the task queued while the question was open never ran";
	EXPECT_NO_THROW(during.get());
	EXPECT_FALSE(sawTrusted.load())
		<< "a task that arrived while the handler was asking ran with RLS off";
	EXPECT_FALSE(wroteParameter.load())
		<< "a task that arrived while the handler was asking wrote a session parameter";

	answered.store(true);
	pool.Wake(session.get());
	ASSERT_EQ(handler.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(handler.get());
	EXPECT_TRUE(trustedAgain.load());
	EXPECT_TRUE(windowAgain.load());
	EXPECT_TRUE(wroteAfter.load());

	pool.Stop();
}
