// A SETTING'S RUN OF LINES — each a field and what is said of it: the SORT (a direction — the thin client's
// ibSortEditor), the GROUPING (how a field unfolds — the list's group page, a grouping level's own), a LIST OF FIELDS
// (those a rule of the conditional appearance dresses), the SELECTED FIELDS (such a list, and «Auto» — what the storey
// above chose, put in this place). The lines are the node's own array, edited in place: added at the end, deleted,
// moved; a double click on a field adds a line of it.

import { _ } from '../localization.js';
import { SettingsEditor, lineTools, moveIn, select } from './settingsEditor.js';

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

// What a selected line is (ibSelectedFieldKind): a field, or «Auto» — what the storey above chose, in this place.
const SelectedKind = Object.freeze({ Field: 0, Auto: 1 });

/** The fields a storey shows (CompositionField {Path, Kind — absent for a field, 1 for «Auto»}). */
export class SelectedFieldsEditor extends FieldListEditor {
	/** @param {SettingsFieldTree} fields */
	constructor(fields) {
		super(fields, [{
			caption: _('Auto'), help: _('Everything the level above shows, in this place'),
			press: () => this.append({ NodeType: 'CompositionField', Kind: SelectedKind.Auto }),
		}]);
	}

	/** @param {any} line */
	cells(line) {
		if (line.Kind === SelectedKind.Auto) {
			const auto = document.createElement('span');
			auto.className = 'settings-auto';
			auto.textContent = _('Auto');
			return [auto];
		}
		return super.cells(line);
	}
}
