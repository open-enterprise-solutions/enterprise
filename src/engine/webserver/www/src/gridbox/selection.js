// CELLS TAKEN, as in a spreadsheet program. A click takes a cell, a drag a block from it, Shift a block from the cell
// taken first, Ctrl one block more; the arrows move the cell taken, Shift with them stretches the block; Ctrl+A takes
// the sheet, Ctrl+C copies what is taken — as text by tabs and lines and as a table, so a spreadsheet program pastes it
// into cells, a figure as a number whatever its decimal sign. A cell spanning several places takes its whole span.
// What is taken is what is seen: a row folded away is neither summed nor copied. The figures taken are summed in a
// line over the sheet's corner — average, count, sum.

/** @typedef {{row: number, col: number}} Place */
/** @typedef {{top: number, left: number, bottom: number, right: number}} Block */

export class CellSelection {
	/**
	 * @param {HTMLElement} host the sheet's scrolling frame — it takes the keys
	 * @param {HTMLElement} summary the line the figures taken are summed in
	 */
	constructor(host, summary) {
		this.host = host;
		this.summary = summary;
		host.tabIndex = 0;
		/** @type {Map<string, HTMLTableCellElement>} the cell over each place, by `row:col` */
		this.cells = new Map();
		/** @type {HTMLTableRowElement[]} */
		this.rows = [];
		/** @type {Block[]} the blocks taken before the last one, with Ctrl */
		this.kept = [];
		/** @type {Place | null} the cell taken first — the current one */
		this.anchor = null;
		/** @type {Place} …and the corner the last block is stretched to */
		this.focus = { row: 0, col: 0 };
		/** @type {Set<HTMLTableCellElement>} */
		this.marked = new Set();
		/** @type {HTMLTableCellElement | undefined} */
		this.current = undefined;
		host.addEventListener('mousedown', (event) => this.pressed(event));
		host.addEventListener('mouseover', (event) => this.dragged(event));
		host.addEventListener('keydown', (event) => this.key(event));
	}

	/** A sheet drawn anew: nothing taken on it. @param {HTMLTableElement} table */
	attach(table) {
		this.cells.clear();
		this.rows = [...table.rows];
		for (const cell of /** @type {NodeListOf<HTMLTableCellElement>} */ (table.querySelectorAll('td[data-row]'))) {
			const { row, col } = placeOf(cell);
			for (let r = row; r < row + cell.rowSpan; r++) {
				for (let c = col; c < col + cell.colSpan; c++)
					this.cells.set(`${r}:${c}`, cell);
			}
		}
		this.kept = [];
		this.anchor = null;
		this.marked.clear();
		this.current = undefined;
		this.paint();
	}

	/** @param {MouseEvent} event */
	pressed(event) {
		const cell = /** @type {HTMLElement} */ (event.target).closest('td[data-row]');
		if (event.button !== 0 || cell === null)
			return;
		const place = placeOf(/** @type {HTMLTableCellElement} */ (cell));
		if (event.shiftKey && this.anchor !== null) {
			this.focus = place;
		}
		else {
			if ((event.ctrlKey || event.metaKey) && this.anchor !== null)
				this.kept.push(this.block(this.anchor, this.focus));
			else
				this.kept = [];
			this.anchor = place;
			this.focus = place;
		}
		this.paint();
	}

	/** A drag, the button held: the last block stretched to the cell under it. @param {MouseEvent} event */
	dragged(event) {
		const cell = /** @type {HTMLElement} */ (event.target).closest('td[data-row]');
		if ((event.buttons & 1) === 0 || cell === null || this.anchor === null)
			return;
		const place = placeOf(/** @type {HTMLTableCellElement} */ (cell));
		if (place.row === this.focus.row && place.col === this.focus.col)
			return;
		this.focus = place;
		this.paint();
	}

