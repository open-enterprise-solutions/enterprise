// A FIELD (Textctrl) — its caption beside it, the text it shows (State's Text), in the text editor a table's cell has
// too (textEditor.js), with the buttons its node was saved with. The text is committed on change and on Enter — event
// Change {Text}, which the server parses by the field's type — never by the key; typing begun is the state event Input.
// The focus taken is its form's to tell (form.js).
//
// Typing wins over the server. What the field shows is written from the node while nobody is in it, or while it still
// shows what the page last wrote there and no older commit of it is unanswered; otherwise the server's text waits and
// lands when the field is left with nothing changed.
//
// Under a mask (State's Mask, the server's resolved) the field shows the server's text at rest — an empty one as the
// mask's skeleton — and, entered, the text the mask is typed over (EditText where the shown one is another) laid into it.

import { Control, EventKind, defineControl } from './control.js';
import { InputMask } from './inputMask.js';
import { TextEditor } from './textEditor.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

class Textctrl extends Control {
	// A field's own, as the server's makes it (ibValueTextCtrl): white — the theme's, as a table's.
	/** @type {[string, string]} */
	static platformBackground = ['rgb(255,255,255)', 'var(--oes-table-bg)'];

	/** @param {ProtocolNode} node */
	build(node) {
		this.caption = document.createElement('span');
		this.caption.className = 'caption';
		const multiline = node.MultilineMode === true;
		this.editor = new TextEditor({ multiline, password: node.PasswordMode === true });
		this.field = this.editor.field;
		this.lookTarget = this.field;
		this.append(this.caption, this.field);

		/** what the page last wrote into the field from a node — while it holds anything else, it is being typed in */
		this.applied = '';
		/** the server's Text — what the field shows at rest */
		this.text = '';
		/** …and its EditText: the text the mask is typed over, where Text is not */
		this.editText = '';
		/** @type {string | undefined} the server's text, waiting for the field to be left */
		this.waiting = undefined;
		/** commits sent and not yet answered */
		this.commits = 0;
		this.typing = false;
		/** the label column's width it was given — a field with no caption keeps the column too */
		this.labelWidth = 0;

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
		// case no change event covers. A masked field left untouched shows its text at rest again.
		this.field.addEventListener('blur', () => {
			if (this.field.value === this.applied
				&& (this.waiting !== undefined || (!this.editor.mask.isEmpty() && this.commits === 0))) {
				const text = this.shownText(false);
				this.field.value = text;
				this.applied = text;
			}
			this.waiting = undefined;
		});
		// Coming into a masked field shows the mask: the text it is typed over, laid into it, all of it selected — the
		// first key starts the value anew.
		this.field.addEventListener('focus', () => {
			if (this.editor.mask.isEmpty() || this.field.value !== this.applied)
				return;
			const text = this.shownText(true);
			this.applied = text;
			this.editor.changeValue(text, 0, text.length);
		});

		this.editor.onButton = async (kind) => {
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
		};
		this.update(node, {});
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
		this.editor.showButtons(node);
		this.editor.setMask(InputMask.parse(state.Mask ?? ''));

		this.text = state.Text ?? '';
		this.editText = state.EditText ?? '';
		const focused = this.contains(document.activeElement) || this.field.matches(':focus-within');
		const text = this.shownText(focused);
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

	/**
	 * The text the field shows while nobody types into it: the server's — and under a mask, where the person stands in
	 * the field, the text the mask is typed over, laid into it; elsewhere an empty text is the mask's skeleton.
	 * @param {boolean} focused
	 */
	shownText(focused) {
		const mask = this.editor.mask;
		if (mask.isEmpty())
			return this.text;
		if (focused)
			return mask.apply(this.editText !== '' ? this.editText : this.text);
		return this.text === '' ? mask.apply('') : this.text;
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
export function textWidth(text, style) {
	measure ??= /** @type {CanvasRenderingContext2D} */ (document.createElement('canvas').getContext('2d'));
	measure.font = `${style.fontStyle} ${style.fontWeight} ${style.fontSize} ${style.fontFamily}`;
	return Math.ceil(measure.measureText(text).width);
}

defineControl('Textctrl', Textctrl);
