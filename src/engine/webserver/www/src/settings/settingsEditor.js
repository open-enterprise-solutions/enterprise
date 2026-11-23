// A PAGE OF A SETTING — the fields beside its lines (the thin client's editors each have the field list on their
// left, shared by them all): the tools over a table, pressed or by their keys in it (the thin client's); a double
// click on a field takes it in, and so does the field dragged onto the lines. What the lines are and what a line holds is the editor's; the cells a line is made
// of — a field, a choice of words, a flag, a value by its type, a reference chosen by the server — are here, one
// spelling for every editor.

import { _ } from '../localization.js';
import { emptyDate, Type } from './fieldTree.js';

/** @typedef {import('./fieldTree.js').SettingsFieldTree} SettingsFieldTree */
/** @typedef {import('./fieldTree.js').Field} Field */
/**
 * A tool over a page's lines: its caption, what it does with its key, the key, the press.
 * @typedef {{caption: string, help: string, key?: string, press: () => void}} Tool
 */
/**
 * A value chosen by the server — the window's act: the type the cell stands on, written as a field's (one class as
 * `{Types: [{TypeId}]}`), the value in the cell, told back with what was chosen; false when the window is busy with
 * another act.
 * @typedef {(type: object, value: object | undefined,
 *   done: (value: object, presentation: string) => void) => boolean} Choose
 */

export class SettingsEditor {
	/**
	 * @param {SettingsFieldTree | null} fields the window's fields — none for a page that takes no field
	 * @param {[string, string][]} columns each caption and width; the one said none takes what is left
	 */
	constructor(fields, columns) {
		this.fields = fields;
		this.table = document.createElement('table');
		this.table.className = 'settings-table';
		const head = this.table.createTHead().insertRow();
		for (const [caption, width] of columns) {
			const cell = document.createElement('th');
			cell.textContent = caption;
			cell.title = caption;
			cell.style.width = width;
			head.append(cell);
		}
		this.table.createTBody();
		/** @type {HTMLElement} the page — made by `build`, once the editor knows its tools */
		this.element = document.createElement('div');
		fields?.listen(() => this.draw());
	}

	/** The lines drawn again. */
	draw() {
	}

	/**
	 * A field taken in by a double click on it.
	 * @param {Field} field
	 */
	pick(field) {
	}

	/**
	 * The page: the fields beside the tools over the table — or the table alone, for a page that takes no field.
	 * @param {Tool[]} tools
	 * @param {boolean} [withFields]
	 */
	build(tools, withFields = true) {
		this.element.className = withFields && this.fields !== null ? 'settings-page' : 'settings-page settings-page-alone';
		if (withFields && this.fields !== null) {
			const fields = document.createElement('div');
			fields.className = 'settings-fields';
			const title = document.createElement('div');
			title.className = 'settings-pane-title';
			title.textContent = _('Available fields');
			fields.append(title, this.fields.pane((field) => this.pick(field)));
			this.element.append(fields);
		}
		const editor = document.createElement('div');
		editor.className = 'settings-editor';
		const bar = document.createElement('div');
		bar.className = 'settings-tools';
		bar.append(...tools.map((tool) => smallButton(tool.caption, tool.help, tool.press)));
		const holder = document.createElement('div');
		holder.className = 'settings-table-holder';
		holder.tabIndex = 0;
		holder.append(this.table);
		holder.addEventListener('keydown', (event) => {
			// A field's own keys stay its own: Delete in a text deletes text.
			const target = /** @type {HTMLElement} */ (event.target);
			if ((target.localName === 'input' && /** @type {HTMLInputElement} */ (target).type !== 'checkbox')
				|| target.localName === 'select')
				return;
			const key = `${event.ctrlKey ? 'Ctrl+' : ''}${event.shiftKey ? 'Shift+' : ''}${event.key}`;
			const tool = tools.find((one) => one.key === key);
			if (tool === undefined)
				return;
			event.preventDefault();
			tool.press();
		});
		if (tools.length > 0)
			editor.append(bar);
		editor.append(holder);
		// A field dragged from the tree and dropped on the lines joins them — the double click's verb, the thin
		// client's drop target on the right-hand pane of every page that takes a field.
		if (withFields && this.fields !== null) {
			editor.addEventListener('dragover', (event) => {
				if (this.fields?.dragged)
					event.preventDefault();
			});
			editor.addEventListener('drop', (event) => {
				const field = this.fields?.dragged;
				if (!field)
					return;
				event.preventDefault();
				this.pick(field);
			});
		}
		this.element.append(editor);
	}

