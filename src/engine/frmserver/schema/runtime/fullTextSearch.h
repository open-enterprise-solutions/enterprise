#ifndef __SCHEMA_FULL_TEXT_SEARCH_H__
#define __SCHEMA_FULL_TEXT_SEARCH_H__

// THE BASE'S FULL-TEXT SEARCH, as a person reaches it with the mouse - the runtime's two windows over ibFullTextIndex:
// Administration → Full-text search, where the index is switched on and built, and Find, where it is searched. The
// script reaches the same through the global FullTextSearch; both ask the same engine, so a window shows what a
// script would answer.

#include "frmserver/schema/clientSchema.h"

// ADMINISTRATION: the mode, whether the index is true, when the build last completed, how far it is - and the three
// things a person does there: switch the mode, build one portion, clear the index. The window builds in portions,
// so a person can stop between them and is told how far the build got.
class FRMSERVER_API ibSchemaFullTextSearch : public ibClientSchema {
public:

	// Data administration: the index is the base's.
	virtual bool AccessRight(const ibSession& session) const override;

	// Enabled, IndexTrue, Updated, Indexed, Total.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Set {Enabled}, Start - one portion, Clear. Anything else is refused.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;
};

// FIND: the records holding every word of a text, among those the person may read - each with its metaobject, its
// presentation and its key; Total counts them after the rights filter. Open {MetaId, Key} opens one in its form.
class FRMSERVER_API ibSchemaSearch : public ibClientSchema {
public:

	// Enabled; for Args {Text}, Hits and Total.
	virtual void Build(const ibSession& session, const ibDataNode& args, ibDataNode& result) const override;

	// Open {MetaId, Key} - a record the search offered this person.
	virtual std::function<void()> Command(const ibSession& session, std::int32_t command,
		const ibDataNode& args, ibProtocol::ibRefusal& refusal, ibString& error) const override;

	// The hits a window lists at most; Total says how many there are.
	static constexpr int kShown = 200;
};

#endif
