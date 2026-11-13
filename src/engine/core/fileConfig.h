#ifndef __CORE_FILE_CONFIG_H__
#define __CORE_FILE_CONFIG_H__

#include "core/core.h"
#include "core/fstring.h"

#include <vector>

////////////////////////////////////////////////////////////////////////////
// ibFileConfig - wxFileConfig (wx/fileconf.h), as the engine reads and writes its .conf files, on ibString and std.
////////////////////////////////////////////////////////////////////////////
//
// An INI file: `[group]` lines, `key=value` lines, `;` and `#` comments. It is kept LINE BY LINE, as wx kept it, so
// Flush writes back what a person wrote - their comments, their order - with only the values written here changed.
//
// A key is a path: "Port" or "/Port" at the top of the file, "trade/Path" (or "/trade/Path") in the group [trade].
// Names are compared without their case, as wx compared them.
//
// A value is read as it is written - no escapes, wx's wxCONFIG_USE_NO_ESCAPE_CHARACTERS, the one style the engine
// used: a Windows path keeps its backslashes - from the first character after `=` that is not a blank to the end of
// the line, blanks at its end included. Its environment variables are expanded, as wxConfigBase expanded them:
// $NAME, ${NAME} and $(NAME) everywhere, %NAME% on Windows; a backslash before `$` or `%` keeps the character as it
// is, and a variable that is not set stays as written.
//
// A file that is not there is an empty one; Flush makes it.
//
////////////////////////////////////////////////////////////////////////////

class CORE_API ibFileConfig {
public:
	explicit ibFileConfig(const ibString& path);

	// The value of `key`: false, and `value` left as it was, when the file does not say it.
	bool Read(const ibString& key, ibString* value) const;
	// ...the value, or `byDefault` when the file does not say it.
	ibString Read(const ibString& key, const ibString& byDefault = ibString()) const;
	// ...a number: false, and `value` left as it was, when the file does not say it or says something else.
	bool Read(const ibString& key, long* value) const;
	// ...and `byDefault` in `value` then.
	bool Read(const ibString& key, long* value, long byDefault) const;

	// The value of `key`, replaced where the file says it, else added to its group - and the group to the end of the
	// file when it has none. On disk at Flush.
	bool Write(const ibString& key, const ibString& value);
	bool Write(const ibString& key, long value);

	// The lines written back, when anything was written; false when the file could not be.
	bool Flush();

	// The groups at the top of the file, in its order, one at a time: `cookie` is where the walk is.
	bool GetFirstGroup(ibString& group, long& cookie) const;
	bool GetNextGroup(ibString& group, long& cookie) const;

private:
	// The line `key` is said on; -1 when none.
	long FindEntry(const ibString& group, const ibString& name) const;

	ibString              m_path;
	std::vector<ibString> m_lines;   // the file as it was read, a line each, without its end
	bool                  m_dirty = false;
};

#endif // __CORE_FILE_CONFIG_H__