	/** @param {HTMLTableRowElement[]} rows */
	drawRows(rows) {
		this.table.tBodies[0].replaceChildren(...rows);
	}

	/**
	 * A line of the table, taken by a press.
	 * @param {boolean} taken
	 * @param {() => void} take
	 */
	lineRow(taken, take) {
		const row = document.createElement('tr');
		row.classList.toggle('taken', taken);
		row.addEventListener('click', () => {
			take();
			for (const one of /** @type {HTMLCollectionOf<HTMLElement>} */ (row.parentElement?.children ?? []))
				one.classList.toggle('taken', one === row);
		});
		return row;
	}

	/**
	 * The field of a line — every field known; one the setting names and the source no longer offers, kept as it reads.
	 * @param {string} path
	 * @param {string} text how it read when it was set
	 * @param {(field: Field) => void} chosen
	 */
	fieldSelect(path, text, chosen) {
		const fields = this.fields?.allFields() ?? [];
		const select = document.createElement('select');
		if (!fields.some((field) => field.path === path)) {
			const kept = document.createElement('option');
			kept.value = path;
			kept.textContent = text || path;
			select.append(kept);
		}
		for (const field of fields) {
			const option = document.createElement('option');
			option.value = field.path;
			option.textContent = field.title;
			select.append(option);
		}
		select.value = path;
		select.addEventListener('change', () => {
			const field = this.fields?.fieldAt(select.value);
			if (field !== undefined)
				chosen(field);
		});
		return select;
	}

	/**
	 * A value in its cell, typed by its type: a flag, a number, a date, a text; a reference — how it reads, «…» to choose
	 * one (the server's choice), «×» for the empty one of its kind; any other type — a characteristic's, whose values are
	 * of the types it lists — as a reference, chosen by the type of the value held (the thin client's), else its own; a
	 * field's type whole (several types) — as a reference too, the type asked by the server when none is held.
	 * @param {string | object} type
	 * @param {any} value the packed value
	 * @param {string} text how a reference reads
	 * @param {(value: object | undefined, text: string) => void} write
	 * @param {Choose} choose
	 */
	valueCell(type, value, text, write, choose) {
		if (typeof type !== 'string' || this.fields?.references.has(type))
			return this.referenceCell(type, value, text, write, choose);
		/** @param {any} v */
		const put = (v) => write({ t: type, v }, '');
		const input = document.createElement('input');
		if (type === Type.Boolean) {
			input.type = 'checkbox';
			input.checked = value?.v === true;
			input.addEventListener('change', () => put(input.checked));
			return input;
		}
		if (type === Type.Number) {
			input.type = 'number';
			input.step = 'any';
			input.value = value?.v !== undefined ? String(value.v) : '';
			input.addEventListener('change', () => {
				if (Number.isFinite(input.valueAsNumber))
					put(input.valueAsNumber);
			});
			return input;
		}
		if (type === Type.Date) {
			input.type = 'datetime-local';
			input.step = '1';
			input.value = value?.v && value.v !== emptyDate ? String(value.v).slice(0, 19).replace(' ', 'T') : '';
			// Seconds left out when they are naught ("2026-10-23T14:05").
			input.addEventListener('change', () => {
				const [date = '', time = ''] = input.value.split('T');
				put(date === '' ? emptyDate : `${date} ${`${time}:00:00`.slice(0, 8)}`);
			});
			return input;
		}
		if (type !== Type.String)
			return this.referenceCell(value?.t ?? type, value, text, write, choose);
		input.type = 'text';
		input.value = value?.v !== undefined ? String(value.v) : '';
		input.addEventListener('change', () => put(input.value));
		return input;
	}

	/**
	 * @param {string | object} type
	 * @param {any} value
	 * @param {string} text
	 * @param {(value: object | undefined, text: string) => void} write
	 * @param {Choose} choose
	 */
	referenceCell(type, value, text, write, choose) {
		const cell = document.createElement('div');
		cell.className = 'settings-reference';
		const said = document.createElement('span');
		said.textContent = text;
		// The kind's empty reference — what «none» is here, as a new line's; a cell of several types has none — cleared,
		// it stands on no type again.
		const empty = typeof type === 'string' ? this.fields?.emptyOf(type) ?? {} : undefined;
		cell.append(said,
			smallButton('…', _('Select'), () => {
				// Drawn again with it: the lines may have been drawn anew while it was chosen. The type goes as a field's is
				// written, one class too; the server takes the value's when the cell holds one (the thin client's ChooseValue).
				choose(typeof type === 'string' ? { Types: [{ TypeId: type }] } : type, value ?? empty, (chosen, presentation) => {
					write(chosen, presentation);
					this.draw();
				});
			}),
			smallButton('×', _('Clear'), () => {
				write(empty, '');
				said.textContent = '';
			}));
		return cell;
	}
}