	/** @param {KeyboardEvent} event */
	key(event) {
		const ctrl = event.ctrlKey || event.metaKey;
		if (ctrl && event.code === 'KeyA') {
			event.preventDefault();
			this.kept = [];
			this.anchor = { row: 0, col: 0 };
			this.focus = { row: this.rows.length - 1, col: this.lastCol() };
			this.paint();
			return;
		}
		if (ctrl && event.code === 'KeyC') {
			event.preventDefault();
			this.copy();
			return;
		}
		const step = { ArrowUp: [-1, 0], ArrowDown: [1, 0], ArrowLeft: [0, -1], ArrowRight: [0, 1] }[event.key];
		if (step === undefined || ctrl)
			return;
		event.preventDefault();
		if (this.anchor === null) {
			this.anchor = { row: 0, col: 0 };
			this.focus = this.anchor;
		}
		else if (event.shiftKey) {
			this.focus = this.next(this.focus, step);
		}
		else {
			this.kept = [];
			this.anchor = this.next(this.anchor, step);
			this.focus = this.anchor;
		}
		this.paint();
		// Brought into view clear of the frozen rows, which stand over the top of the frame.
		const head = /** @type {HTMLTableElement | null} */ (this.host.querySelector('table'))?.tHead;
		this.host.style.scrollPaddingTop = `${head?.offsetHeight ?? 0}px`;
		this.cells.get(key(event.shiftKey ? this.focus : this.anchor))?.scrollIntoView({ block: 'nearest', inline: 'nearest' });
	}

	/**
	 * The cell a step away: past the cell it starts on when that spans, past a row folded away, and where it stands
	 * when there is nothing further.
	 * @param {Place} from
	 * @param {number[]} step
	 * @returns {Place}
	 */
	next(from, [down, across]) {
		const start = this.cells.get(key(from));
		for (let row = from.row + down, col = from.col + across; ; row += down, col += across) {
			const cell = this.cells.get(`${row}:${col}`);
			if (cell === undefined)
				return from;
			if (cell !== start && !this.rows[row].hidden)
				return placeOf(cell);
		}
	}

	/** The last column any row reaches. */
	lastCol() {
		let last = 0;
		for (const place of this.cells.keys())
			last = Math.max(last, Number(place.split(':')[1]));
		return last;
	}

	/**
	 * The block between two corners, grown over every cell that spans out of it.
	 * @param {Place} one
	 * @param {Place} other
	 * @returns {Block}
	 */
	block(one, other) {
		const block = {
			top: Math.min(one.row, other.row), left: Math.min(one.col, other.col),
			bottom: Math.max(one.row, other.row), right: Math.max(one.col, other.col),
		};
		for (let grown = true; grown;) {
			grown = false;
			for (const cell of this.cellsIn(block)) {
				const { row, col } = placeOf(cell);
				const bottom = row + cell.rowSpan - 1;
				const right = col + cell.colSpan - 1;
				if (row < block.top || col < block.left || bottom > block.bottom || right > block.right) {
					block.top = Math.min(block.top, row);
					block.left = Math.min(block.left, col);
					block.bottom = Math.max(block.bottom, bottom);
					block.right = Math.max(block.right, right);
					grown = true;
				}
			}
		}
		return block;
	}

	/** Each cell over a block's places, once. @param {Block} block */
	cellsIn(block) {
		/** @type {Set<HTMLTableCellElement>} */
		const cells = new Set();
		for (let row = block.top; row <= block.bottom; row++) {
			for (let col = block.left; col <= block.right; col++) {
				const cell = this.cells.get(`${row}:${col}`);
				if (cell !== undefined)
					cells.add(cell);
			}
		}
		return cells;
	}

	/** @returns {Block[]} */
	blocks() {
		return this.anchor === null ? [] : [...this.kept, this.block(this.anchor, this.focus)];
	}

	/** The cells taken marked — tinted when more than one is, the current one framed — and the figures summed. */
	paint() {
		/** @type {Set<HTMLTableCellElement>} */
		const taken = new Set();
		for (const block of this.blocks()) {
			for (const cell of this.cellsIn(block))
				taken.add(cell);
		}
		const tinted = taken.size > 1 ? taken : new Set();
		for (const cell of this.marked) {
			if (!tinted.has(cell))
				cell.classList.remove('selected');
		}
		for (const cell of tinted)
			cell.classList.add('selected');
		this.marked = tinted;
		this.current?.classList.remove('current');
		this.current = this.anchor === null ? undefined : this.cells.get(key(this.anchor));
		this.current?.classList.add('current');
		this.sum([...taken].filter((cell) => !/** @type {HTMLElement} */ (cell.parentElement).hidden));
	}

