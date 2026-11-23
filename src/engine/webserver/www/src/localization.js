// THE PAGE'S OWN WORDS IN THE PERSON'S LANGUAGE — the thin client's dictionary (lang/<language>/open_es.po, which the web
// server serves beside the page from the program folder), in the language the login names (Locale) — the browser's
// until then, as the thin client speaks the machine's before it has a base. The page writes its
// words as the thin client writes them — its msgids — so one dictionary says both clients' words, and a word the
// dictionary does not have stays as written. Read before the window is drawn; with none to be had, the page speaks
// English. What the server says (its menus, forms, messages) comes said already.

// The catalogue's words, said once — as the engine's (dictionary.cpp): its file's extension, and what stands between a
// context and its word in a key.
const catalogExtension = '.po';
const contextSeparator = '\x04';

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
		const answer = await fetch(`lang/${encodeURIComponent(language)}/open_es${catalogExtension}`).catch(() => null);
		if (answer?.ok) {
			readCatalogue(await answer.text());
			return;
		}
	}
}

/**
 * The language a translated text is read in — an application's own text, written in several (`uk = '…'; en = '…';`):
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
	return words.get(context === undefined ? text : `${context}${contextSeparator}${text}`) || text;
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
 * A GNU catalogue as the translators write it (the .po), read as the engine reads it (core/localization/dictionary.cpp,
 * ibMsgCatalogFile): an entry still `fuzzy`, an empty msgstr, an obsolete one (`#~`) and the header are no translation;
 * a c-format one naming other conversions than its text is not taken; a line no .po has refuses the whole catalogue —
 * half of one would be read as a whole one. A word with a context is kept by `ctx\x04word`, as `_` asks for it; a
 * plural by its first form: the page asks for none.
 * @param {string} text
 */
function readCatalogue(text) {
	/** @typedef {{ context?: string, id?: string, plural: string, texts: string[], fuzzy: boolean, cFormat: boolean }} Entry */
	/** @type {Entry[]} */
	const entries = [];
	/** @returns {Entry} */
	const fresh = () => ({ plural: '', texts: [], fuzzy: false, cFormat: false });
	let entry = fresh();
	/** the string a line of "…" carries on @type {((more: string) => void) | null} */
	let carry = null;
	let translated = false;   // the entry has a msgstr: a keyword or a comment after it starts the next one
	const finish = () => {
		if (entry.id !== undefined)
			entries.push(entry);
		entry = fresh();
		carry = null;
		translated = false;
	};

	for (const read of text.split('\n')) {
		const line = read.replace(/\r$/, '').replace(/^[ \t]+/, '');
		if (line === '') {
			carry = null;
			continue;
		}
		// A comment of the next entry ends this one: `#,` says the flags, `#~` is an entry nobody uses any more.
		if (line[0] === '#') {
			if (translated)
				finish();
			carry = null;
			if (line[1] === ',')
				for (const flag of line.slice(2).split(',').map((one) => one.trim())) {
					if (flag === 'fuzzy')
						entry.fuzzy = true;
					else if (flag === 'c-format')
						entry.cFormat = true;
				}
			continue;
		}
		// A string carried over to the next line.
		if (line[0] === '"') {
			const more = unquote(line);
			if (carry === null || more === null)
				return;
			carry(more);
			continue;
		}
		const space = line.search(/[ \t]/);
		const keyword = space < 0 ? line : line.slice(0, space);
		const value = space < 0 ? null : unquote(line.slice(space));
		if (value === null)
			return;
		const started = entry.id !== undefined;
		if (keyword === 'msgctxt' || keyword === 'msgid') {
			if (translated)
				finish();
			const one = entry;
			if (keyword === 'msgctxt') {
				one.context = value;
				carry = (more) => { one.context += more; };
			}
			else {
				one.id = value;
				carry = (more) => { one.id += more; };
			}
		}
		else if (keyword === 'msgid_plural' && started) {
			const one = entry;
			one.plural = value;
			carry = (more) => { one.plural += more; };
		}
		else if (/^msgstr(\[[0-9]{1,3}\])?$/.test(keyword) && started) {
			const texts = entry.texts;
			const form = keyword === 'msgstr' ? 0 : Number(keyword.slice(7, -1));
			texts[form] = value;
			carry = (more) => { texts[form] += more; };
			translated = true;
		}
		else
			return;
	}
	finish();

	for (const one of entries) {
		if (one.fuzzy || (one.context === undefined && one.id === ''))
			continue;   // a guess is no translation yet; the header is the file's, not a word
		const said = one.texts[0] ?? '';
		if (said === '' || (one.cFormat && conversions(said) !== conversions(one.id ?? '')))
			continue;
		words.set(one.context === undefined ? one.id ?? '' : `${one.context}${contextSeparator}${one.id}`, said);
	}
}

