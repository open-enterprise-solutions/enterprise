// THE TEXT EDITOR — the driver both a field and a table's cell type into, as the thin client's (ibTextCtrl): the
// text, and inside its frame the buttons its control was saved with: Select (…), Open (▷), Clear (×). It looks the same
// wherever it stands. What the text means, and when it goes back, is its owner's.
//
// Under a mask (setMask) a key, a deletion, a paste is the mask's (inputMask.js replace) before the browser's: what the
// mask does not take does not go in, a position deleted is blanked, the literals stay — as the thin client's editor.

import { EventKind } from './control.js';
import { InputMask } from './inputMask.js';

export class TextEditor {
	/** @param {{multiline?: boolean, password?: boolean}} [kind] decided as it is made */
	constructor({ multiline = false, password = false } = {}) {
		this.field = /** @type {any} */ (document.createElement(multiline ? 'wa-textarea' : 'wa-input'));
		this.field.setAttribute('size', 's');
		if (!multiline && password)
			this.field.setAttribute('type', 'password');
		this.multiline = multiline;
		/** @type {(kind: number) => void} a button pressed: the event its owner sends */
		this.onButton = () => {};
		/** @type {{[kind: string]: HTMLElement}} */
		this.buttons = {
			ButtonSelect: this.sideButton('…', 'Select', EventKind.Select),
			ButtonOpen: this.sideButton('▷', 'Open', EventKind.Open),
			ButtonClear: this.sideButton('×', 'Clear', EventKind.Clear),
		};

		/** what can be typed into it — an empty one: anything */
		this.mask = new InputMask();
		/** @type {{masked: string, caret: number}[]} the text before each masked edit — the browser's history knows none */
		this.toUndo = [];
		/** @type {{masked: string, caret: number}[]} …and before each undo, to be done again */
		this.toRedo = [];
		/** a masked edit not yet said by a change event: the browser says one only of what it typed itself */
		this.edited = false;
		this.field.addEventListener('beforeinput', (/** @type {InputEvent} */ event) => this.maskInput(event));
		this.field.addEventListener('change', () => { this.edited = false; });
		// Enter commits at either owner (a field, a cell): what was typed is said by it, not again when the field is left.
		this.field.addEventListener('keydown', (/** @type {KeyboardEvent} */ key) => {
			if (key.key === 'Enter' && !this.multiline)
				this.edited = false;
		});
		this.field.addEventListener('blur', () => {
			if (this.edited) {
				this.edited = false;
				this.field.dispatchEvent(new Event('change', { bubbles: true }));
			}
		});
		// A double click under a mask selects the run the caret stands in — a day, a month, a number's whole part or its
		// fraction (inputMask.js getRun), as the thin client's editor; to the browser a date is one word. The browser
		// selects its word on the second press, and is stopped there.
		this.field.addEventListener('mousedown', (/** @type {MouseEvent} */ event) => {
			const native = this.field.input;
			if (event.detail !== 2 || this.multiline || !native)
				return;
			const run = this.mask.getRun(native.value, native.selectionStart ?? 0);
			if (run === null)
				return;   // no mask, or no run here: the browser's word
			event.preventDefault();
			native.setSelectionRange(run.first, run.last + 1);
		});
	}

	/**
	 * The mask its text is typed through, the server's resolved; an empty one is none. A multi-line editor has none, as
	 * the thin client's.
	 * @param {InputMask} mask
	 */
	setMask(mask) {
		this.mask = mask;
	}

	/**
	 * The buttons shown, as its control's node names them.
	 * @param {{[saved: string]: any}} node
	 */
	showButtons(node) {
		for (const [saved, button] of Object.entries(this.buttons))
			button.hidden = node[saved] !== true;
	}

	/** The focus taken, the text selected, once the component is there to take them. */
	async focus() {
		await customElements.whenDefined(this.field.localName);
		await this.field.updateComplete;
		this.field.focus();
		this.field.select?.();
	}

	/**
	 * A text in the field, not typed — no input event — and the selection in it, at once: the component draws its value
	 * later, and finding the text there already it leaves the selection be.
	 * @param {string} text
	 * @param {number} [start]
	 * @param {number} [end]
	 */
	changeValue(text, start = text.length, end = start) {
		const native = this.field.input ?? this.field.textarea;
		if (native)
			native.value = text;
		this.field.value = text;
		native?.setSelectionRange(start, end);
	}

	/**
	 * A key, a deletion, a paste, an undo — under a mask, the mask's: the browser's own edit is stopped and the mask's
	 * made in its place, said as typing is (input). A composition (an input method) cannot be stopped, and is let be.
	 * @param {InputEvent} event
	 */
	maskInput(event) {
		const native = this.field.input;
		if (this.mask.isEmpty() || this.multiline || event.isComposing || !native)
			return;
		const masked = native.value;
		const start = native.selectionStart ?? masked.length;
		const end = native.selectionEnd ?? start;
		const kind = event.inputType;

		if (kind === 'historyUndo' || kind === 'historyRedo') {
			event.preventDefault();
			const [taken, kept] = kind === 'historyUndo' ? [this.toUndo, this.toRedo] : [this.toRedo, this.toUndo];
			const step = taken.pop();
			if (step !== undefined) {
				kept.push({ masked, caret: start });
				this.changeValue(step.masked, step.caret);
				this.typed();
			}
			return;
		}

		// The range replaced, as the thin client's editor takes it: the selection, else the character the key deletes.
		let from = start;
		let to = end;
		let text = '';
		if (kind.startsWith('insert'))
			text = event.data ?? event.dataTransfer?.getData('text/plain') ?? '';
		else if (kind.startsWith('delete')) {
			if (from === to) {
				if (kind.includes('Backward'))
					from = Math.max(from - 1, 0);
				else
					to = Math.min(to + 1, masked.length);
			}
		}
		else
			return;   // a format — not a field's of one line
		event.preventDefault();
		if (kind.startsWith('insert') && text === '')
			return;   // a line break: the field has one line

		const edit = this.mask.replace(masked, from, to, text, start);
		if (edit === null)
			return;   // nothing of it the mask takes: the key is refused
		this.toUndo.push({ masked, caret: start });
		this.toRedo = [];
		this.changeValue(edit.masked, edit.caret);
		this.typed();
	}

	/** A masked edit made: said as typing is, and owed a change when the field is left. */
	typed() {
		this.edited = true;
		this.field.dispatchEvent(new Event('input', { bubbles: true, composed: true }));
	}

	/**
	 * @param {string} glyph
	 * @param {string} title
	 * @param {number} kind
	 */
	sideButton(glyph, title, kind) {
		const button = document.createElement('button');
		button.type = 'button';
		button.slot = 'end';
		button.className = 'side-button';
		button.textContent = glyph;
		button.title = title;
		button.setAttribute('aria-label', title);
		// The focus stays where it is: a blur here would commit the field beside the button, and the two would race
		// for one value.
		button.addEventListener('mousedown', (event) => event.preventDefault());
		button.addEventListener('click', () => this.onButton(kind));
		this.field.append(button);
		return button;
	}
}