/**
 * @param {[number, string][]} options
 * @param {number} value
 * @param {(value: number) => void} chosen
 */
export function select(options, value, chosen) {
	const select = document.createElement('select');
	for (const [id, caption] of options) {
		const option = document.createElement('option');
		option.value = String(id);
		option.textContent = caption;
		select.append(option);
	}
	select.value = String(value);
	select.addEventListener('change', () => chosen(Number(select.value)));
	return select;
}

/**
 * @param {boolean} checked
 * @param {(checked: boolean) => void} changed
 */
export function checkbox(checked, changed) {
	const box = document.createElement('input');
	box.type = 'checkbox';
	box.checked = checked;
	box.addEventListener('change', () => changed(box.checked));
	return box;
}

/**
 * A flat button of the thin client's 16×16 toolbars.
 * @param {string} caption
 * @param {string} label
 * @param {() => void} press
 */
export function smallButton(caption, label, press) {
	const button = document.createElement('button');
	button.type = 'button';
	button.textContent = caption;
	button.title = label;
	button.addEventListener('click', press);
	return button;
}

/**
 * A page's commands where the person pressed (the thin client's context menus on the settings pages): an entry's
 * caption, greyed when it may not be pressed now, the press. Put in the page, not in the document — a window's page
 * stands in a modal dialog, and what is outside it does not take the pointer.
 * @param {MouseEvent} event
 * @param {HTMLElement} page
 * @param {{caption: string, enabled?: boolean, press: () => void}[]} entries
 */
export function popupMenu(event, page, entries) {
	event.preventDefault();
	const menu = /** @type {any} */ (document.createElement('div'));
	menu.className = 'popup-menu';
	menu.popover = 'auto';
	menu.append(...entries.map((entry) => {
		const item = document.createElement('button');
		item.type = 'button';
		item.textContent = entry.caption;
		item.disabled = entry.enabled === false;
		item.addEventListener('click', () => {
			menu.hidePopover();
			entry.press();
		});
		return item;
	}));
	menu.addEventListener('toggle', (/** @type {any} */ toggled) => {
		if (toggled.newState === 'closed')
			menu.remove();
	});
	const show = () => {
		page.append(menu);
		menu.showPopover();
		const size = menu.getBoundingClientRect();
		menu.style.left = `${Math.max(0, Math.min(event.clientX, innerWidth - size.width))}px`;
		menu.style.top = `${Math.max(0, Math.min(event.clientY, innerHeight - size.height))}px`;
	};
	// Linux and macOS ask for the menu as the button goes down, Windows as it comes up: it opens once the button is up,
	// the release being what closes a menu opened under it (the table's context menu).
	if (event.buttons === 0)
		show();
	else
		document.addEventListener('pointerup', () => setTimeout(show), { once: true, capture: true });
}

/**
 * The tools every editor of lines has — added, deleted, moved; the thin client's keys.
 * @param {() => void} add
 * @param {() => void} remove
 * @param {(step: number) => void} move
 * @returns {Tool[]}
 */
export function lineTools(add, remove, move) {
	return [
		{ caption: _('New item'), help: _('New item (Ins)'), key: 'Insert', press: add },
		{ caption: _('Delete'), help: _('Delete (Del)'), key: 'Delete', press: remove },
		{ caption: '↑', help: _('Move up (Ctrl+Shift+Up)'), key: 'Ctrl+Shift+ArrowUp', press: () => move(-1) },
		{ caption: '↓', help: _('Move down (Ctrl+Shift+Down)'), key: 'Ctrl+Shift+ArrowDown', press: () => move(1) },
	];
}

/**
 * A line moved among its siblings; false at an end.
 * @param {any[]} lines
 * @param {any} line
 * @param {number} step
 */
export function moveIn(lines, line, step) {
	const at = lines.indexOf(line);
	const to = at + step;
	if (at < 0 || to < 0 || to >= lines.length)
		return false;
	[lines[at], lines[to]] = [lines[to], lines[at]];
	return true;
}
