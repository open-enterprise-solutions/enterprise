#ifndef __SCHEMA_STYLE_H__
#define __SCHEMA_STYLE_H__

// STYLE — how a person's interface is painted, as the server holds it: the theme they chose (light, dark, or the one
// their system is in). Kept as the person's, in the base, so it goes with them from one machine to another; the client
// paints itself with it. Shared by every mode. More of the look - colours, fonts, sizes - is to come here.

#include "frmserver/schema/clientSchema.h"

#include "protocol/protocol.h"   // ibProtocol::ibSchema::ibStyle::ibTheme

class ibSession;

class FRMSERVER_API ibSchemaStyle : public ibClientSchema {
public:

	// Theme - the person's; System when they never chose one.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Save {Theme} - the person's from now on. Kept before the answer; refused, nothing kept: no Theme,
	// one that is no theme, or a storage that would not keep it.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;

	// The person's theme, as the storage of their base holds it - System when nothing is stored, or no storage.
	static ibProtocol::ibSchema::ibStyle::ibTheme ReadTheme(const ibSession& session);
};

#endif