/**
 * A .po string, its quotes and its escapes taken off — the engine's Unquote; null for what is no quoted string. The
 * octal escapes in a row are bytes, UTF-8 as the engine reads them.
 * @param {string} line
 */
function unquote(line) {
	const quoted = line.replace(/^[ \t]+|[ \t]+$/g, '');
	if (quoted.length < 2 || quoted[0] !== '"' || quoted[quoted.length - 1] !== '"')
		return null;
	const simple = /** @type {Record<string, string>} */ ({ n: '\n', t: '\t', r: '\r', a: '\x07', b: '\b', f: '\f', v: '\v',
		'"': '"', '\\': '\\', "'": "'", '?': '?' });
	let text = '';
	/** @type {number[]} */
	let bytes = [];
	const flush = () => {
		if (bytes.length > 0)
			text += new TextDecoder('utf-8').decode(new Uint8Array(bytes));
		bytes = [];
	};
	for (let i = 1; i + 1 < quoted.length; ++i) {
		const c = quoted[i];
		if (c === '"')
			return null;
		if (c === '\\' && i + 2 >= quoted.length)
			return null;   // what it escapes is the closing quote
		const escaped = c === '\\' ? quoted[++i] : '';
		if (escaped >= '0' && escaped <= '7') {
			let value = escaped.charCodeAt(0) - 48;   // octal, up to three digits
			for (let digit = 1; digit < 3 && i + 2 < quoted.length && quoted[i + 1] >= '0' && quoted[i + 1] <= '7'; ++digit)
				value = value * 8 + (quoted.charCodeAt(++i) - 48);
			bytes.push(value);
			continue;
		}
		flush();
		if (c !== '\\')
			text += c;
		else if (escaped in simple)
			text += simple[escaped];
		else
			return null;
	}
	flush();
	return text;
}

/**
 * The conversions a c-format string names, each by its length and its kind, in its values' order — the engine's
 * Conversions: `%%` is a character, `%2$s` stands at its position. A translation must name the same.
 * @param {string} text
 */
function conversions(text) {
	/** @type {[number, string][]} */
	const found = [];
	let next = 0;
	for (let i = 0; i < text.length; ++i) {
		if (text[i] !== '%')
			continue;
		if (++i < text.length && text[i] === '%')
			continue;
		let position = ++next;
		let digits = i;
		let written = 0;
		while (digits < text.length && text[digits] >= '0' && text[digits] <= '9')
			written = written * 10 + text.charCodeAt(digits++) - 48;
		if (digits > i && digits < text.length && text[digits] === '$') {
			position = written;
			i = digits + 1;
		}
		while (i < text.length && "-+ #0'".includes(text[i]))
			++i;   // the flags
		while (i < text.length && /[0-9.*]/.test(text[i]))
			++i;   // the width and the precision
		let conversion = '';
		while (i < text.length && 'hlLqjzt'.includes(text[i]))
			conversion += text[i++];
		if (i < text.length)
			conversion += text[i] === 'i' ? 'd' : text[i];
		found.push([position, conversion]);
	}
	found.sort((a, b) => a[0] - b[0]);
	return found.map(([, conversion]) => `${conversion};`).join('');
}
