// A role handler (OnAccessRead / OnAccessWrite) runs before every query, under
// the policy's lock. It may not ask the user: a question there would be asked
// on every execution, and while it was open the person could not do anything
// else. The refusal is a script error that names the handler.
//
// SetSessionParameters may ask. It holds no lock. While the question is open
// the session's trust and its write window are paused, so a call that arrives
// meanwhile runs under RLS with the parameters not yet set and fails closed.
// Both are put back when the person answers.

#include <gtest/gtest.h>

#include <wx/init.h>

#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/backend_exception.h"
#include "backend/backend_mainFrame.h"
#include "backend/metadataConfiguration.h"
#include "backend/metaCollection/metaObject.h"
#include "backend/metaCollection/metaRoleObject.h"
#include "backend/metaCollection/partial/catalog.h"
#include "backend/query/dataQueryBuilder.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/sessionRegistry.h"
#include "backend/session/workerPoolHeadless.h"
#include "backend/system/systemEnum.h"
#include "backend/system/systemManager.h"
#include "backend/userInfo.h"

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

// Question goes through Current(). A test that asks one opens a base, as every
// pool in the engine has by the time it runs a task.
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
	bool IsOpen() const { return m_owns && m_wxInit.IsOk(); }
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

bool SetModuleText(const ibValueMetaObjectManagerModule* module, const wxString& text)
{
	if (module == nullptr)
		return false;
	const_cast<ibValueMetaObjectManagerModule*>(module)->SetModuleText(text);
	return true;
}

bool Compile(ibSession& session, ibMetaDataConfigurationFile& cfg, wxString& error)
{
	if (session.CreateRoot(&cfg) == nullptr) {
		error = wxT("CreateRoot returned null");
		return false;
	}
	try {
		if (!session.CompileRoot()) {
			error = wxT("CompileRoot returned false");
			return false;
		}
	}
	catch (const ibBackendException& err) {
		error = err.GetErrorDescription();
		return false;
	}
	return true;
}

// The base frame's modal. Desktop, thin client and web call the same refusal
// before they show anything; this one reaches that function and stops there.
class ibBaseFrame : public ibBackendDocFrame {
public:
	explicit ibBaseFrame(const std::shared_ptr<ibRoleSession>& session)
		: ibBackendDocFrame(ibSessionHolder(session))
		, m_role(session.get())
	{
		session->m_frame = this;
	}
	~ibBaseFrame() override
	{
		if (m_role != nullptr)
			m_role->m_frame = nullptr;
	}
	void SetTitle(const wxString&) override {}
	void SetStatusText(const wxString&, int) override {}
	void RefreshFrame() override {}
	void RaiseFrame() override {}
	int ShowModalMessage(const wxString& message, const wxString& caption, int style) override
	{
		return ibBackendDocFrame::ShowModalMessage(message, caption, style);
	}
private:
	ibRoleSession* m_role;
};

// ShowModalMessage as the web frame does it: park on the session's pool until
// the person answers. Used only for SetSessionParameters, which is allowed to ask.
class ibAskFrame : public ibBackendDocFrame {
public:
	ibAskFrame(const std::shared_ptr<ibRoleSession>& session, ibWorkerPoolHeadless* pool,
		std::atomic<bool>* answered, ibLatch* asked,
		std::atomic<bool>* trustedAfter, std::atomic<bool>* windowAfter)
		: ibBackendDocFrame(ibSessionHolder(session))
		, m_role(session.get())
		, m_pool(pool)
		, m_answered(answered)
		, m_asked(asked)
		, m_trustedAfter(trustedAfter)
		, m_windowAfter(windowAfter)
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
		// Await's pause has put the asker's flags back. Still inside
		// SetSessionParameters, so both are open again.
		m_trustedAfter->store(m_role->AccessTrusted());
		m_windowAfter->store(m_role->SessionParametersOpen());
		return wxYES;
	}

private:
	ibRoleSession*         m_role;
	ibWorkerPoolHeadless*  m_pool;
	std::atomic<bool>*     m_answered;
	ibLatch*               m_asked;
	std::atomic<bool>*     m_trustedAfter;
	std::atomic<bool>*     m_windowAfter;
};

