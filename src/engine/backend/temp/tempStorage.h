#ifndef __TEMP_STORAGE_H__
#define __TEMP_STORAGE_H__

/////////////////////////////////////////////////////////////////////////////
// ibTempStorage — THE SESSION'S TEMPORARY STORAGE, its temporary folder: the files a client handed the server, and
// the ones the server made for it, kept in the sys_file app-table while the session lives. A file is named by its
// unique ID — a document is given the id where the desktop gave it a path (ibDocument::DoOpenDocument), and the
// storage knows the rest: the name the file came under, whose extension says which template reads it.
//
// ⭐ FIRST THE ID, THEN THE CONTENT. Create(name) makes the file and answers its id; the content is written into it
// afterwards — a client's upload, a document saving.
//
// ⭐ IN PARTS, never whole. A file is its header row (part 0: the name, the owner) and a run of content parts, each of
// at most kPartSize bytes, written as they arrive and read one at a time — a gigabyte passes through without a
// gigabyte in memory (ibTempFileReader / ibTempFileWriter, streams over the parts).
//
// ⭐ THE SESSION'S, and only its. Every call names the session, and a file another session made is not there for it.
// The files go with the session — at its end (OnSessionEnd, from the registry's removal, beside the lock manager's)
// and, for a session that died without one, at the sweep (SweepOrphans, handed who is alive). Nothing is kept by a
// clock.
//
// OWNED BY ibApplicationInstance, like the lock manager and the settings storage — reached through
// ibApplicationInstance::GetTempStorage() (nullptr pre-appData / post-appData; callers must null-check).
/////////////////////////////////////////////////////////////////////////////

#include "backend/appDataCtorToken.h"
#include "backend/databaseLayer/connectionHolder.h"
#include "core/guid.h"

#include <cstddef>
#include <mutex>
#include <streambuf>
#include <vector>

#include <wx/buffer.h>
#include <wx/convauto.h>
#include <wx/string.h>

class BACKEND_API ibTempStorage {
public:

	// The most a content part holds: what a client sends at a time, and where a writer cuts.
	static constexpr std::size_t kPartSize = 1024 * 1024;

	explicit ibTempStorage(ib::AppDataCtorToken owner);
	~ibTempStorage();

	// A NEW FILE of the session, named `name` (without any folder it came with), empty — its id. Empty: no name, or
	// the write failed.
	wxString Create(const ibGuid& session, const wxString& name);
	// The name the session's file was made under. Empty: the session has no such file.
	wxString GetName(const ibGuid& session, const wxString& id) const;

	// THE CONTENT PART `index` (from 0) of the session's file. Written in order; the next one is PartCount().
	// False: the part is over kPartSize, or is there already, or the write failed (journalled).
	bool WritePart(const ibGuid& session, const wxString& id, int index, const void* data, std::size_t size);
	// The content part `index`, into `data`. False: past the last one, or no such file.
	bool ReadPart(const ibGuid& session, const wxString& id, int index, wxMemoryBuffer& data) const;
	// How many content parts the session's file has.
	int PartCount(const ibGuid& session, const wxString& id) const;
	// The content gone, the file kept — before it is written anew.
	bool Clear(const ibGuid& session, const wxString& id);

	// THE SESSION GONE — its files with it. Called by the registry as it removes the session.
	void OnSessionEnd(const ibGuid& session);
	// The files of every session not in `liveSessionGuids` — the ones that died without an end.
	void SweepOrphans(const std::vector<ibGuid>& liveSessionGuids);

private:

	ibTempStorage(const ibTempStorage&) = delete;
	ibTempStorage& operator=(const ibTempStorage&) = delete;

	// Its own connection, as the lock manager has: a part must commit when it is written, whatever transaction
	// the caller's session has open, and the cleanup runs on the registry's thread, where no session is.
	mutable ibSingleConnectionHolder m_holder;
	// The holder does not serialise the threads that ask it — this does (see ibLockManager::m_mtx). Recursive:
	// SweepOrphans ends each orphan through OnSessionEnd.
	mutable std::recursive_mutex m_mtx;
};

// A FILE OF THE CURRENT SESSION'S TEMPORARY STORAGE, as wxFile is one on disk — what a document opens and saves by
// the id it was given where the desktop's opened and saved a path. The session is the one working, asked here.
class BACKEND_API ibTempFile {
public:

	ibTempFile() = default;
	explicit ibTempFile(const wxString& id) { Open(id); }

	// For reading. False: the current session has no such file.
	bool Open(const wxString& id);
	// For writing, its content anew. False: the current session has no such file — its id comes first
	// (ibTempStorage::Create) — or it has content and `overwrite` is not set.
	bool Create(const wxString& id, bool overwrite = false);
	bool IsOpened() const { return m_storage != nullptr; }

	// The name it was made under.
	wxString GetName() const;

	// The whole content, through `conv` — for a document that holds its text whole anyway.
	bool ReadAll(wxString* str, const wxMBConv& conv = wxConvAuto());
	// The content part `index`, from 0. False past the last.
	bool ReadPart(int index, wxMemoryBuffer& data) const;

	// `str` through `conv`, as the next content parts.
	bool Write(const wxString& str, const wxMBConv& conv = wxConvAuto());
	// Bytes as the next content parts, at most kPartSize each.
	bool Write(const void* data, std::size_t size);

private:

	ibTempStorage* m_storage = nullptr;
	ibGuid         m_session;
	wxString       m_id;
	int            m_written = 0;   // content parts written since Create
};

// A TEMPORARY FILE READ AS A STREAM — one part in memory at a time, the next fetched when this one is read.
class ibTempFileReader : public std::streambuf {
public:

	explicit ibTempFileReader(const ibTempFile& file) : m_file(file) {}

protected:

	virtual int_type underflow() override {
		// An empty part is a part, so it is passed over, not taken for the end.
		while (gptr() == egptr()) {
			if (!m_file.ReadPart(m_index, m_part))
				return traits_type::eof();
			++m_index;
			char* const begin = static_cast<char*>(m_part.GetData());
			setg(begin, begin, begin + m_part.GetDataLen());
		}
		return traits_type::to_int_type(*gptr());
	}

private:

	const ibTempFile& m_file;
	int               m_index = 0;
	wxMemoryBuffer    m_part;
};

// A TEMPORARY FILE WRITTEN AS A STREAM — a part goes down every kPartSize bytes, and Finish() writes the rest.
class ibTempFileWriter : public std::streambuf {
public:

	explicit ibTempFileWriter(ibTempFile& file) : m_file(file), m_buffer(ibTempStorage::kPartSize) {
		setp(m_buffer.data(), m_buffer.data() + m_buffer.size());
	}

	// What is left, as the last part. False: a part did not go down.
	bool Finish() { return WriteBuffered(); }

protected:

	virtual int_type overflow(int_type ch) override {
		if (!WriteBuffered())
			return traits_type::eof();
		if (!traits_type::eq_int_type(ch, traits_type::eof())) {
			*pptr() = traits_type::to_char_type(ch);
			pbump(1);
		}
		return traits_type::not_eof(ch);
	}

private:

	bool WriteBuffered() {
		const std::size_t size = static_cast<std::size_t>(pptr() - pbase());
		if (m_ok && size != 0)
			m_ok = m_file.Write(pbase(), size);
		setp(m_buffer.data(), m_buffer.data() + m_buffer.size());
		return m_ok;
	}

	ibTempFile&       m_file;
	std::vector<char> m_buffer;
	bool              m_ok = true;
};

#endif
