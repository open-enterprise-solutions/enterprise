#ifndef __IB_LOGGER_DIAGNOSTIC_SINK_H__
#define __IB_LOGGER_DIAGNOSTIC_SINK_H__

#include "backend/backend.h"
#include "backend/backend_diagnostic.h"

class ibLogger;

// THE RUNTIME'S OWN REPORT, WRITTEN DOWN WHERE IT CAN BE READ AGAIN.
//
// A script that fails at execution already says everything about itself: the
// module, the line, the offending text and the whole call stack are published
// as an ibDiagnostic at the single point where a backend error is raised. What
// was missing is that nobody kept it. The message box closes, the technological
// journal keeps it for the platform's own developer, and by the next run it is
// gone.
//
// This is the one subscriber that keeps it. It is a SINK, not a call added at
// the throw sites, because there is already exactly one road: every runtime
// failure passes ibDiagnostics::Publish. Adding writes at the sites would be a
// second road to the same place, and second roads diverge.
//
// COMPILE DIAGNOSTICS ARE DELIBERATELY NOT KEPT. They are the ordinary state of
// a module being edited — an unfinished line is an error until it is finished —
// and journalling them would bury the events that actually happened under the
// events that were merely being typed. What is recorded is what RAN and failed.
class ibLoggerDiagnosticSink : public ibDiagnosticSink {
public:

	explicit ibLoggerDiagnosticSink(ibLogger* logger);
	~ibLoggerDiagnosticSink() override;

	void OnDiagnostic(const ibDiagnostic& diagnostic) override;

private:
	ibLogger* m_logger = nullptr;
};

#endif