const wxString kRoleAsks =
	wxT("Procedure OnAccessRead(Source, Operation, Allowed) {\n")
	wxT("\tQuestion(\"allow this row?\", QuestionMode.YesNo);\n")
	wxT("\tAllowed = true;\n")
	wxT("}\n");

const wxString kSessionAsks =
	wxT("Procedure SetSessionParameters() {\n")
	wxT("\tQuestion(\"which organisation?\", QuestionMode.YesNo);\n")
	wxT("}\n");

} // namespace

TEST(FiberTrust, ARoleHandlerThatAsksIsRefusedByName)
{
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen());
	auto session = std::make_shared<ibRoleSession>(wxT("role-name"), ibSessionKind::Enterprise);
	ibSessionScope bound(session.get());
	ibBaseFrame frame(session);

	{
		ibRoleHandlerScope handler(session.get(), wxT("OnAccessRead"));
		try {
			ibValueSystemFunction::Question(wxT("allow this row?"), ibQuestionMode::ibQuestionMode_YesNo);
			FAIL() << "Question was asked from a role handler";
		}
		catch (const ibBackendException& err) {
			EXPECT_NE(err.GetErrorDescription().Find(wxT("OnAccessRead")), wxNOT_FOUND)
				<< err.GetErrorDescription().ToStdString();
		}
	}
	{
		ibRoleHandlerScope handler(session.get(), wxT("OnAccessWrite"));
		try {
			frame.ShowModalMessage(wxT("allow this row?"), wxT("Question"), wxYES_NO);
			FAIL() << "a modal was shown from a role handler";
		}
		catch (const ibBackendException& err) {
			EXPECT_NE(err.GetErrorDescription().Find(wxT("OnAccessWrite")), wxNOT_FOUND)
				<< err.GetErrorDescription().ToStdString();
		}
	}

	// Outside a handler the same question is asked. No frame call throws.
	EXPECT_NO_THROW((void)ibValueSystemFunction::Question(
		wxT("later"), ibQuestionMode::ibQuestionMode_OK));
}

// Through the real gate: a restricting role, so the policy and its lock, on an
// enterprise session. The handler's only act is Question. The read comes back
// refused, the modal is never shown, and the call returns.
TEST(FiberTrust, ARoleHandlerThatAsksThroughTheGateDoesNotHang)
{
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen());

	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	auto* role = dynamic_cast<ibValueMetaObjectRole*>(
		cfg.CreateMetaObject(g_metaRoleCLSID, root, /*runObject*/ false));
	auto* catalog = dynamic_cast<ibValueMetaObjectCatalog*>(
		cfg.CreateMetaObject(g_metaCatalogCLSID, root, /*runObject*/ false));
	ASSERT_NE(role, nullptr);
	ASSERT_NE(catalog, nullptr);
	cfg.RenameMetaObject(role, wxT("Clerk"));
	cfg.RenameMetaObject(catalog, wxT("Goods"));
	ASSERT_TRUE(SetModuleText(role->GetRoleModule(), kRoleAsks));
	// Full access would skip the handler. This role objects to administration,
	// so the policy runs it, and it does not object to reading the catalog.
	ASSERT_TRUE(root->SetRight(wxT("Administration"), role->GetMetaID(), false));
	ASSERT_TRUE(cfg.RunDatabase()) << "the configuration in memory did not run";

	auto session = std::make_shared<ibRoleSession>(wxT("role-gate"), ibSessionKind::Enterprise);
	ibUserInfo user;
	user.m_strUserGuid = wxT("11111111-1111-1111-1111-111111111111");
	user.m_strUserName = wxT("clerk");
	ibUserInfo::ibUserRole membership;
	membership.m_miRoleId = role->GetMetaID();
	membership.m_strRoleName = wxT("Clerk");
	membership.m_mode = ibRoleCompositionMode_Intersection;
	user.m_roleArray.push_back(membership);
	// SetUserInfo is the registry's. The policy reads this session's roles.
	ibSessionRegistry* registry = ibApplicationInstance::GetSessionRegistry();
	ASSERT_NE(registry, nullptr);
	registry->InstallUser(session.get(), user, wxEmptyString);

	{
		ibSessionScope bound(session.get());
		wxString error;
		ASSERT_TRUE(Compile(*session, cfg, error)) << error.ToStdString();
	}

	const ibBackendQueryable* source = catalog->GetQueryable();
	ASSERT_NE(source, nullptr);
	const std::vector<const ibBackendQueryColumn*> columns = source->GetColumns();
	ASSERT_FALSE(columns.empty());
	const ibBackendQueryColumn* column = columns.front();
	ASSERT_NE(column, nullptr);

	std::atomic<bool> asked { false };
	class ibFlagFrame : public ibBackendDocFrame {
	public:
		ibFlagFrame(const std::shared_ptr<ibRoleSession>& session, std::atomic<bool>* asked)
			: ibBackendDocFrame(ibSessionHolder(session)), m_role(session.get()), m_asked(asked)
		{
			session->m_frame = this;
		}
		~ibFlagFrame() override
		{
			if (m_role != nullptr)
				m_role->m_frame = nullptr;
		}
		void SetTitle(const wxString&) override {}
		void SetStatusText(const wxString&, int) override {}
		void RefreshFrame() override {}
		void RaiseFrame() override {}
		// Returns at once. A question that reaches here is the failure; parking
		// the thread would turn that failure into a stuck test.
		int ShowModalMessage(const wxString&, const wxString&, int) override
		{
			m_asked->store(true);
			return wxYES;
		}
	private:
		ibRoleSession*     m_role;
		std::atomic<bool>* m_asked;
	} frame(session, &asked);

	{
		ibSessionScope bound(session.get());
		const ibAccessPolicy* policy = session->GetAccessPolicy();
		ASSERT_NE(policy, nullptr) << "an enterprise session with a restricting role built no policy";
		ibDataQueryBuilder query;
		query.WithAccessPolicy(policy).From(source);
		query.Select(column, wxT("c"));
		try {
			(void)query.Execute(ibReadPageRequest{});
			FAIL() << "the read was allowed";
		}
		catch (const ibBackendAccessException& err) {
			EXPECT_NE(err.GetErrorDescription().Find(wxT("refused by the role")), wxNOT_FOUND)
				<< err.GetErrorDescription().ToStdString();
		}
		catch (const ibBackendException& err) {
			FAIL() << err.GetErrorDescription().ToStdString();
		}
	}
	EXPECT_FALSE(asked.load()) << "the role handler's question was shown";

	session->ClearRoot();
	cfg.CloseDatabase();
}

