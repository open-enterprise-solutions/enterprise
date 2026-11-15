// A PROPERTY GRID — a property set drawn, as the thin client's (ibPropertyGridManager): a line a property, its title
// beside its cell, the two columns halves of it (ibPG_SPLITTER_AUTO_CENTER); a category — a row with no Class — a line
// of its own over its rows, folded by a press. The line taken says its title and Help under the grid (ibPG_DESCRIPTION).
// It holds no values: a cell's edit goes to whoever showed the grid as the row changed — its Value and how it reads —
// and the form editor queues it; a row changed and not taken yet is shown as changed, bold (ibPG_BOLD_MODIFIED).

import { cellOf } from './index.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

export class PropertyGrid {
	/** @param {(row: ProtocolNode, changed: ProtocolNode) => void} change a cell's edit: the row, and the row as changed */
	constructor(change) {
		this.change = change;
		this.element = document.createElement('div');
		this.element.className = 'property-grid';
		this.lines = document.createElement('div');
		this.lines.className = 'property-lines';
		this.description = document.createElement('div');
		this.description.className = 'property-description';
		this.element.append(this.lines, this.description);
		/** @type {WeakMap<Element, ProtocolNode>} a line → its row */
		this.rows = new WeakMap();
		/** @type {Element | null} the line taken */
		this.taken = null;
		const take = (/** @type {Event} */ event) => this.take(/** @type {Element} */ (event.target).closest('.property'));
		this.lines.addEventListener('pointerdown', take);
		this.lines.addEventListener('focusin', take);
		// Enter in a cell is the cell's — it takes what was typed — not the dialog's default button.
		this.lines.addEventListener('keydown', (event) => {
			if (event.key === 'Enter')
				event.stopPropagation();
		});
	}

	/**
	 * The rows drawn, none taken.
	 * @param {ProtocolNode[]} rows
	 * @param {Map<ProtocolNode, ProtocolNode>} [changed] the rows changed and not taken yet — each shown as changed
	 */
	show(rows, changed = new Map()) {
		this.taken = null;
		this.lines.replaceChildren(...rows.map((row) => row.Class === undefined ? this.category(row, changed) : this.line(row, changed)));
		this.describe(null);
	}

	/**
	 * A property's line: its title, its cell.
	 * @param {ProtocolNode} row
	 * @param {Map<ProtocolNode, ProtocolNode>} changed
	 */
	line(row, changed) {
		const line = document.createElement('div');
		line.className = 'property';
		line.classList.toggle('modified', changed.has(row));
		const title = document.createElement('span');
		title.className = 'property-title';
		title.textContent = row.Title || row.Name || '';
		const cell = cellOf(changed.get(row) ?? row, (value, presentation) => {
			line.classList.add('modified');
			this.change(row, { ...row, Value: value, Presentation: presentation ?? row.Presentation });
		});
		cell.classList.add('property-cell');
		line.append(title, cell);
		this.rows.set(line, row);
		return line;
	}

	/**
	 * A category's line over its rows; pressed, they fold.
	 * @param {ProtocolNode} row
	 * @param {Map<ProtocolNode, ProtocolNode>} changed
	 */
	category(row, changed) {
		const group = document.createElement('div');
		group.className = 'property-category';
		const heading = document.createElement('button');
		heading.type = 'button';
		heading.setAttribute('aria-expanded', 'true');
		heading.textContent = row.Title || row.Name || '';
		const inside = document.createElement('div');
		inside.append(...(row.NodeChildren ?? []).map((one) => one.Class === undefined ? this.category(one, changed) : this.line(one, changed)));
		heading.addEventListener('click', () => {
			inside.hidden = !inside.hidden;
			heading.setAttribute('aria-expanded', String(!inside.hidden));
		});
		group.append(heading, inside);
		return group;
	}

	/** @param {Element | null} line */
	take(line) {
		if (line === this.taken)
			return;
		this.taken?.classList.remove('taken');
		this.taken = line;
		line?.classList.add('taken');
		this.describe(line !== null ? this.rows.get(line) ?? null : null);
	}

	/** What the line taken is — its title and its Help. @param {ProtocolNode | null} row */
	describe(row) {
		const title = document.createElement('strong');
		title.textContent = row !== null ? row.Title || row.Name || '' : '';
		const help = document.createElement('p');
		help.textContent = row?.Help ?? '';
		this.description.replaceChildren(title, help);
	}
}
