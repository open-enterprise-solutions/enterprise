#include "connectionFile.h"

#include "core/dynamicLibrary.h"
#include "core/fileSystem.h"

#include "core/localization/dictionary.h"   // _() — what a refusal says to a person

namespace {

// fileserver beside the program — loaded once and never unloaded: what the server registers in the backend as it loads
// (frmserver's forms and controls) must outlive every base, and an unloading at the process's end would run after the
// statics it unregisters from (the 2026-10-07 exit crash, codeRunner) — so the library is never deleted. Null when it
// could not be loaded; `path` — where it was looked for.
const ibDynamicLibrary* Library(ibString& path)
{
	// Beside the program. Inside enterprise-thin.app that is next to the bundle, where
	// libfileserver.dylib is shipped — not Contents/MacOS/, which is only the binary.
	path = ibProgramFolder() + ibFILE_SEP_PATH
		+ ibDynamicLibrary::CanonicalizeName(ibT("fileserver"));
	static const ibDynamicLibrary* const s_library = [](const ibString& from) {
		ibDynamicLibrary* const library = new ibDynamicLibrary;
		library->Load(from);
		return library;
	}(path);
	return s_library->IsLoaded() ? s_library : nullptr;
}

} // namespace

bool ibProtocolFileBase::Open(const ibString& request, ibString& error)
{
	Close();

	ibString path;
	const ibDynamicLibrary* const library = Library(path);
	if (library == nullptr) {
		error = ibString::Format(_("The file base library could not be loaded: %s"), path);
		return false;
	}
	const auto open = reinterpret_cast<decltype(&ibFileBaseOpen)>(library->GetSymbol(ibT("ibFileBaseOpen")));
	const auto call = reinterpret_cast<decltype(&ibFileBaseCall)>(library->GetSymbol(ibT("ibFileBaseCall")));
	const auto listen = reinterpret_cast<decltype(&ibFileBaseListen)>(library->GetSymbol(ibT("ibFileBaseListen")));
	const auto disconnect = reinterpret_cast<decltype(&ibFileBaseDisconnect)>(
		library->GetSymbol(ibT("ibFileBaseDisconnect")));
	const auto close = reinterpret_cast<decltype(&ibFileBaseClose)>(library->GetSymbol(ibT("ibFileBaseClose")));
	if (open == nullptr || call == nullptr || listen == nullptr || disconnect == nullptr || close == nullptr) {
		error = ibString::Format(_("%s is not the file base library this program was built with."), path);
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

bool ibProtocolFileBase::Call(const ibMemoryBuffer& request, const void* connection, const ibString& address,
	ibMemoryBuffer& answer, ibString& error) const
{
	if (m_base == nullptr || m_call == nullptr) {
		error = ibT("no base is open");
		return false;
	}
	return m_call(m_base, request, connection, address, answer, error);
}

void ibProtocolFileBase::Listen(const void* connection, std::function<void(const ibMemoryBuffer& packet)> notified) const
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

// A request to the base, by this connection — the one door both connections send through; the packet the base gave
// checked once, here.
bool ExchangeWith(const ibProtocolFileBase& base, const void* connection, const ibMemoryBuffer& request,
	ibProtocolNode& answer, ibProtocol::ibRefusal& refusal, ibString& error)
{
	ibMemoryBuffer packet;
	if (!base.Call(request, connection, ibString(), packet, error)) {
		refusal = ibProtocol::ibRefusal::Failed;
		if (error.IsEmpty())
			error = ibT("no base is open");
		return false;
	}
	if (!ibProtocolNode::Read(packet, answer)) {
		refusal = ibProtocol::ibRefusal::Failed;
		error = ibT("the base's answer is not a packet");
		return false;
	}
	return true;
}

// …and what it says unasked, by this connection — the node each notification's packet is; one that is none is told
// to nobody.
void ListenWith(const ibProtocolFileBase& base, const void* connection,
	std::function<void(const ibProtocolNode& said)> notified)
{
	if (!notified) {
		base.Listen(connection, nullptr);
		return;
	}
	base.Listen(connection, [notified = std::move(notified)](const ibMemoryBuffer& packet) {
		ibProtocolNode said;
		if (ibProtocolNode::Read(packet, said))
			notified(said);
	});
}

// THE SECOND CONNECTION to a base another connection opened — its own to the base's clients, the base not its to close.
class ibProtocolConnectionFileChannel : public ibProtocolConnection {
public:

	explicit ibProtocolConnectionFileChannel(std::shared_ptr<ibProtocolFileBase> base) : m_base(std::move(base)) {}
	virtual ~ibProtocolConnectionFileChannel() { m_base->Disconnect(this); }

	virtual bool Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer,
		ibProtocol::ibRefusal& refusal, ibString& error) override
	{
		return ExchangeWith(*m_base, this, request, answer, refusal, error);
	}
	virtual void Listen(std::function<void(const ibProtocolNode& said)> notified) override
	{
		ListenWith(*m_base, this, std::move(notified));
	}

private:
	std::shared_ptr<ibProtocolFileBase> m_base;
};

} // namespace

bool ibProtocolConnectionFile::Open(const ibString& request, ibString& error)
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

bool ibProtocolConnectionFile::Exchange(const ibMemoryBuffer& request, ibProtocolNode& answer,
	ibProtocol::ibRefusal& refusal, ibString& error)
{
	return ExchangeWith(*m_base, this, request, answer, refusal, error);
}

void ibProtocolConnectionFile::Listen(std::function<void(const ibProtocolNode& said)> notified)
{
	ListenWith(*m_base, this, std::move(notified));
}

std::unique_ptr<ibProtocolConnection> ibProtocolConnectionFile::OpenChannel(ibString& /*error*/)
{
	return std::make_unique<ibProtocolConnectionFileChannel>(m_base);
}
