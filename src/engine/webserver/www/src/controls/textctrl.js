// A FIELD (Textctrl) — its caption beside it, the text it shows (State's Text), and the buttons its node was saved
// with: Select (…), Open (▷), Clear (×), inside its frame, as the desktop's text editor has them. The text is
// committed on change and on Enter — event Change {Text}, which the server parses by the field's type — never by the
// key; typing begun is the state event Input, the focus taken Focus.
//
// Typing wins over the server. What the field shows is written from the node while nobody is in it, or while it still
// shows what the page last wrote there and no older commit of it is unanswered; otherwise the server's text waits and
// lands when the field is left with nothing changed.

import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Textctrl extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.caption = document.createElement('span');
		this.caption.className = 'caption';
		const multiline = node.MultilineMode === true;
		this.field = /** @type {any} */ (document.createElement(multiline ? 'wa-textarea' : 'wa-input'));
		this.field.setAttribute('size', 's');
		if (!multiline && node.PasswordMode)
			this.field.setAttribute('type', 'password');
		this.lookTarget = this.field;
		this.append(this.caption, this.field);

		/** what the page last wrote into the field from a node — while it holds anything else, it is being typed in */
		this.applied = '';
		/** @type {string | undefined} the server's text, waiting for the field to be left */
		this.waiting = undefined;
		/** commits sent and not yet answered */
		this.commits = 0;
		this.typing = false;
		/** the label column's width it was given — a field with no caption keeps the column too */
		this.labelWidth = 0;

		this.field.addEventListener('focus', () => this.send(EventKind.Focus));
		this.field.addEventListener('input', () => {
			if (!this.typing) {
				this.typing = true;
				this.send(EventKind.Input);
			}
		});
		this.field.addEventListener('change', () => this.commit());
		if (!multiline) {
			this.field.addEventListener('keydown', (/** @type {KeyboardEvent} */ key) => {
				if (key.key === 'Enter') {
					key.preventDefault();
					this.commit();
				}
			});
		}
		// A text the server sent while the field was typed in lands when the field is left with no net change — the
		// case no change event covers.
		this.field.addEventListener('blur', () => {
			if (this.waiting !== undefined && this.field.value === this.applied) {
				this.field.value = this.waiting;
				this.applied = this.waiting;
			}
			this.waiting = undefined;
		});

		/** @type {{[kind: string]: HTMLElement}} */
		this.buttons = {
			ButtonSelect: this.sideButton('…', 'Select', EventKind.Select),
			ButtonOpen: this.sideButton('▷', 'Open', EventKind.Open),
			ButtonClear: this.sideButton('×', 'Clear', EventKind.Clear),
		};
		this.update(node, {});
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
		button.addEventListener('click', async () => {
			// The button acts on the field as it stands: text typed and not committed is let go.
			this.applied = this.field.value;
			this.waiting = undefined;
			this.typing = false;
			await this.send(kind);
			// Clear puts the caret at the start of the field it emptied, as the desktop's editor does.
			if (kind === EventKind.Clear) {
				this.field.focus({ preventScroll: true });
				this.field.setSelectionRange?.(0, 0);
			}
		});
		this.field.append(button);
		return button;
	}

	/** Its caption's width in its font; an empty caption is 0 and still takes part, as the desktop's does. */
	calculateLabelSize() {
		const text = this.caption.textContent ?? '';
		return text === '' ? 0 : textWidth(text, getComputedStyle(this.caption));
	}

	/** @param {number} width */
	applyLabelSize(width) {
		this.labelWidth = width;
		this.style.setProperty('--oes-caption-width', width > 0 ? `${width}px` : '');
		this.caption.hidden = this.caption.textContent === '' && width <= 0;
	}

	async commit() {
		const sent = this.field.value ?? '';
		this.applied = sent;
		this.waiting = undefined;
		this.typing = false;
		this.commits++;
		try {
			await this.send(EventKind.Change, { Text: sent });
		}
		finally {
			this.commits--;
		}
	}

	/**
	 * @param {ProtocolNode} node
	 * @param {ProtocolNode} previous
	 */
	update(node, previous) {
		// The kind of field is decided when it is made; the first drawing has nothing before it.
		if (previous.NodeType !== undefined) {
			if ((node.MultilineMode === true) !== (previous.MultilineMode === true)
				|| (node.PasswordMode === true) !== (previous.PasswordMode === true))
				return false;
		}

		const state = this.state(node);
		this.caption.textContent = state.Caption ?? node.Title ?? '';
		this.caption.hidden = this.caption.textContent === '' && this.labelWidth <= 0;
		this.field.toggleAttribute('readonly', state.ReadOnly === true || node.TexteditMode === false);
		for (const [saved, button] of Object.entries(this.buttons))
			button.hidden = node[saved] !== true;

		const text = state.Text ?? '';
		const focused = this.contains(document.activeElement) || this.field.matches(':focus-within');
		if (!focused || (this.field.value === this.applied && this.commits <= 1)) {
			if (this.field.value !== text) {
				// The server's reformatted text lands under the caret, not after it.
				const native = this.field.input ?? this.field.textarea;
				const start = focused ? native?.selectionStart : null;
				const end = focused ? native?.selectionEnd : null;
				this.field.value = text;
				if (start !== null && start !== undefined)
					this.field.setSelectionRange?.(Math.min(start, text.length), Math.min(end ?? start, text.length));
			}
			this.applied = text;
			this.waiting = undefined;
		}
		else
			this.waiting = text;
		return true;
	}
}

/** @type {CanvasRenderingContext2D | null} */
let measure = null;

/**
 * A text's width in a font, as the desktop's GetTextExtent gives it — read from the font, not from a layout, so a
 * caption on a page not on show is measured too.
 * @param {string} text
 * @param {CSSStyleDeclaration} style
 */
function textWidth(text, style) {
	measure ??= /** @type {CanvasRenderingContext2D} */ (document.createElement('canvas').getContext('2d'));
	measure.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
	return Math.ceil(measure.measureText(text).width);
}

defineControl('Textctrl', Textctrl);
