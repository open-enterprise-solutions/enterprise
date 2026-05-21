/////////////////////////////////////////////////////////////////////////////
// basCfReader — 1С / BAS binary .cf archive reader.
//
// STATUS: DEFERRED. .cf is a proprietary 1С container (custom LZF-style
// compression + Microsoft cab-derived directory layout). A reliable
// unpacker requires either:
//   a) Reverse-engineering the LZF dictionary (community projects exist
//      but no battle-tested C++ port we trust); or
//   b) Calling out to the 1С Configurator CLI to unpack into XML first
//      (preferred — user can already do this and feed XML to
//      import_bas_xml).
//
// v1 contract: detect the .cf magic header, return DeferredUnsupported
// with a structured-error code OES_E_BAS_CF_UNSUPPORTED + actionable
// guidance. Future work can replace the body without changing the
// interface.
/////////////////////////////////////////////////////////////////////////////

#ifndef _IB_BAS_CF_READER_HPP_
#define _IB_BAS_CF_READER_HPP_

#include "basXmlReader.hpp"

namespace migration {
namespace bas {

enum class CfStatus {
	Unsupported,       // binary unpacker not implemented (current state)
	NotACfFile,        // header magic didn't match
	FileMissing,
	Ok                 // (reserved for future unpack-success path)
};

struct CfReadResult {
	CfStatus status = CfStatus::Unsupported;
	wxString errorCode;
	wxString message;
	ImportResult import;   // populated only when status == Ok
};

// Read a .cf archive at `path`. Returns CfReadResult with status, error
// code (suitable for an MCP error envelope), and a human-readable
// message.
CfReadResult ReadCfArchive(const wxString& path);

} // namespace bas
} // namespace migration

#endif // _IB_BAS_CF_READER_HPP_
