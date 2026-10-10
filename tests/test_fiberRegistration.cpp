// A plugin that registers a fiber local is refused and unloaded. The
// process stays up, and a fiber that was already parked resumes with
// the same slots. Backend's own registrations are not in a load scope,
// so they still land.

#include <gtest/gtest.h>

#include <wx/dynlib.h>
#include <wx/ffile.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/log.h>
#include <wx/stdpaths.h>

#include "core/diagnostics/journal.h"
#include "core/fiber/fiberLocals.h"
#include "backend/appData.h"
#include "backend/appHost.h"
#include "backend/session/session.h"
#include "backend/session/sessionHolder.h"
#include "backend/session/workerPoolHeadless.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

namespace {

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
	bool IsOpen() const { return !ibApplicationHost::IsEmpty(); }
private:
	wxInitializer m_wxInit;
	bool          m_owns = false;
};

wxString PluginPath(const wxString& stem)
{
	return wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath()
		+ wxFILE_SEP_PATH
		+ wxDynamicLibrary::CanonicalizeName(stem);
}

std::string JournalText()
{
	wxFFile in(ibTechJournal::Path(), wxT("rb"));
	if (!in.IsOpened())
		return {};
	const wxFileOffset n = in.Length();
	if (n <= 0)
		return {};
	std::string bytes(static_cast<std::size_t>(n), '\0');
	const std::size_t got = in.Read(bytes.data(), bytes.size());
	bytes.resize(got);
	return bytes;
}

using SavesFn = int (*)();
using InitFn = int (*)(void*);

} // namespace

TEST(FiberRegistration, StaticRegistrationIsRefusedAndAParkedFiberResumes)
{
	ibTechJournal::Open(wxT("oes_tests"));
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen());

	const wxString path = PluginPath(wxT("fiberRegStatic"));
	ASSERT_TRUE(wxFileExists(path)) << path.ToStdString();

	auto session = std::make_shared<ibSession>(wxT("fiber-reg"), ibSessionKind::Designer);
	ibWorkerPoolHeadless pool(1);
	std::atomic<int> phase{ 0 };
	std::future<void> ran = pool.Submit(session.get(), [&] {
		phase.store(1);
		pool.Await(session.get(), [&] { return phase.load() >= 2; });
		phase.store(3);
	});

	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (phase.load() == 0 && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	ASSERT_EQ(phase.load(), 1);

	{
		wxDynamicLibrary library;
		ibFiberLocals::ModuleLoadScope loading(path);
		{
			wxLogNull quiet;
			ASSERT_TRUE(library.Load(path)) << path.ToStdString();
		}
		EXPECT_TRUE(loading.Refused());
		SavesFn saves = nullptr;
		{
			wxLogNull quiet;
			saves = reinterpret_cast<SavesFn>(library.GetSymbol(wxT("oes_fiber_register_saves")));
		}
		ASSERT_NE(nullptr, saves);
		EXPECT_EQ(0, saves());
	}

	phase.store(2);
	pool.Wake(session.get());
	ASSERT_EQ(ran.wait_for(std::chrono::seconds(5)), std::future_status::ready);
	EXPECT_NO_THROW(ran.get());
	EXPECT_EQ(phase.load(), 3);
	pool.Stop();

	ASSERT_TRUE(ibTechJournal::IsOpen());
	const std::string text = JournalText();
	EXPECT_NE(std::string::npos, text.find("fiberRegistration.static")) << text;
	EXPECT_NE(std::string::npos, text.find("refused while loading")) << text;
	const std::string file = wxFileName(path).GetFullName().ToStdString();
	EXPECT_NE(std::string::npos, text.find(file)) << text;
}

TEST(FiberRegistration, InitializeRegistrationIsRefused)
{
	ibTechJournal::Open(wxT("oes_tests"));

	const wxString path = PluginPath(wxT("fiberRegInit"));
	ASSERT_TRUE(wxFileExists(path)) << path.ToStdString();

	wxDynamicLibrary library;
	ibFiberLocals::ModuleLoadScope loading(path);
	{
		wxLogNull quiet;
		ASSERT_TRUE(library.Load(path)) << path.ToStdString();
	}
	EXPECT_FALSE(loading.Refused());

	InitFn init = nullptr;
	SavesFn saves = nullptr;
	{
		wxLogNull quiet;
		init = reinterpret_cast<InitFn>(library.GetSymbol(wxT("oes_plugin_initialize")));
		saves = reinterpret_cast<SavesFn>(library.GetSymbol(wxT("oes_fiber_register_saves")));
	}
	ASSERT_NE(nullptr, init);
	ASSERT_NE(nullptr, saves);
	EXPECT_EQ(0, init(nullptr));
	EXPECT_TRUE(loading.Refused());
	EXPECT_EQ(0, saves());

	ASSERT_TRUE(ibTechJournal::IsOpen());
	const std::string text = JournalText();
	EXPECT_NE(std::string::npos, text.find("fiberRegistration.init")) << text;
}

TEST(FiberRegistration, BackendRegistrationsStillRestoreTheSession)
{
	ibBaseForTest base;
	ASSERT_TRUE(base.IsOpen());
	auto session = std::make_shared<ibSession>(wxT("fiber-reg-own"), ibSessionKind::Designer);

	ibFiberLocals::Snapshot snap;
	{
		ibSessionScope bound(session.get());
		ASSERT_EQ(session.get(), ibSession::Current());
		snap = ibFiberLocals::ForScheduler();
	}
	EXPECT_EQ(nullptr, ibSession::Current());
	snap.Install();
	EXPECT_EQ(session.get(), ibSession::Current());
}
