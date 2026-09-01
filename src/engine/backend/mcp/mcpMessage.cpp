////////////////////////////////////////////////////////////////////////////
//	Description : reading and writing the JSON-RPC envelope
////////////////////////////////////////////////////////////////////////////

#include "backend/mcp/mcpMessage.h"

#include "backend/backend_exception.h"
#include "backend/fileSystem/fs.h"            // ibReaderMemory / ibWriterMemory
#include "backend/serialize/jsonProvider.h"

wxString ibMcpRenderNode(const ibDataNode& node, const std::function<wxString(ibClassID)>& typeResolver)
{
	ibJsonProvider provider;
	if (typeResolver)
		provider.SetTypeResolver(typeResolver);

	ibWriterMemory writer;
	if (!provider.Write(node, writer))
		return wxEmptyString;

	return wxString::FromUTF8(reinterpret_cast<const char*>(writer.pointer()), writer.size());
}

namespace {

wxString EmitNode(const ibDataNode& node) { return ibMcpRenderNode(node); }

// The envelope every answer shares. Written by hand rather than by a helper
// object because there are exactly two shapes and both are three lines.
ibDataNode Envelope(const ibDataValue& id)
{
	ibDataNode root;
	root.SetValue(wxT("jsonrpc"), wxString(wxT("2.0")));
	// The id goes back VERBATIM — a number stays a number, a string stays a
	// string. Rendering it through a type of our choosing would break the one
	// thing the protocol uses to pair an answer with its call.
	root.AddField(wxT("id"), id);
	return root;
}

} // namespace

bool ibMcpParseRequest(const wxString& text, ibMcpRequest& request, wxString& error)
{
	const wxScopedCharBuffer utf8 = text.utf8_str();

	// ⚠ A READER BORROWS ITS BYTES (fileSystem/fs.h): the buffer must outlive it,
	// so it is a named local and not a temporary in the call.
	wxMemoryBuffer buffer;
	buffer.AppendData(utf8.data(), utf8.length());

	ibDataNode root;

	try {
		ibReaderMemory reader(buffer);
		ibJsonProvider provider;
		if (!provider.Read(reader, root)) {
			error = wxT("the request could not be read as JSON");
			return false;
		}
	}
	catch (const ibBackendException& e) {
		// The parser throws with a byte offset — the most useful half of the
		// answer, so it is passed on rather than replaced with a summary.
		error = e.what();
		return false;
	}
	catch (...) {
		error = wxT("the request could not be read as JSON");
		return false;
	}

	// A scalar key lands in the FIELD area; `params`, being an object, lands in
	// the properties area as a Child.
	request.m_method = root.GetValue<wxString>(wxT("method"));
	if (request.m_method.IsEmpty()) {
		error = wxT("no method named");
		return false;
	}

	if (const ibDataValue* id = root.FindField(wxT("id")))
		request.m_id = *id;

	if (const ibDataNode* params = root.FindChild(wxT("params")))
		request.m_params = *params;

	return true;
}

bool ibMcpParseResponse(const wxString& text, ibDataValue& id, wxString& payload, bool& isError)
{
	const wxScopedCharBuffer utf8 = text.utf8_str();

	wxMemoryBuffer buffer;
	buffer.AppendData(utf8.data(), utf8.length());

	ibDataNode root;

	try {
		ibReaderMemory reader(buffer);
		ibJsonProvider provider;
		if (!provider.Read(reader, root))
			return false;
	}
	catch (...) {
		return false;
	}

	// ONE FACT TELLS THEM APART: a request names a method, an answer does not.
	if (!root.GetValue<wxString>(wxT("method")).IsEmpty())
		return false;

	const ibDataNode* result = root.FindChild(wxT("result"));
	const ibDataNode* failed = root.FindChild(wxT("error"));

	if (result == nullptr && failed == nullptr)
		return false;   // neither a call nor an answer — not ours to read

	if (const ibDataValue* found = root.FindField(wxT("id")))
		id = *found;

	isError = (failed != nullptr);
	payload = ibMcpRenderNode(isError ? *failed : *result);
	return true;
}

wxString ibMcpWriteResult(const ibDataValue& id, const ibDataNode& result)
{
	ibDataNode root = Envelope(id);
	root.Child(wxT("result")) = result;
	return EmitNode(root);
}

wxString ibMcpWriteError(const ibDataValue& id, ibMcpError code, const wxString& message)
{
	ibDataNode root = Envelope(id);

	ibDataNode& error = root.Child(wxT("error"));
	// Built as a VALUE, not through SetValue<T>: the node's codecs cover a fixed
	// set (wxString / bool / s32 / buffer / guid / ibNumber / date) and a 64-bit
	// integer is not one of them.
	error.AddField(wxT("code"), ibDataValue::Int((s64)code));
	error.SetValue(wxT("message"), message);

	return EmitNode(root);
}
