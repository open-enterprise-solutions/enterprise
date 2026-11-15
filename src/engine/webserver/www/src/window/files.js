// A FILE, GOING UP AND DOWN — the thin client's frame's (ibFrontendMainFrame::UploadFile, DownloadFile), over the
// session's calls (docs/public/client-protocol.md; ibProtocol::ibFile): upload {Name, Data} → File, a new temporary
// file of the session's, Data its first part; {File, Data} → its next part; download {File, Part} → Name, Part,
// Parts, Data — the file's part, from 0, of how many there are.

/** @typedef {import('../protocol/session.js').Session} Session */

// A part's size — the thin client's (kUploadPartSize).
const partSize = 1024 * 1024;

/**
 * A file of the person's put in the session's temporary storage, in parts as the server keeps them: the first names
 * the file and is answered its id, each next one names the id; an empty file is its first call with nothing in it.
 * @param {Session} session
 * @param {Blob & {name: string}} file
 * @returns {Promise<string>} its id there
 */
export async function uploadFile(session, file) {
	let id = '';
	for (let offset = 0; ; ) {
		const part = new Uint8Array(await file.slice(offset, offset + partSize).arrayBuffer());
		if (part.length === 0 && offset !== 0)
			return id;
		const answer = await session.call('upload', offset === 0 ? { Name: file.name, Data: part } : { File: id, Data: part });
		id = answer.File;
		offset += part.length;
		if (part.length < partSize)
			return id;
	}
}

/**
 * A file of the server's — the session's temporary one, by its id — read in parts, the first answer saying how many;
 * an empty file is its part 0, empty.
 * @param {Session} session
 * @param {string} file
 * @returns {Promise<File>}
 */
export async function downloadFile(session, file) {
	/** @type {Uint8Array<ArrayBuffer>[]} the packet's bytes are its own buffer's, never a shared one: a file's parts */
	const parts = [];
	let name = file;
	for (let part = 0, count = 1; part < count; ++part) {
		const answer = await session.call('download', { File: file, Part: part });
		count = answer.Parts ?? 0;
		name = answer.Name ?? name;
		if (answer.Data instanceof Uint8Array && answer.Data.length !== 0)
			parts.push(answer.Data);
	}
	return new File(parts, name);
}

/**
 * A file put where the person keeps it: the browser's save dialog where it has one — called from a press, which lets
 * it open — else the browser's download. False when the person cancelled the dialog.
 * @param {(name: string) => Promise<File>} read the file, read once the place is chosen — told the name it was given
 * @param {string} name the name offered
 * @returns {Promise<boolean>}
 */
export async function saveFile(read, name) {
	const picker = /** @type {any} */ (globalThis).showSaveFilePicker;
	if (typeof picker === 'function') {
		let handle;
		try {
			handle = await picker({ suggestedName: name });
		}
		catch {
			return false;
		}
		const writable = await handle.createWritable();
		await writable.write(await read(handle.name));
		await writable.close();
		return true;
	}
	const link = document.createElement('a');
	link.href = URL.createObjectURL(await read(name));
	link.download = name;
	link.click();
	setTimeout(() => URL.revokeObjectURL(link.href), 60000);
	return true;
}

/**
 * The masks a file may be chosen by, as the browser's file input takes them: «*.xlsx;*.xls» → «.xlsx,.xls»; a mask that
 * takes every file — none.
 * @param {string[]} masks
 */
export function acceptOf(masks) {
	const extensions = masks.flatMap((mask) => mask.split(';')).map((one) => one.trim());
	if (extensions.some((one) => one === '*' || one === '*.*' || one === ''))
		return '';
	return extensions.map((one) => one.replace(/^\*/, '')).join(',');
}
