#include "clusterService.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>

#ifdef __WXMSW__

#include <condition_variable>
#include <mutex>
#include <thread>

#include <windows.h>

#include <wx/log.h>   // wxSysErrorMsgStr

#include "core/server/clusterControl.h"   // ibClusterControl::ServiceName

namespace {

// A stop not done in this long is the cluster's to finish by killing its servers (ibClusterWorker) — the manager is told
// to wait so long.
constexpr DWORD kStopWaitMs = 35000;

// WHERE THE CONNECTION STANDS — told by the manager's thread, waited for by the main one.
enum class ibConnection { Waiting, Connected, Refused };

std::thread             s_dispatcher;   // the manager's: it returns once the service is said to be stopped
std::mutex              s_mutex;
std::condition_variable s_told;
ibConnection            s_connection = ibConnection::Waiting;
DWORD                   s_refusal = 0;
SERVICE_STATUS_HANDLE   s_handle = nullptr;
std::function<void()>   s_stop;

ibString Refusal(const ibChar* what, DWORD code)
{
	return ibString::Format(wxT("%s: %s"), what, wxSysErrorMsgStr(code));
}

void Report(DWORD state, DWORD code = 0)
{
	SERVICE_STATUS status{};
	status.dwServiceType             = SERVICE_WIN32_OWN_PROCESS;
	status.dwCurrentState            = state;
	status.dwControlsAccepted        = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
	status.dwWin32ExitCode           = code == 0 ? NO_ERROR : ERROR_SERVICE_SPECIFIC_ERROR;
	status.dwServiceSpecificExitCode = code;
	status.dwWaitHint                = state == SERVICE_STOP_PENDING ? kStopWaitMs : 0;
	::SetServiceStatus(s_handle, &status);
}

DWORD WINAPI OnControl(DWORD control, DWORD, void*, void*)
{
	switch (control) {
	case SERVICE_CONTROL_STOP:
	case SERVICE_CONTROL_SHUTDOWN:
		Report(SERVICE_STOP_PENDING);
		s_stop();
		return NO_ERROR;
	case SERVICE_CONTROL_INTERROGATE:
		return NO_ERROR;
	}
	return ERROR_CALL_NOT_IMPLEMENTED;
}

void WINAPI ServiceMain(DWORD, wchar_t**)
{
	s_handle = ::RegisterServiceCtrlHandlerExW(ibClusterControl::ServiceName, OnControl, nullptr);
	const DWORD refusal = ::GetLastError();
	if (s_handle != nullptr)
		Report(SERVICE_RUNNING);
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_connection = s_handle != nullptr ? ibConnection::Connected : ibConnection::Refused;
		s_refusal = refusal;
	}
	s_told.notify_all();
}

} // namespace

bool ibClusterService::Install(const ibString& folder, ibString& error)
{
	wxFileName absolute = wxFileName::DirName(folder);
	absolute.MakeAbsolute();
	const ibString command = ibString::Format(wxT("\"%s\" --service --dir=\"%s\""),
		wxStandardPaths::Get().GetExecutablePath(), absolute.GetPath());

	const SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
	if (manager == nullptr) {
		error = Refusal(wxT("the service manager is not opened - an administrator installs a service"), ::GetLastError());
		return false;
	}
	const SC_HANDLE service = ::CreateServiceW(manager, ibClusterControl::ServiceName, ibClusterControl::ServiceTitle,
		SERVICE_CHANGE_CONFIG, SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, command.wc_str(),
		nullptr, nullptr, nullptr, nullptr, nullptr);
	if (service == nullptr) {
		error = Refusal(wxT("the service is not installed"), ::GetLastError());
		::CloseServiceHandle(manager);
		return false;
	}
	const ibString description = ibString::Format(wxT("Runs the application servers of %s, a process for each base."),
		absolute.GetPath());
	SERVICE_DESCRIPTIONW said{ const_cast<wchar_t*>(description.wc_str()) };
	::ChangeServiceConfig2W(service, SERVICE_CONFIG_DESCRIPTION, &said);
	::CloseServiceHandle(service);
	::CloseServiceHandle(manager);
	return true;
}

bool ibClusterService::Uninstall(ibString& error)
{
	const SC_HANDLE manager = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
	if (manager == nullptr) {
		error = Refusal(wxT("the service manager is not opened"), ::GetLastError());
		return false;
	}
	const SC_HANDLE service = ::OpenServiceW(manager, ibClusterControl::ServiceName, SERVICE_STOP | DELETE);
	if (service == nullptr) {
		error = Refusal(wxT("the service is not opened - an administrator removes a service"), ::GetLastError());
		::CloseServiceHandle(manager);
		return false;
	}
	// Stopped first — a service that is not running answers so, and is removed all the same.
	SERVICE_STATUS status{};
	::ControlService(service, SERVICE_CONTROL_STOP, &status);
	const bool removed = ::DeleteService(service) != FALSE;
	if (!removed)
		error = Refusal(wxT("the service is not removed"), ::GetLastError());
	::CloseServiceHandle(service);
	::CloseServiceHandle(manager);
	return removed;
}

bool ibClusterService::Connect(std::function<void()> stop, ibString& error)
{
	s_stop = std::move(stop);
	s_dispatcher = std::thread([] {
		SERVICE_TABLE_ENTRYW table[] = {
			{ const_cast<wchar_t*>(ibClusterControl::ServiceName), ServiceMain },
			{ nullptr, nullptr }
		};
		if (!::StartServiceCtrlDispatcherW(table)) {
			const DWORD refusal = ::GetLastError();
			std::lock_guard<std::mutex> lock(s_mutex);
			if (s_connection == ibConnection::Waiting) {
				s_connection = ibConnection::Refused;
				s_refusal = refusal;
			}
		}
		s_told.notify_all();
	});

	std::unique_lock<std::mutex> lock(s_mutex);
	s_told.wait(lock, [] { return s_connection != ibConnection::Waiting; });
	if (s_connection == ibConnection::Connected)
		return true;
	lock.unlock();
	s_dispatcher.join();
	error = Refusal(wxT("the service manager is not reached - --service is the manager's to start"), s_refusal);
	return false;
}

void ibClusterService::Disconnect(int code)
{
	if (!s_dispatcher.joinable())
		return;
	Report(SERVICE_STOPPED, static_cast<DWORD>(code));
	s_dispatcher.join();
}

#else

bool ibClusterService::Install(const ibString&, ibString& error)
{
	error = wxT("a service is installed here by a systemd unit: ExecStart=<this program> --service --dir=<server folder>");
	return false;
}

bool ibClusterService::Uninstall(ibString& error)
{
	error = wxT("a service is removed here with its systemd unit");
	return false;
}

bool ibClusterService::Connect(std::function<void()>, ibString&)
{
	return true;
}

void ibClusterService::Disconnect(int)
{
}

#endif
