// THE PAGE'S OWN WORDS IN THE PERSON'S LANGUAGE — the thin client's dictionary (lang/<language>/open_es.mo, which the web
// server serves beside the page from the program folder), in the language the login names (Locale) — the browser's
// until then, as the thin client speaks the machine's before it has a base. The page writes its
// words as the thin client writes them — its msgids — so one dictionary says both clients' words, and a word the
// dictionary does not have stays as written. Read before the window is drawn; with none to be had, the page speaks
// English. What the server says (its menus, forms, messages) comes said already.

/** @type {Map<string, string>} */
const words = new Map();
/** the locale the dictionary was read for */
let spoken = '';
/** the language a translated text is read in — the session's, as the server tells it */
let reading = '';

/**
 * The dictionary of a language — the browser's before the login, the one the login names after: `uk_UA` read as
 * `uk_UA`, else as `uk`. None to be had — the words are said as written; no language named — they stay as they are.
 * @param {string} locale
 */
export async function readDictionary(locale) {
	if (locale === '')
		return;
	spoken = locale;
	words.clear();
	for (const language of new Set([locale, locale.split(/[_-]/)[0]])) {
		const answer = await fetch(`lang/${encodeURIComponent(language)}/open_es.mo`).catch(() => null);
		if (answer?.ok) {
			readCatalogue(new DataView(await answer.arrayBuffer()));
			return;
		}
	}
}

/**
 * The language a translated text is read in — a configuration's own text, written in several (`uk = '…'; en = '…';`):
 * the session's, as the server tells it with the frame (its Language — the thin client's SetUserLanguage), which is the
 * one the server reads such a text in too; none told, the page's — `uk` of `uk_UA`.
 */
export function language() {
	return reading || spoken.split(/[_-]/)[0];
}

/**
 * The session's language for a translated text, as the frame names it — before anything is drawn.
 * @param {string} code
 */
export function setLanguage(code) {
	reading = code;
}

/**
 * A word of the page as the person reads it — the thin client's `_()`; with a context, the one the dictionary keeps
 * under it (ibGetTranslation's): «Mon» as a weekday's short name.
 * @param {string} text
 * @param {string} [context]
 */
export function _(text, context) {
	return words.get(context === undefined ? text : `${context}\x04${text}`) || text;
}

/**
 * A label as the thin client's control or menu shows it: its msgid with the letter it underlines (`&New`, `&OK` — wx's
 * stock label as a menu item and a button take it, ibGetStockLabel with the mnemonic), translated so, the mark taken
 * out (wxControl::GetLabelText) — the page underlines none. A stock word's plain msgid is translated apart and may read
 * otherwise (`New` «Новий», `&New` «Створити»; `OK` «OK», `&OK` «Гаразд»). `&&` is an ampersand.
 * @param {string} text
 */
export function labelText(text) {
	return _(text).replace(/&(&?)/g, '$1');
}

/**
 * A word with its values put in, in order — each `%s`, `%d`, `%i` the next one (ibString::Format): a translation may
 * move the place, never the order.
 * @param {string} text
 * @param {...(string | number)} values
 */
export function format(text, ...values) {
	let next = 0;
	return text.replace(/%[sdi]/g, () => String(values[next++] ?? ''));
}

/**
 * A GNU catalogue's pairs (the .mo layout): the magic in its byte order, then the count and the two tables of
 * (length, offset) — originals and translations — of UTF-8 strings. A word with a context is kept by its whole original
 * (`ctx\x04word`), as `_` asks for it; a plural (`one\0many`) by its first translation: the page asks for none.
 * @param {DataView} data
 */
function readCatalogue(data) {
	const little = data.getUint32(0, true) === 0x950412de;
	if (!little && data.getUint32(0, false) !== 0x950412de)
		return;
	const read = (/** @type {number} */ at) => data.getUint32(at, little);
	const count = read(8);
	const originals = read(12);
	const translations = read(16);
	const decoder = new TextDecoder('utf-8');
	/** @param {number} table @param {number} index */
	const text = (table, index) => decoder.decode(new Uint8Array(data.buffer, data.byteOffset + read(table + index * 8 + 4),
		read(table + index * 8)));
	for (let index = 0; index < count; index++) {
		const original = text(originals, index);
		if (original !== '')
			words.set(original, text(translations, index).split('\0')[0]);
	}
}
