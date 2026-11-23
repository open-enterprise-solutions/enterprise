// A SETTING'S RUN OF LINES — each a field and what is said of it: the SORT (a direction — the thin client's
// ibSortEditor), the GROUPING (how a field unfolds — the list's group page, a grouping level's own), a LIST OF FIELDS
// (those a rule of the conditional appearance dresses), the SELECTED FIELDS (such a list, «Auto» — what the storey
// above chose, put in this place — and groups of them, a tree). The lines are the node's own array, edited in place:
// added at the end, deleted, moved; a double click on a field adds a line of it.

import { _, language } from '../localization.js';
import { editTranslate, readingOf, translated } from '../properties/translate.js';
import { SettingsEditor, lineTools, moveIn, popupMenu, select, smallButton } from './settingsEditor.js';

/** @typedef {import('./fieldTree.js').SettingsFieldTree} SettingsFieldTree */
/** @typedef {import('./fieldTree.js').Field} Field */
/** @typedef {import('./settingsEditor.js').Tool} Tool */

export class LineEditor extends SettingsEditor {
	/**
	 * @param {SettingsFieldTree} fields
	 * @param {[string, string][]} columns
	 * @param {Tool[]} [more] tools beside the run's own, after New item
	 */
	constructor(fields, columns, more = []) {
		super(fields, columns);
		/** @type {any[]} the lines edited */
		this.lines = [];
		/** @type {any} the line taken */
		this.taken = null;
		const tools = lineTools(() => this.add(this.fields?.taken ?? null), () => this.remove(), (step) => this.move(step));
		tools.splice(1, 0, ...more);
		this.build(tools);
	}

	draw() {
		this.drawRows(this.lines.map((line) => {
			const row = this.lineRow(line === this.taken, () => { this.taken = line; });
			for (const content of this.cells(line)) {
				const cell = document.createElement('td');
				cell.append(content);
				row.append(cell);
			}
			return row;
		}));
	}

	/** @param {Field} field */
	pick(field) {
		this.add(field);
	}

	/**
	 * A line made of a field.
	 * @param {Field} field
	 * @returns {any}
	 */
	lineOf(field) {
		return { Field: field.path };
	}

	/**
	 * What a line shows, a cell each.
	 * @param {any} line
	 * @returns {(HTMLElement | string)[]}
	 */
	cells(line) {
		return [String(line.Field ?? '')];
	}

	/**
	 * The lines to edit — a node's array, edited in place.
	 * @param {any[]} lines
	 */
	show(lines) {
		this.lines = lines;
		this.taken = null;
		this.draw();
	}

	/**
	 * A line of the field — the one taken, or the first — added at the end, taken.
	 * @param {Field | null} field
	 */
	add(field) {
		const chosen = field ?? this.fields?.allFields()[0];
		if (chosen !== undefined)
			this.append(this.lineOf(chosen));
	}

	/** @param {any} line */
	append(line) {
		this.lines.push(line);
		this.taken = line;
		this.draw();
	}

	/** The line taken removed, the next one taken. */
	remove() {
		const at = this.lines.indexOf(this.taken);
		if (at < 0)
			return;
		this.lines.splice(at, 1);
		this.taken = this.lines[Math.min(at, this.lines.length - 1)] ?? null;
		this.draw();
	}

	/** @param {number} step */
	move(step) {
		if (moveIn(this.lines, this.taken, step))
			this.draw();
	}
}

/** The sort: a field, ascending or descending (CompositionOrderLine {Field, Ascending 1 / 0}). */
export class SortEditor extends LineEditor {
	/** @param {SettingsFieldTree} fields */
	constructor(fields) {
		super(fields, [[_('Field'), ''], [_('Direction'), '9rem']]);
	}

	/** @param {Field} field */
	lineOf(field) {
		return { NodeType: 'CompositionOrderLine', Field: field.path, Ascending: 1 };
	}

