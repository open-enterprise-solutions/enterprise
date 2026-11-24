// A TEXT AREA — a text document's view (a tab's whole view, the server's ibTextEditView), or a text area on a form: the
// thin client's ibValueTextBox on its text editor. The text the State names, in the editor's monospace on the theme's
// paper — the editor's own, whatever colours and font the control was saved with: the desktop's editor paints its text
// by its styles, not by its window's. Tab indents as the desktop's editor does (its settings: an indent of four,
// spaces, no tabs). What is typed is the person's until the box is left: then, changed, it goes back whole — event
// Change {Text}. The server's text lands only while the box still shows the one it sent last. ReadOnly — help, a
// document opened so. On paper the whole text, its lines wrapped at the page's width — as the thin client's text
// printout lays it (ibTextEditorPrintout: the editor's whole length page after page, Scintilla wrapping by words), not
// the box as it stands, cut at its frame: what the editor holds is laid into a copy for as long as the page is printed.

import { Control, EventKind, defineControl } from './control.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// The desktop editor's indent (ibEditorSettings: 4, no tabs).
const indentSize = 4;

class TextArea extends Control {
	constructor() {
		super();
		this.editor = document.createElement('textarea');
		/** the server's text, as shown last — the editor's other than it: typed, not committed yet */
		this.text = '';
		/** the text on paper — the editor's, while the page is printed; empty otherwise */
		this.printout = document.createElement('pre');
	}

	/** @param {ProtocolNode} node */
	build(node) {
		this.editor.spellcheck = false;
		this.editor.wrap = 'off';
		this.printout.className = 'printout';
		this.append(this.editor, this.printout);

		this.editor.addEventListener('blur', () => this.commit());
		this.editor.addEventListener('keydown', (event) => {
			if (event.key !== 'Tab' || event.ctrlKey || event.altKey || event.metaKey || this.editor.readOnly)
				return;
			event.preventDefault();
			this.indent(event.shiftKey ? -1 : 1);
		});
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		const state = this.state(node);
		// Not over what is being typed: that is the person's until committed.
		if (state.Text !== undefined && this.editor.value === this.text) {
			this.text = String(state.Text);
			// …the caret at its start, as the desktop's editor is left by a text set (Scintilla's SetText) — a field given
			// a value puts it at the end, and Find, down from there, found nothing.
			if (this.editor.value !== this.text) {
				this.editor.value = this.text;
				this.editor.setSelectionRange(0, 0);
			}
		}
		this.editor.readOnly = state.ReadOnly === true;
		return true;
	}

	/** What was typed, sent as the box is left — a box taken off the page takes it with it. */
	commit() {
		if (!this.isConnected || this.editor.value === this.text)
			return;
		this.text = this.editor.value;
		this.send(EventKind.Change, { Text: this.text });
	}

	/**
	 * Tab as the desktop's editor takes it (Scintilla, spaces, no tab indents): the lines a selection spans indented by
	 * one step, or outdented — an empty line is not indented; else spaces up to the next stop, or the caret back to the
	 * previous one. Typed as an edit, so Ctrl+Z takes it back.
	 * @param {number} step 1 forward, -1 back
	 */
	indent(step) {
		const editor = this.editor;
		const { value, selectionStart: start, selectionEnd: end } = editor;
		const first = value.lastIndexOf('\n', start - 1) + 1;
		if (value.slice(start, end).includes('\n')) {
			// A selection ending at a line's start leaves that line as it is.
			const last = value[end - 1] === '\n' ? end - 1 : end;
			const stop = value.indexOf('\n', last);
			const blockEnd = stop < 0 ? value.length : stop;
			const outdent = new RegExp(`^( {1,${indentSize}}|\\t)`);
			const shift = (/** @type {string} */ line) => {
				if (step < 0)
					return line.replace(outdent, '');
				return line === '' ? line : `${' '.repeat(indentSize)}${line}`;
			};
			const block = value.slice(first, blockEnd).split('\n').map(shift).join('\n');
			editor.setSelectionRange(first, blockEnd);
			document.execCommand('insertText', false, block);
			editor.setSelectionRange(first, first + block.length);
			return;
		}
		const column = start - first;
		if (step > 0)
			document.execCommand('insertText', false, ' '.repeat(indentSize - (column % indentSize)));
		else if (column > 0) {
			const back = first + Math.floor((column - 1) / indentSize) * indentSize;
			editor.setSelectionRange(back, back);
		}
	}
}

// The page printed — by the menu's Print or the browser's own: every text box's text laid into its printout, and taken
// out after, so a long text is not held twice.
addEventListener('beforeprint', () => {
	for (const box of /** @type {NodeListOf<TextArea>} */ (document.querySelectorAll('oes-textarea')))
		box.printout.textContent = box.editor.value;
});
addEventListener('afterprint', () => {
	for (const box of /** @type {NodeListOf<TextArea>} */ (document.querySelectorAll('oes-textarea')))
		box.printout.textContent = '';
});

defineControl('TextArea', TextArea);
