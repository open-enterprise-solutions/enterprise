// A CELL'S TEXT TYPED IN PLACE, as the thin client's sheet takes it (ibGrid::OnChar, its cell's text editor): F2 opens
// the current cell with its text; a key that types a character opens it empty, and the character goes in; Backspace
// opens it empty. Enter takes the text and moves down (Shift: up), Tab across (Shift: back), Ctrl+Enter breaks the
// line; Escape leaves the cell as it was; leaving the field takes it. A sheet not to be edited is not opened, nor a
// cell that says so of itself. A double click stays the report's question (Cell), as the thin client's box keeps it.

/** @typedef {import('./selection.js').CellSelection} CellSelection */

export class CellEditor {
	/**
	 * @param {HTMLElement} host the sheet's frame — the keys come to it
	 * @param {CellSelection} selection
	 * @param {(cell: HTMLTableCellElement, text: string) => void} done a text taken into a cell, changed
	 */
	constructor(host, selection, done) {
		this.host = host;
		this.selection = selection;
		this.done = done;
		/** whether the sheet is edited at all — its ReadOnly, the other way round */
		this.editable = false;
		/** @type {HTMLTextAreaElement | null} the text being typed */
		this.field = null;
		host.addEventListener('keydown', (event) => this.key(event));
	}

	/** @param {KeyboardEvent} event */
	key(event) {
		const cell = this.selection.current;
		if (!this.editable || event.target !== this.host || cell === undefined || event.ctrlKey || event.metaKey
			|| event.altKey)
			return;
		if (event.key === 'F2') {
			event.preventDefault();
			this.open(cell, cell.textContent ?? '');
		}
		else if (event.key === 'Backspace') {
			event.preventDefault();
			this.open(cell, '');
		}
		// The character is not taken here: the field has the focus by the time it is typed, and takes it as it takes any
		// — a composed one too.
		else if (event.key.length === 1) {
			this.open(cell, '');
		}
	}

	/**
	 * @param {HTMLTableCellElement} cell
	 * @param {string} text what it opens with
	 */
	open(cell, text) {
		if (cell.classList.contains('read-only'))
			return;
		this.close(true);
		const field = document.createElement('textarea');
		field.className = 'cell-editor';
		field.value = text;
		field.spellcheck = false;
		field.addEventListener('keydown', (event) => this.fieldKey(event));
		field.addEventListener('blur', () => this.close(true));
		cell.classList.add('editing');
		cell.append(field);
		this.field = field;
		field.focus({ preventScroll: true });
		field.setSelectionRange(text.length, text.length);
	}

	/** The field's keys are its own; these four end it, or break its line. @param {KeyboardEvent} event */
	fieldKey(event) {
		event.stopPropagation();
		const field = /** @type {HTMLTextAreaElement} */ (event.currentTarget);
		if (event.key === 'Enter' && (event.ctrlKey || event.altKey)) {
			event.preventDefault();
			field.setRangeText('\n', field.selectionStart, field.selectionEnd, 'end');
		}
		else if (event.key === 'Enter' || event.key === 'Tab') {
			event.preventDefault();
			this.close(true);
			const back = event.shiftKey ? -1 : 1;
			this.selection.moveBy(event.key === 'Enter' ? [back, 0] : [0, back], false);
		}
		else if (event.key === 'Escape') {
			event.preventDefault();
			this.close(false);
		}
	}

	/**
	 * The field taken away, its text into the cell when it is taken and differs; the keys back to the sheet.
	 * @param {boolean} take
	 */
	close(take) {
		const field = this.field;
		if (field === null)
			return;
		this.field = null;   // first: taking the field away blurs it, and the blur closes it again
		const cell = /** @type {HTMLTableCellElement} */ (field.parentElement);
		const focused = document.activeElement === field;
		field.remove();
		cell.classList.remove('editing');
		if (take && field.value !== (cell.textContent ?? ''))
			this.done(cell, field.value);
		if (focused)
			this.host.focus({ preventScroll: true });
	}
}