	/** @param {any} line */
	cells(line) {
		return [
			this.fieldSelect(String(line.Field ?? ''), '', (field) => { line.Field = field.path; }),
			select([[1, _('Ascending')], [0, _('Descending')]], line.Ascending === 0 ? 0 : 1,
				(ascending) => { line.Ascending = ascending; }),
		];
	}
}

// How a grouping line unfolds (ibQueryDimUnfold) — said when drawn, in the person's language.
const unfolds = () => [_('Elements'), _('Elements and hierarchy'), _('Hierarchy only')];

/** The grouping: a field and how it unfolds (CompositionGroupLine {Field, Kind}). */
export class GroupEditor extends LineEditor {
	/**
	 * @param {SettingsFieldTree} fields
	 * @param {() => void} [changed] what the lines group by changed — a level's title follows it
	 */
	constructor(fields, changed = () => {}) {
		super(fields, [[_('Field'), ''], [_('Kind'), '12rem']]);
		this.changed = changed;
	}

	/** @param {Field} field */
	lineOf(field) {
		return { NodeType: 'CompositionGroupLine', Field: field.path, Kind: 0 };
	}

	/** @param {any} line */
	cells(line) {
		return [
			this.fieldSelect(String(line.Field ?? ''), '', (field) => {
				line.Field = field.path;
				this.changed();
			}),
			select(unfolds().map((caption, at) => [at, caption]), Number(line.Kind ?? 0), (kind) => { line.Kind = kind; }),
		];
	}

	draw() {
		super.draw();
		this.changed();
	}
}

/** A list of fields (CompositionField {Path}) — those a rule of the conditional appearance dresses. */
export class FieldListEditor extends LineEditor {
	/**
	 * @param {SettingsFieldTree} fields
	 * @param {Tool[]} [more]
	 */
	constructor(fields, more = []) {
		super(fields, [[_('Field'), '']], more);
	}

	/** @param {Field} field */
	lineOf(field) {
		return { NodeType: 'CompositionField', Path: field.path };
	}

	/**
	 * @param {any} line
	 * @returns {(HTMLElement | string)[]}
	 */
	cells(line) {
		return [this.fieldSelect(String(line.Path ?? ''), '', (field) => { line.Path = field.path; })];
	}
}

// What a selected line is (ibSelectedFieldKind): a field, «Auto» — what the storey above chose, in this place — or a
// GROUP: a title, a placement and lines of its own, as a table's column group.
const SelectedKind = Object.freeze({ Field: 0, Auto: 1, Group: 2 });
// How a group lays its lines (ibColumnGroupKind), in the words a table's column group uses; a new one is vertical, as a
// table's new group is.
const placements = () => [_('Horizontal'), _('Vertical'), _('In cell')];
const vertical = 1;

/**
 * The fields a storey shows (CompositionField {Path, Kind — absent for a field, 1 for «Auto», 2 for a group; a group's
 * Title, Placement and its own lines under Selected}) — a TREE, as the thin client's (composerSettings.cpp,
 * ibSelectedListModel): a group holds lines, groups nest, and a line is carried by the mouse into a group, out of it,
 * within it.
 */
export class SelectedFieldsEditor extends LineEditor {
	/** @param {SettingsFieldTree} fields */
	constructor(fields) {
		super(fields, [[_('Field'), ''], [_('Title'), '10rem'], [_('Placement'), '8rem']], [
			{ caption: _('Add group'), help: _('Columns under one title: side by side, one under another, or in one cell'),
				press: () => this.addGroup() },
			{ caption: _('Auto'), help: _('Everything the level above shows, in this place'), press: () => this.addAuto() },
			{ caption: _('Copy'), help: _('Copy'), press: () => this.copy() },
		]);
		/** @type {any} the line carried by the mouse */
		this.carried = null;
		// PAST THE LAST LINE — the end of the table, for a line carried and a field from the tree alike; and the
		// commands with no line under them. A line's own drop and menu are its own (treeRow), and stop here.
		const holder = /** @type {HTMLElement} */ (this.table.parentElement);
		holder.addEventListener('dragover', (event) => {
			if (this.carried !== null || this.fields?.dragged)
				event.preventDefault();
		});
		holder.addEventListener('drop', (event) => {
			event.preventDefault();
			event.stopPropagation();
			this.land(this.lines, this.lines.length);
		});
		holder.addEventListener('contextmenu', (event) => {
			this.taken = null;
			this.draw();
			this.offerMenu(event);
		});
	}

