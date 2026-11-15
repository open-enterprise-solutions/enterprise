// A TRANSLATED TEXT — `code = 'text';`, a language each (ibLocalization's format), read in a language and edited in a
// window of its own: a box for each of the configuration's languages, then the codes the text holds beyond them, apart
// and kept (the thin client's translation constructor, ibDialogTranslateConstructor — the window under a translated
// property's «…»). OK writes each box into the text that came in: its order stays, an empty box takes its language out
// — «not translated», so a reader falls back to another language, not to nothing. The languages are the
// configuration's, asked of the server once a session (the schema Languages); a text in no format is the text in the
// language in force.

import { _, language } from '../localization.js';
import { editWindow } from '../settings/settingsWindow.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../protocol/session.js').Session} Session */
/** A language of a text — its code, what it says. @typedef {{code: string, text: string}} Translation */

// The schema the configuration's languages are asked by (ibSchema::ibKind::Languages).
const schema = 11;

/** @type {{code: string, title: string}[]} the configuration's languages, in its order — what a person calls each */
let languages = [];

/**
 * The configuration's languages, read once the person is in (ibUserInfo::ListLanguages) — when the server offers the
 * schema (the frame's Schemas). A read that fails, or none offered, leaves none: the window then has the one box every
 * text has, the language in force.
 * @param {Session} session
 * @param {ProtocolNode | null} frame
 */
export async function readLanguages(session, frame) {
	languages = [];
	if (!(frame?.Schemas ?? []).includes(schema))
		return;
	try {
		const answer = await session.call('schema', { Schema: schema });
		languages = (answer.Languages?.NodeChildren ?? [])
			.map((/** @type {any} */ one) => ({ code: String(one.Code ?? ''), title: String(one.Title ?? '') }))
			.filter((one) => one.code !== '');
	}
	catch {
		languages = [];
	}
}

/**
 * A translated text edited language by language, in a window titled as the property is; the text written back, null
 * when cancelled.
 * @param {string} caption
 * @param {string} raw the text as it is held
 * @returns {Promise<string | null>}
 */
export async function editTranslate(caption, raw) {
	const held = heldOf(raw);
	const own = languages.length > 0 ? languages : [{ code: language(), title: '' }];
	const isOwn = (/** @type {string} */ code) => own.some((one) => sameCode(one.code, code));
	/** @type {[string, HTMLTextAreaElement][]} each box with the code it writes */
	const boxes = [];
	const grid = (/** @type {[string, string][]} */ rows) => {
		const element = document.createElement('div');
		element.className = 'translate-languages';
		for (const [code, label] of rows) {
			const name = document.createElement('label');
			name.textContent = label;
			const box = document.createElement('textarea');
			box.rows = 2;
			box.spellcheck = false;
			box.value = held.find((one) => sameCode(one.code, code))?.text ?? '';
			name.htmlFor = box.id = `translate-${boxes.length}`;
			element.append(name, box);
			boxes.push([code, box]);
		}
		return element;
	};
	const content = document.createElement('div');
	content.className = 'translate-content';
	content.append(grid(own.map((one) => [one.code, one.title === '' || sameCode(one.title, one.code) ? one.code
		: `${one.title} (${one.code})`])));
	// What the text holds beyond those languages — shown apart, and kept.
	const other = held.filter((one) => !isOwn(one.code));
	if (other.length > 0) {
		const said = document.createElement('p');
		said.textContent = languages.length > 0
			? _('Also in this text, for codes this configuration has no language for. They are kept:')
			: _('Also in this text:');
		content.append(said, grid(other.map((one) => [one.code, one.code])));
	}
	// The keyboard goes where a translation is missing — what the window was opened to write — else to the language in
	// force, else to the first.
	const focus = boxes.find(([, box]) => box.value === '') ?? boxes.find(([code]) => sameCode(code, language())) ?? boxes[0];
	if (focus !== undefined)
		focus[1].autofocus = true;
	if (!await editWindow(caption, 'translate-window', content))
		return null;
	for (const [code, box] of boxes)
		put(held, code, box.value);
	return written(held);
}

/**
 * A translated text with the language in force given `text` (ibTStringProperty::StringToValue — SetTranslate): its
 * entry written, the others kept; an empty one takes the language out, as the window's empty box does.
 * @param {string} raw
 * @param {string} text
 */
export function translated(raw, text) {
	const entries = heldOf(raw);
	put(entries, language(), text);
	return written(entries);
}

/**
 * A translated text as it reads in a language (ibLocalization::GetTranslateFromArray): its entry, the code matched
 * without regard to case; with none, the first — a text a person can read beats an empty one; a text in no format reads
 * as itself.
 * @param {string} raw
 * @param {string} language
 */
export function readingOf(raw, language) {
	const entries = translations(raw);
	if (entries === null)
		return raw;
	return (entries.find((entry) => sameCode(entry.code, language)) ?? entries[0])?.text ?? '';
}

/**
 * The languages a text holds (ibTranslateString::SetRawText): its own, or — in no format — the text in the language in
 * force; none for none.
 * @param {string} raw
 * @returns {Translation[]}
 */
function heldOf(raw) {
	return translations(raw) ?? (raw !== '' ? [{ code: language(), text: raw }] : []);
}

/**
 * A language's text put in (Collect, SetTranslate): written over its entry or added after the others; empty — taken out,
 * «not translated».
 * @param {Translation[]} entries
 * @param {string} code
 * @param {string} text
 */
function put(entries, code, text) {
	const at = entries.findIndex((one) => sameCode(one.code, code));
	if (text === '') {
		if (at >= 0)
			entries.splice(at, 1);
	}
	else if (at >= 0)
		entries[at].text = text;
	else
		entries.push({ code, text });
}

/**
 * The languages of a translated text, in the order written; null when it is in no format.
 * @param {string} raw
 * @returns {Translation[] | null}
 */
function translations(raw) {
	const entry = /\s*([^\s=';]+)\s*=\s*'((?:[^']|'')*)'\s*(?:;|$)/y;
	const entries = [];
	const end = raw.trimEnd().length;
	while (entry.lastIndex < end) {
		const match = entry.exec(raw);
		if (match === null)
			return null;
		entries.push({ code: match[1], text: match[2].replace(/''/g, "'") });
	}
	return entries.length > 0 ? entries : null;
}

/**
 * The text of its languages, as the format writes it.
 * @param {Translation[]} entries
 */
function written(entries) {
	return entries.map((entry) => `${entry.code} = '${entry.text.replace(/'/g, "''")}';`).join('');
}

/**
 * The same language code, whatever the case.
 * @param {string} a
 * @param {string} b
 */
function sameCode(a, b) {
	return a.toLowerCase() === b.toLowerCase();
}
