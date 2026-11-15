// FIND — the thin client's Find (ibFindReplaceDialog, which its doc manager opens over the document on show and keeps
// while it is up): what to search for, Whole word, Match case, the search direction; Find takes the next one in the text
// on show from its selection — down from its end or up from its start, not round, as the thin client's text view
// (SearchNext, SearchPrev) — and selects it; none, a box says so. Not modal, as the thin client's: the text stays to be
// read, and typed in. Down at first — the thin client's opens on Up only because wx's search data starts with no flags.

import { _, format, labelText } from '../localization.js';

/** @typedef {import('./questions.js').Questions} Questions */

/** @type {HTMLDialogElement | null} the one up — one, as the thin client's doc manager keeps one */
let panel = null;
/** @type {(() => void) | null} the found one's mark taken away — none marked: null */
let unmark = null;

/**
 * Find opened, or the one up given the focus.
 * @param {() => HTMLTextAreaElement | null} textOnShow the text searched, asked as Find is pressed: the tab on show
 *   may change while it is up
 * @param {Questions} questions what a text not found is said through
 */
export function showFind(textOnShow, questions) {
	panel ??= build(textOnShow, questions);
	if (!panel.isConnected)
		document.body.append(panel);
	panel.show();
	panel.querySelector('input')?.focus();
}

/**
 * @param {() => HTMLTextAreaElement | null} textOnShow
 * @param {Questions} questions
 * @returns {HTMLDialogElement}
 */
function build(textOnShow, questions) {
	const dialog = document.createElement('dialog');
	dialog.className = 'find-dialog';
	const close = () => dialog.close();
	dialog.addEventListener('close', () => unmark?.());

	const header = document.createElement('div');
	header.className = 'find-dialog-header';
	const title = document.createElement('span');
	title.textContent = _('Find');
	const cross = document.createElement('button');
	cross.type = 'button';
	cross.textContent = '×';
	cross.ariaLabel = _('Close');
	cross.addEventListener('click', close);
	header.append(title, cross);

	const form = document.createElement('form');
	const sought = document.createElement('input');
	sought.type = 'text';
	sought.spellcheck = false;
	const soughtLine = document.createElement('label');
	soughtLine.className = 'find-dialog-sought';
	soughtLine.append(_('Search for:'), sought);
	const wholeWord = checkBox(_('Whole word'));
	const matchCase = checkBox(_('Match case'));
	const direction = document.createElement('fieldset');
	const legend = document.createElement('legend');
	legend.textContent = _('Search direction');
	const [up, down] = [_('Up'), _('Down')].map((caption) => {
		const radio = document.createElement('input');
		radio.type = 'radio';
		radio.name = 'find-direction';
		const label = document.createElement('label');
		label.append(radio, caption);
		direction.append(label);
		return radio;
	});
	down.checked = true;
	direction.prepend(legend);
	const options = document.createElement('div');
	options.className = 'find-dialog-options';
	const checks = document.createElement('div');
	checks.append(wholeWord.label, matchCase.label);
	options.append(checks, direction);

	const find = /** @type {any} */ (document.createElement('wa-button'));
	find.setAttribute('size', 's');
	find.setAttribute('variant', 'brand');
	find.textContent = _('Find');
	const cancel = /** @type {any} */ (document.createElement('wa-button'));
	cancel.setAttribute('size', 's');
	cancel.textContent = _('Cancel');
	cancel.addEventListener('click', close);
	const buttons = document.createElement('div');
	buttons.className = 'find-dialog-buttons';
	buttons.append(find, cancel);

	const body = document.createElement('div');
	body.className = 'find-dialog-body';
	const fields = document.createElement('div');
	fields.append(soughtLine, options);
	body.append(fields, buttons);
	form.append(body);
	dialog.append(header, form);

	// Nothing to search for, nothing to find (OnUpdateFindUI).
	const enable = () => find.toggleAttribute('disabled', sought.value === '');
	sought.addEventListener('input', enable);
	enable();
	const findNext = () => {
		if (sought.value === '')
			return;
		const text = textOnShow();
		if (text === null || !findIn(text, sought.value, wholeWord.box.checked, matchCase.box.checked, down.checked)) {
			questions.dialog(_('Not Found!'), format(_('"%s" not found!'), sought.value),
				[{ caption: labelText('&OK'), answer: {}, preferred: true }], null);
		}
	};
	find.addEventListener('click', findNext);
	form.addEventListener('submit', (event) => {
		event.preventDefault();
		findNext();
	});
	dialog.addEventListener('keydown', (event) => {
		if (event.key !== 'Escape')
			return;
		event.preventDefault();
		close();
	});
	return dialog;
}