	draw() {
		/** @type {HTMLTableRowElement[]} */
		const rows = [];
		const walk = (/** @type {any[]} */ lines, /** @type {number} */ depth) => {
			for (const line of lines) {
				rows.push(this.treeRow(line, lines, depth));
				if (line.Kind === SelectedKind.Group)
					walk(line.Selected?.NodeChildren ?? [], depth + 1);
			}
		};
		walk(this.lines, 0);
		this.drawRows(rows);
	}

	/** @param {Field} field */
	lineOf(field) {
		return { NodeType: 'CompositionField', Path: field.path };
	}

	/**
	 * A new line where the cursor says (the thin client's InsertFieldSetRow): into the group taken, at its end; after
	 * any other line taken, among its own; at the end of the table with none.
	 * @param {any} line
	 */
	append(line) {
		const taken = this.taken;
		if (taken?.Kind === SelectedKind.Group) {
			this.put(line, linesOf(taken), Infinity);
			return;
		}
		const run = taken !== null ? runOf(taken, this.lines) : null;
		if (run !== null)
			this.put(line, run, run.indexOf(taken) + 1);
		else
			this.put(line, this.lines, this.lines.length);
	}

	/** The line taken removed — the one that took its place taken, the one before it, or its group. */
	remove() {
		const run = this.taken !== null ? runOf(this.taken, this.lines) : null;
		if (run === null)
			return;
		const at = run.indexOf(this.taken);
		run.splice(at, 1);
		this.taken = run[Math.min(at, run.length - 1)] ?? ownerOf(run, this.lines);
		this.draw();
	}

	/**
	 * Within its own group — out of one and into another is the mouse's.
	 * @param {number} step
	 */
	move(step) {
		const run = this.taken !== null ? runOf(this.taken, this.lines) : null;
		if (run !== null && moveIn(run, this.taken, step))
			this.draw();
	}

	/** A group, titled so it reads as one before anything is in it. */
	addGroup() {
		this.append({ NodeType: 'CompositionField', Kind: SelectedKind.Group, Title: _('Group'), Placement: vertical });
	}

	/** «Auto» — one at most, anywhere in the tree: a second says nothing the first did not. */
	addAuto() {
		if (!inherits(this.lines))
			this.append({ NodeType: 'CompositionField', Kind: SelectedKind.Auto });
	}

	/** The line taken, again, after it — a group whole. */
	copy() {
		const run = this.taken !== null ? runOf(this.taken, this.lines) : null;
		if (run !== null)
			this.put(structuredClone(this.taken), run, run.indexOf(this.taken) + 1);
	}

	/**
	 * @param {any} line
	 * @param {any[]} run
	 * @param {number} index past the end — the end
	 */
	put(line, run, index) {
		run.splice(Math.min(index, run.length), 0, line);
		this.taken = line;
		this.draw();
	}

	/**
	 * What the mouse carried, put at `index` of `run`: a line of the table moved there, a field of the tree added there.
	 * @param {any[]} run
	 * @param {number} index
	 */
	land(run, index) {
		const field = this.fields?.dragged;
		if (this.carried !== null)
			this.carry(this.carried, run, index);
		else if (field)
			this.put(this.lineOf(field), run, index);
	}

