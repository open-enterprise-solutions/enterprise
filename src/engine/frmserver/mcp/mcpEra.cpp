////////////////////////////////////////////////////////////////////////////
//	Description : ibMcpEra — the two eras of the MCP protocol, each in its own words
////////////////////////////////////////////////////////////////////////////

#include "frmserver/mcp/mcpEra.h"

#include "core/build.h"   // GetBuildId — the build a server names itself by

#include <algorithm>

namespace {

// Where a modern request names its revision.
const ibChar* const kMetaVersion = ibT("io.modelcontextprotocol/protocolVersion");

const ibMcpLegacyEra s_legacy{};
const ibMcpModernEra s_modern{};

// A refusal, its code handed out as the server hands its own (outErrorCode).
ibString Refuse(const ibDataValue& id, ibRpcError code, const ibString& text, int* outErrorCode,
	const ibDataNode* data = nullptr)
{
	if (outErrorCode != nullptr)
		*outErrorCode = static_cast<int>(code);
	return ibRpcWriteError(id, code, text, data);
}

bool Names(const std::vector<ibString>& versions, const ibString& version)
{
	return std::find(versions.begin(), versions.end(), version) != versions.end();
}

} // namespace

//---------------------------------------------------------------------------
// ibMcpEra
//---------------------------------------------------------------------------

const ibMcpEra& ibMcpEra::Of(const ibRpcRequest& request)
{
	// Nothing else is consulted: how the request came is what it is.
	if (auto meta = request.m_params.FindChild(ibT("_meta")))
		if (auto version = meta->FindField(kMetaVersion); version && version->Kind() == ibDataKind::String)
			return s_modern;

	return s_legacy;
}

bool ibMcpEra::Speaks(const ibString& version)
{
	return Names(s_modern.GetVersions(), version) || Names(s_legacy.GetVersions(), version);
}

std::vector<ibDataValue> ibMcpEra::SupportedVersionList()
{
	std::vector<ibDataValue> versions;
	for (const ibMcpEra* era : { static_cast<const ibMcpEra*>(&s_modern), static_cast<const ibMcpEra*>(&s_legacy) })
		for (const ibString& version : era->GetVersions())
			versions.push_back(ibDataValue::String(version));

	return versions;
}

ibDataNode ibMcpEra::SupportedVersions(const ibString& asked)
{
	ibDataNode data;
	data.AddField(ibT("supported"), ibDataValue::Array(SupportedVersionList()));
	if (!asked.IsEmpty())
		data.SetValue(ibT("requested"), asked);

	return data;
}

void ibMcpEra::FillServerInfo(ibDataNode& info)
{
	info.SetValue(ibT("name"), ibString(ibT("OES Enterprise")));
	info.SetValue(ibT("version"), ibString::Format(ibT("build %u"), GetBuildId()));
}

//---------------------------------------------------------------------------
// ibMcpLegacyEra
//---------------------------------------------------------------------------

const std::vector<ibString>& ibMcpLegacyEra::GetVersions() const
{
	static const std::vector<ibString> s_versions = {
		ibT("2025-11-25"),
		ibT("2025-06-18"),
		ibT("2025-03-26"),
	};
	return s_versions;
}

ibString ibMcpLegacyEra::Admit(const ibRpcRequest& /*request*/, const ibMcpWireHeaders& /*headers*/,
	int* /*outErrorCode*/) const
{
	// NOTHING BEYOND WHAT THE TRANSPORT ASKED (ibMcpServer::Call): a header naming a revision this server speaks, or
	// none — a client older than 2025-06-18 sends none, read as 2025-03-26. The handshake agrees the rest.
	return ibEmptyString;
}

void ibMcpLegacyEra::Complete(ibDataNode& /*result*/) const
{
	// A legacy result is written as it always was.
}

void ibMcpLegacyEra::FillCacheable(ibDataNode& /*result*/) const
{
	// The legacy era says nothing of how long an answer may be kept.
}

//---------------------------------------------------------------------------
// ibMcpModernEra
//---------------------------------------------------------------------------

const std::vector<ibString>& ibMcpModernEra::GetVersions() const
{
	static const std::vector<ibString> s_versions = {
		ibT("2026-07-28"),
	};
	return s_versions;
}

ibString ibMcpModernEra::Admit(const ibRpcRequest& request, const ibMcpWireHeaders& headers, int* outErrorCode) const
{
	ibString asked;
	if (auto meta = request.m_params.FindChild(ibT("_meta")))
		if (auto version = meta->FindField(kMetaVersion); version && version->Kind() == ibDataKind::String)
			asked = version->AsString();

	// THE HEADERS AND THE BODY MUST AGREE. Two readers, two sources: an intermediary routes on the header, this
	// server executes on the body. A request where they differ is the one that gets past the first and is carried
	// out by the second.
	if (!headers.m_protocolVersion.IsEmpty() && headers.m_protocolVersion != asked) {
		return Refuse(request.m_id, ibRpcError::HeaderMismatch,
			ibString::Format(ibT("MCP-Protocol-Version header says '%s' and the body says '%s'"),
				headers.m_protocolVersion, asked), outErrorCode);
	}
	if (!headers.m_method.IsEmpty() && headers.m_method != request.m_method) {
		return Refuse(request.m_id, ibRpcError::HeaderMismatch,
			ibString::Format(ibT("Mcp-Method header says '%s' and the body calls '%s'"),
				headers.m_method, request.m_method), outErrorCode);
	}

	// …AND SO MUST THE NAME, where the method names one: a tool, a prompt.
	if (!headers.m_name.IsEmpty()
		&& (request.m_method == ibT("tools/call") || request.m_method == ibT("prompts/get"))) {
		const ibString named = request.m_params.GetValue<ibString>(ibT("name"));
		if (headers.m_name != named) {
			return Refuse(request.m_id, ibRpcError::HeaderMismatch,
				ibString::Format(ibT("Mcp-Name header says '%s' and the body names '%s'"),
					headers.m_name, named), outErrorCode);
		}
	}

	// AND THE REVISION MUST BE ITS OWN — answered with the list, so the caller can pick another rather than work out
	// why the replies look wrong. A legacy revision named in `_meta` is neither era: those are opened by
	// `initialize`, never by metadata.
	if (!Names(GetVersions(), asked)) {
		const ibDataNode data = SupportedVersions(asked);
		return Refuse(request.m_id, ibRpcError::UnsupportedVersion, ibT("Unsupported protocol version"), outErrorCode, &data);
	}

	return ibEmptyString;
}

void ibMcpModernEra::Complete(ibDataNode& result) const
{
	// WHAT IT IS AND WHO GAVE IT: `resultType` — "complete", since this server never answers `input_required` —
	// and the server's identity, which the handshake used to say once and every result says now.
	result.SetValue(ibT("resultType"), ibString(ibT("complete")));
	FillServerInfo(result.Child(ibT("_meta")).Child(ibT("io.modelcontextprotocol/serverInfo")));
}

void ibMcpModernEra::FillCacheable(ibDataNode& result) const
{
	// PRIVATE: what is offered follows the person's rights, so one caller's answer is not another's. FIVE MINUTES:
	// the list changes with a new build or a change of rights, neither of which happens mid-sentence.
	result.AddField(ibT("ttlMs"), ibDataValue::Int(300000));
	result.SetValue(ibT("cacheScope"), ibString(ibT("private")));
}