TEST(FiberTrust, SetSessionParametersPausesTrustWhileItAsks)
{
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen());

	ibMetaDataConfigurationFile cfg;
	ibValueMetaObjectConfiguration* root = cfg.GetCommonMetaObject();
	ASSERT_NE(root, nullptr);
	ASSERT_TRUE(SetModuleText(root->GetSessionModule(), kSessionAsks));
	ASSERT_TRUE(cfg.RunDatabase()) << "the configuration in memory did not run";

	auto session = std::make_shared<ibRoleSession>(wxT("session-params"), ibSessionKind::Enterprise);
	{
		ibSessionScope bound(session.get());
		wxString error;
		ASSERT_TRUE(Compile(*session, cfg, error)) << error.ToStdString();
	}

	ibWorkerPoolHeadless pool(1);
	std::atomic<bool> answered { false };
	ibLatch asked;
	std::atomic<bool> trustedAfter { false };
	std::atomic<bool> windowAfter { false };
	ibAskFrame frame(session, &pool, &answered, &asked, &trustedAfter, &windowAfter);

	std::future<void> asking = pool.Submit(session.get(), [&] {
		session->SetSessionParameters();
	});

	ASSERT_TRUE(asked.Wait()) << "SetSessionParameters never asked";

	std::atomic<bool> sawTrusted { true };
	std::atomic<bool> wroteParameter { true };
	std::future<void> during = pool.Submit(session.get(), [&] {
		sawTrusted.store(session->AccessTrusted());
		try {
			session->SetSessionParameter(wxT("Org"), ibValue(true));
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
		<< "a task that arrived while SetSessionParameters was asking ran with RLS off";
	EXPECT_FALSE(wroteParameter.load())
		<< "a task that arrived while the parameters were not yet set wrote one";

	answered.store(true);
	pool.Wake(session.get());
	ASSERT_EQ(asking.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(asking.get());
	EXPECT_TRUE(trustedAfter.load());
	EXPECT_TRUE(windowAfter.load());

	pool.Stop();
	session->ClearRoot();
	cfg.CloseDatabase();
}