	/**
	 * A line moved to `index` of `run` — a group never into itself, nor into a group under it: it would hang off a line
	 * it took away with it.
	 * @param {any} line
	 * @param {any[]} run
	 * @param {number} index
	 */
	carry(line, run, index) {
		if (line.Kind === SelectedKind.Group && holds(line, run))
			return;
		const from = runOf(line, this.lines);
		if (from === null)
			return;
		const at = from.indexOf(line);
		if (from === run && (index === at || index === at + 1))
			return;   // where it already is
		from.splice(at, 1);
		this.put(line, run, from === run && index > at ? index - 1 : index);
	}

	/**
	 * A line of the tree: its field — or «Auto», or a group — stepped in by its depth, after the grip it is carried by;
	 * a group's title and placement, edited where they stand.
	 * @param {any} line
	 * @param {any[]} run the lines it stands among
	 * @param {number} depth
	 */
	treeRow(line, run, depth) {
		const row = this.lineRow(line === this.taken, () => { this.taken = line; });
		const field = document.createElement('td');
		const stepped = document.createElement('div');
		stepped.className = 'settings-tree-line';
		stepped.style.paddingInlineStart = `${depth * 1.125}rem`;
		// THE GRIP, not the line: a line that is itself draggable takes the press its field's list and a group's title
		// need.
		const grip = document.createElement('span');
		grip.className = 'settings-grip';
		grip.textContent = '⋮';
		grip.draggable = true;
		grip.addEventListener('dragstart', (event) => {
			this.carried = line;
			event.dataTransfer?.setData('text/plain', '');
			event.dataTransfer?.setDragImage(row, 0, 0);
		});
		grip.addEventListener('dragend', () => { this.carried = null; });
		stepped.append(grip, this.fieldOf(line));
		field.append(stepped);
		const title = document.createElement('td');
		const placement = document.createElement('td');
		if (line.Kind === SelectedKind.Group) {
			// A TITLE IN EVERY LANGUAGE (translate.js): typed for the language in force, the others kept; the «…» opens
			// them all — the thin client's cell (composerSettings.cpp, EditFieldSetTitle). An empty title is a choice —
			// the group then draws no heading band.
			const text = document.createElement('input');
			text.type = 'text';
			text.value = readingOf(String(line.Title ?? ''), language());
			text.addEventListener('change', () => { line.Title = translated(String(line.Title ?? ''), text.value); });
			const part = document.createElement('div');
			part.className = 'settings-part';
			part.append(text, smallButton('…', _('Languages'), async () => {
				const edited = await editTranslate(_('Title'), String(line.Title ?? ''));
				if (edited === null)
					return;
				line.Title = edited;
				text.value = readingOf(edited, language());
			}));
			title.append(part);
			placement.append(select(placements().map((caption, at) => [at, caption]), Number(line.Placement ?? vertical),
				(kind) => { line.Placement = kind; }));
		}
		row.append(field, title, placement);
		row.addEventListener('dragover', (event) => {
			if (this.carried === null && !this.fields?.dragged)
				return;
			event.preventDefault();
			event.stopPropagation();
			hint(row, zoneOf(event, row, line));
		});
		row.addEventListener('dragleave', () => hint(row, ''));
		row.addEventListener('drop', (event) => {
			event.preventDefault();
			event.stopPropagation();
			hint(row, '');
			const zone = zoneOf(event, row, line);
			if (zone === 'into')
				this.land(linesOf(line), Infinity);
			else
				this.land(run, run.indexOf(line) + (zone === 'after' ? 1 : 0));
		});
		row.addEventListener('contextmenu', (event) => {
			event.stopPropagation();
			this.taken = line;
			this.draw();
			this.offerMenu(event);
		});
		return row;
	}

	/** @param {any} line */
	fieldOf(line) {
		if (line.Kind === SelectedKind.Auto || line.Kind === SelectedKind.Group) {
			const word = document.createElement('span');
			word.className = line.Kind === SelectedKind.Auto ? 'settings-auto' : 'settings-group';
			word.textContent = line.Kind === SelectedKind.Auto ? _('Auto') : _('Group');
			return word;
		}
		return this.fieldSelect(String(line.Path ?? ''), '', (field) => { line.Path = field.path; });
	}