/**
 * The next one in a text from its selection, selected, marked and brought into view; false — none. The focus stays in
 * Find, as the thin client's: Enter finds the next one, and types nothing over the one found.
 * @param {HTMLTextAreaElement} text
 * @param {string} sought
 * @param {boolean} wholeWord not part of a longer word: no letter, digit or _ either side
 * @param {boolean} matchCase
 * @param {boolean} down
 */
function findIn(text, sought, wholeWord, matchCase, down) {
	const literal = sought.replace(/[.*+?^${}()|[\]\\/]/g, '\\$&');
	const pattern = new RegExp(wholeWord ? `(?<![\\p{L}\\p{N}_])${literal}(?![\\p{L}\\p{N}_])` : literal,
		matchCase ? 'gu' : 'giu');
	/** @type {RegExpExecArray | null} */
	let found = null;
	if (down) {
		pattern.lastIndex = text.selectionEnd;
		found = pattern.exec(text.value);
	}
	else {
		for (const one of text.value.matchAll(pattern)) {
			if (one.index >= text.selectionStart)
				break;
			found = one;
		}
	}
	if (found === null)
		return false;
	text.setSelectionRange(found.index, found.index + found[0].length);
	mark(text, found.index, found.index + found[0].length);
	return true;
}

/**
 * The found one marked over its text — a field shows its selection only while it has the focus, and the focus is
 * Find's: a copy of the text laid over the field's own, its ink clear but the found part's, which has the selection's
 * look; the field scrolled to it, the copy following. Gone as the field is gone into, typed in, or Find closes.
 * @param {HTMLTextAreaElement} text
 * @param {number} from
 * @param {number} to
 */
function mark(text, from, to) {
	unmark?.();
	const style = getComputedStyle(text);
	const copy = document.createElement('div');
	copy.className = 'find-mark';
	Object.assign(copy.style, {
		left: `${text.offsetLeft + text.clientLeft}px`, top: `${text.offsetTop + text.clientTop}px`,
		width: `${text.clientWidth}px`, height: `${text.clientHeight}px`, padding: style.padding, font: style.font,
		tabSize: style.tabSize,
	});
	const found = document.createElement('mark');
	found.textContent = text.value.slice(from, to);
	copy.append(text.value.slice(0, from), found, text.value.slice(to));
	text.after(copy);
	// Into view, from where the copy laid it: a line above or below what is seen, half a field in.
	const top = found.offsetTop;
	if (top < text.scrollTop || top + found.offsetHeight > text.scrollTop + text.clientHeight)
		text.scrollTop = Math.max(0, top - text.clientHeight / 2);
	const left = found.offsetLeft;
	if (left < text.scrollLeft || left + found.offsetWidth > text.scrollLeft + text.clientWidth)
		text.scrollLeft = Math.max(0, left - text.clientWidth / 2);
	const follow = () => {
		copy.scrollTop = text.scrollTop;
		copy.scrollLeft = text.scrollLeft;
	};
	follow();
	const listening = new AbortController();
	text.addEventListener('scroll', follow, { signal: listening.signal });
	for (const gone of ['focus', 'input'])
		text.addEventListener(gone, () => unmark?.(), { signal: listening.signal });
	unmark = () => {
		listening.abort();
		copy.remove();
		unmark = null;
	};
}

/** @param {string} caption */
function checkBox(caption) {
	const box = document.createElement('input');
	box.type = 'checkbox';
	const label = document.createElement('label');
	label.append(box, caption);
	return { box, label };
}
