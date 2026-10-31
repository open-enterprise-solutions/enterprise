// THE TEXT EDITOR — the driver both a field and a table's cell type into, as the thin client's (ibControlTextEditor): the
// text, and inside its frame the buttons its control was saved with: Select (…), Open (▷), Clear (×). It looks the same
// wherever it stands. What the text means, and when it goes back, is its owner's.

import { EventKind } from './control.js';

export class TextEditor {
	/** @param {{multiline?: boolean, password?: boolean}} [kind] decided as it is made */
	constructor({ multiline = false, password = false } = {}) {
		this.field = /** @type {any} */ (document.createElement(multiline ? 'wa-textarea' : 'wa-input'));
		this.field.setAttribute('size', 's');
		if (!multiline && password)
			this.field.setAttribute('type', 'password');
		/** @type {(kind: number) => void} a button pressed: the event its owner sends */
		this.onButton = () => {};
		/** @type {{[kind: string]: HTMLElement}} */
		this.buttons = {
			ButtonSelect: this.sideButton('…', 'Select', EventKind.Select),
			ButtonOpen: this.sideButton('▷', 'Open', EventKind.Open),
			ButtonClear: this.sideButton('×', 'Clear', EventKind.Clear),
		};
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