	/** @param {HTMLTableCellElement[]} cells the cells taken and seen */
	sum(cells) {
		let count = 0;
		let figures = 0;
		let total = 0;
		let digits = 0;
		for (const cell of cells) {
			const text = cell.textContent?.trim() ?? '';
			if (text === '')
				continue;
			count++;
			const figure = figureOf(text);
			if (figure === undefined)
				continue;
			figures++;
			total += figure.value;
			digits = Math.max(digits, figure.digits);
		}
		this.summary.hidden = count < 2;
		this.summary.textContent = figures === 0 ? `Count: ${count}`
			: `Average: ${(total / figures).toFixed(digits)}    Count: ${count}    Sum: ${total.toFixed(digits)}`;
	}

	/** What is taken onto the clipboard: the rows and columns the blocks cover, a place not taken left empty. */
	copy() {
		const blocks = this.blocks();
		if (blocks.length === 0)
			return;
		const top = Math.min(...blocks.map((block) => block.top));
		const left = Math.min(...blocks.map((block) => block.left));
		const bottom = Math.max(...blocks.map((block) => block.bottom));
		const right = Math.max(...blocks.map((block) => block.right));
		const taken = (/** @type {number} */ row, /** @type {number} */ col) => blocks.some((block) =>
			row >= block.top && row <= block.bottom && col >= block.left && col <= block.right);
		/** @type {string[][]} */
		const lines = [];
		for (let row = top; row <= bottom; row++) {
			if (this.rows[row].hidden)
				continue;
			const line = [];
			for (let col = left; col <= right; col++) {
				const cell = this.cells.get(`${row}:${col}`);
				// A spanning cell says its text once, at its first place.
				const first = cell !== undefined && placeOf(cell).row === row && placeOf(cell).col === col;
				line.push(first && taken(row, col) ? (cell.textContent ?? '') : '');
			}
			lines.push(line);
		}
		const text = lines.map((line) => line.join('\t')).join('\r\n');
		const html = `<table>${lines.map((line) => `<tr>${line.map(htmlCell).join('')}</tr>`).join('')}</table>`;
		const put = (/** @type {ClipboardEvent} */ event) => {
			event.clipboardData?.setData('text/plain', text);
			event.clipboardData?.setData('text/html', html);
			event.preventDefault();
		};
		// The copy event of the page, not the clipboard's promise: it asks for no secure origin.
		document.addEventListener('copy', put, { once: true });
		document.execCommand('copy');
		document.removeEventListener('copy', put);
	}
}

/** @param {HTMLTableCellElement} cell @returns {Place} */
function placeOf(cell) {
	return { row: Number(cell.dataset.row), col: Number(cell.dataset.col) };
}

/** @param {Place} place */
function key(place) {
	return `${place.row}:${place.col}`;
}

/**
 * A cell's text read as a figure — spaces between its thousands, a point or a comma before its fraction — and how many
 * digits that fraction has; undefined for a text that is no figure.
 * @param {string} text
 */
function figureOf(text) {
	let plain = text.replace(/[\s  ]/g, '');
	plain = plain.includes('.') ? plain.replaceAll(',', '') : plain.replace(',', '.');
	if (!/^[-+]?\d+(\.\d+)?$/.test(plain))
		return undefined;
	return { value: Number(plain), digits: plain.split('.')[1]?.length ?? 0 };
}

/** A cell of the table copied; a figure carries its value, so a spreadsheet program reads it as a number. */
function htmlCell(/** @type {string} */ text) {
	const escaped = text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
	const figure = figureOf(text.trim());
	return figure === undefined ? `<td>${escaped}</td>` : `<td x:num="${figure.value}">${escaped}</td>`;
}
