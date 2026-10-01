////////////////////////////////////////////////////////////////////////////
//	Description : runtime diagnostics kept in the registration journal
////////////////////////////////////////////////////////////////////////////

#include "backend/logger/loggerDiagnosticSink.h"
#include "backend/logger/logger.h"

ibLoggerDiagnosticSink::ibLoggerDiagnosticSink(ibLogger* logger)
	: m_logger(logger)
{
	ibDiagnostics::Subscribe(this);
}

ibLoggerDiagnosticSink::~ibLoggerDiagnosticSink()
{
	ibDiagnostics::Unsubscribe(this);
}

void ibLoggerDiagnosticSink::OnDiagnostic(const ibDiagnostic& diagnostic)
{
	if (m_logger == nullptr)
		return;

	// See the header: what ran and failed is an event; what is being typed is not.
	if (diagnostic.m_kind != ibDiagnosticKind::Runtime)
		return;

	// The message is assembled the way the runtime already shows it, because that
	// is the form a person recognises when they see the same failure twice —
	// module and line first, then what went wrong, then the line that did it.
	wxString message;
	message << wxT("{") << diagnostic.m_moduleName;
	if (diagnostic.m_line > 0)
		message << wxT("(") << (int)diagnostic.m_line << wxT(")");
	message << wxT("}: ") << diagnostic.m_message;

	if (!diagnostic.m_codeLine.IsEmpty())
		message << wxT("\n") << diagnostic.m_codeLine;

	// THE STACK IS THE HALF THAT SAYS HOW IT GOT THERE. A posting module that
	// fails through three calls is not diagnosable from its own last line.
	if (!diagnostic.m_stack.empty()) {
		message << wxT("\nCall stack:");
		for (std::size_t index = 0; index < diagnostic.m_stack.size(); ++index) {
			message << wxString::Format(wxT("\n%i: %s (#line %d)"),
				static_cast<int>(index) + 1,
				diagnostic.m_stack[index].m_module,
				diagnostic.m_stack[index].m_line);
		}
	}

	// An external report carries its file; a module inside the configuration
	// carries nothing there, and the empty field is the difference.
	if (!diagnostic.m_fileName.IsEmpty())
		message << wxT("\nfile: ") << diagnostic.m_fileName;

	// m_docPath is the module's guid — the one identifier that survives a rename,
	// and the only one a reader can navigate by. One row, not two: an error and
	// a note about the same error are the same event.
	if (!diagnostic.m_docPath.IsEmpty())
		message << wxT("\nmodule: ") << diagnostic.m_docPath;

	m_logger->Error(wxT("script"), wxT("runtime.error"), message);
}
