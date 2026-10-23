#include "connectionFile.h"

#include "core/fileSystem.h"

#include <wx/dynlib.h>
#include <wx/filename.h>
#include <wx/intl.h>   // _() — what a refusal says to a person
#include <wx/log.h>

namespace {

// fileserver beside the program — loaded once and never unloaded: what the server registers in the backend as it loads
// (frmserver's forms and controls) must outlive every base, and an unloading at the process's end would run after the
// statics it unregisters from (the 2026-10-07 exit crash, codeRunner). Null when it could not be loaded; `path` — where
// it was looked for.
wxDllType Library(wxString& path)
{
	// Beside the program. Inside enterprise-thin.app that is next to the bundle, where
	// libfileserver.dylib is shipped — not Contents/MacOS/, which is only the binary.
	path = ibProgramFolder() + wxFileName::GetPathSeparator()
		+ wxDynamicLibrary::CanonicalizeName(wxT("fileserver"));
	static const wxDllType s_library = [](const wxString& from) {
		wxLogNull quiet;   // a library not found is said by the caller, not by a dialog of wx's
		wxDynamicLibrary library;
		return library.Load(from) ? library.Detach() : wxDllType(nullptr);
	}(path);
	return s_library;
}

} // namespace

bool ibProtocolFileBase::Open(const ibString& request, wxString& error)
{
	Close();

	wxString path;
	const wxDllType library = Library(path);
	if (library == nullptr) {
		error = wxString::Format(_("The file base library could not be loaded: %s"), path);
		return false;
	}
	const auto open = reinterpret_cast<decltype(&ibFileBaseOpen)>(
		wxDynamicLibrary::RawGetSymbol(library, wxT("ibFileBaseOpen")));
	const auto call = reinterpret_cast<decltype(&ibFileBaseCall)>(
		wxDynamicLibrary::RawGetSymbol(library, wxT("ibFileBaseCall")));
	const auto listen = reinterpret_cast<decltype(&ibFileBaseListen)>(
		wxDynamicLibrary::RawGetSymbol(library, wxT("ibFileBaseListen")));
	const auto disconnect = reinterpret_cast<decltype(&ibFileBaseDisconnect)>(
		wxDynamicLibrary::RawGetSymbol(library, wxT("ibFileBaseDisconnect")));
	const auto close = reinterpret_cast<decltype(&ibFileBaseClose)>(
		wxDynamicLibrary::RawGetSymbol(library, wxT("ibFileBaseClose")));
	if (open == nullptr || call == nullptr || listen == nullptr || disconnect == nullptr || close == nullptr) {
		error = wxString::Format(_("%s is not the file base library this program was built with."), path);
		return false;
	}

	m_base = open(request, error);
	if (m_base == nullptr)
		return false;
	m_call = call;
	m_listen = listen;
	m_disconnect = disconnect;
	m_close = close;
	return true;
}

void ibProtocolFileBase::Close()
{
	if (m_base != nullptr && m_close != nullptr)
		m_close(m_base);
	m_base = nullptr;
	m_call = nullptr;
	m_listen = nullptr;
	m_disconnect = nullptr;
	m_close = nullptr;
}

ibProtocolFileBase::~ibProtocolFileBase()
{
	Close();
}

bool ibProtocolFileBase::Call(const ibString& request, const void* connection, const ibString& address,
	ibString& answer, wxString& error) const
{
	if (m_base == nullptr || m_call == nullptr) {
		error = wxT("no base is open");
		return false;
	}
	return m_call(m_base, request, connection, address, answer, error);
}

void ibProtocolFileBase::Listen(const void* connection, std::function<void(const ibString& text)> notified) const
{
	if (m_base != nullptr && m_listen != nullptr)
		m_listen(m_base, connection, std::move(notified));
}

void ibProtocolFileBase::Disconnect(const void* connection) const
{
	if (m_base != nullptr && m_disconnect != nullptr)
		m_disconnect(m_base, connection);
}

namespace {

// A request to the base, by this connection — the one door both connections send through.
bool ExchangeWith(const ibProtocolFileBase& base, const void* connection, const ibString& request, ibString& answer,
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	if (!base.Call(request, connection, ibString(), answer, error)) {
		refusal = ibProtocol::ibRefusal::Failed;
		if (error.IsEmpty())
			error = wxT("no base is open");
		return false;
	}
	return true;
}

// THE SECOND CONNECTION to a base another connection opened — its own to the base's clients, the base not its to close.
class ibProtocolConnectionFileChannel : public ibProtocolConnection {
public:

	explicit ibProtocolConnectionFileChannel(std::shared_ptr<ibProtocolFileBase> base) : m_base(std::move(base)) {}
	virtual ~ibProtocolConnectionFileChannel() { m_base->Disconnect(this); }

	virtual bool Exchange(const ibString& request, ibString& answer,
		ibProtocol::ibRefusal& refusal, wxString& error) override
	{
		return ExchangeWith(*m_base, this, request, answer, refusal, error);
	}
	virtual void Listen(std::function<void(const ibString& text)> notified) override
	{
		m_base->Listen(this, std::move(notified));
	}

private:
	std::shared_ptr<ibProtocolFileBase> m_base;
};

} // namespace

bool ibProtocolConnectionFile::Open(const ibString& request, wxString& error)
{
	return m_base->Open(request, error);
}

void ibProtocolConnectionFile::Close()
{
	m_base->Close();
}

ibProtocolConnectionFile::~ibProtocolConnectionFile()
{
	Close();
}

bool ibProtocolConnectionFile::Exchange(const ibString& request, ibString& answer,
	ibProtocol::ibRefusal& refusal, wxString& error)
{
	return ExchangeWith(*m_base, this, request, answer, refusal, error);
}

void ibProtocolConnectionFile::Listen(std::function<void(const ibString& text)> notified)
{
	m_base->Listen(this, std::move(notified));
}

std::unique_ptr<ibProtocolConnection> ibProtocolConnectionFile::OpenChannel(wxString& /*error*/)
{
	return std::make_unique<ibProtocolConnectionFileChannel>(m_base);
}