	/**
	 * The tools' commands where the person pressed — the thin client's context menu of the list.
	 * @param {MouseEvent} event
	 */
	offerMenu(event) {
		const onLine = this.taken !== null;
		popupMenu(event, this.element, [
			{ caption: _('Add field'), press: () => this.add(this.fields?.taken ?? null) },
			{ caption: _('Add group'), press: () => this.addGroup() },
			{ caption: _('Add "Auto"'), enabled: !inherits(this.lines), press: () => this.addAuto() },
			{ caption: _('Delete'), enabled: onLine, press: () => this.remove() },
			{ caption: _('Copy'), enabled: onLine, press: () => this.copy() },
			{ caption: _('Move up'), enabled: onLine, press: () => this.move(-1) },
			{ caption: _('Move down'), enabled: onLine, press: () => this.move(1) },
		]);
	}
}

/**
 * A group's own lines — made when one is put in.
 * @param {any} group
 * @returns {any[]}
 */
function linesOf(group) {
	return (group.Selected ??= {}).NodeChildren ??= [];
}

/**
 * The run that holds a line, searched down from `lines`; null — none.
 * @param {any} line
 * @param {any[]} lines
 * @returns {any[] | null}
 */
function runOf(line, lines) {
	if (lines.includes(line))
		return lines;
	for (const one of lines) {
		const run = one.Kind === SelectedKind.Group ? runOf(line, one.Selected?.NodeChildren ?? []) : null;
		if (run !== null)
			return run;
	}
	return null;
}

/**
 * The group whose lines `run` is, searched down from `lines`; null — the table's own.
 * @param {any[]} run
 * @param {any[]} lines
 * @returns {any}
 */
function ownerOf(run, lines) {
	for (const one of lines) {
		if (one.Kind !== SelectedKind.Group)
			continue;
		const own = one.Selected?.NodeChildren ?? [];
		if (own === run)
			return one;
		const deeper = ownerOf(run, own);
		if (deeper !== null)
			return deeper;
	}
	return null;
}

/**
 * Is `run` a group's own lines, or a group's under it?
 * @param {any} group
 * @param {any[]} run
 * @returns {boolean}
 */
function holds(group, run) {
	const own = group.Selected?.NodeChildren ?? [];
	return own === run || own.some((/** @type {any} */ one) => one.Kind === SelectedKind.Group && holds(one, run));
}

/**
 * Does the run hold «Auto», anywhere in its groups (the thin client's ibSelectedInherits)?
 * @param {any[]} lines
 * @returns {boolean}
 */
function inherits(lines) {
	return lines.some((line) => line.Kind === SelectedKind.Auto
		|| (line.Kind === SelectedKind.Group && inherits(line.Selected?.NodeChildren ?? [])));
}

/**
 * Where on a line the mouse lets go (the thin client's drop): the middle of a group — into it, at the end; the upper
 * part of any line — before it, the lower — after it.
 * @param {DragEvent} event
 * @param {HTMLElement} row
 * @param {any} line
 */
function zoneOf(event, row, line) {
	const box = row.getBoundingClientRect();
	const share = (event.clientY - box.top) / Math.max(1, box.height);
	if (line.Kind === SelectedKind.Group && share > 0.25 && share < 0.75)
		return 'into';
	return share < 0.5 ? 'before' : 'after';
}

/**
 * Where a carried line would land, drawn on the line under the pointer; '' — nowhere.
 * @param {HTMLElement} row
 * @param {string} zone
 */
function hint(row, zone) {
	row.classList.toggle('drop-before', zone === 'before');
	row.classList.toggle('drop-after', zone === 'after');
	row.classList.toggle('drop-into', zone === 'into');
}
