#include "connectionFile.h"

#include "core/diagnostics/journal.h"
#include "core/fiber/fiberLocals.h"
#include "core/programFolder.h"

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
		// What fileserver registers as it loads is refused. The library is
		// not kept: a slot taken during load would be missing from fibers
		// that are already parked.
		ibFiberLocals::ModuleLoadScope loading(from);
		if (!library.Load(from))
			return wxDllType(nullptr);
		if (loading.Refused()) {
			if (ibTechJournal::IsOpen())
				ibTechJournal::Print(ibJournalMark::Error, wxT("fileserver"),
					wxT("The file base library did not load: it registered a fiber local (%s)"), from);
			return wxDllType(nullptr);
		}
		return library.Detach();
	}(path);
	return s_library;
}

} // namespace

bool ibProtocolConnectionFile::Open(const std::string& request, wxString& error)
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
	const auto close = reinterpret_cast<decltype(&ibFileBaseClose)>(
		wxDynamicLibrary::RawGetSymbol(library, wxT("ibFileBaseClose")));
	if (open == nullptr || call == nullptr || listen == nullptr || close == nullptr) {
		error = wxString::Format(_("%s is not the file base library this client was built with."), path);
		return false;
	}

	m_base = open(request, error);
	if (m_base == nullptr)
		return false;
	m_call = call;
	m_listen = listen;
	m_close = close;
	return true;
}

void ibProtocolConnectionFile::Close()
{
	if (m_base != nullptr && m_close != nullptr)
		m_close(m_base);
	m_base = nullptr;
	m_call = nullptr;
	m_listen = nullptr;
	m_close = nullptr;
}

ibProtocolConnectionFile::~ibProtocolConnectionFile()
{
	Close();
}

bool ibProtocolConnectionFile::Exchange(const std::string& request, std::string& answer,
	ibProtocolRefusal& refusal, wxString& error)
{
	if (m_base == nullptr || m_call == nullptr || !m_call(m_base, request, answer, error)) {
		refusal = ibProtocolRefusal::Failed;
		if (error.IsEmpty())
			error = wxT("no base is open");
		return false;
	}
	return true;
}

void ibProtocolConnectionFile::Listen(std::function<void(const std::string& text)> notified)
{
	if (m_base != nullptr && m_listen != nullptr)
		m_listen(m_base, std::move(notified));
}
