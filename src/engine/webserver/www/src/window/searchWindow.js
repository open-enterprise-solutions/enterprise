// ADVANCED SEARCH — a table's settings window over one condition (the server's question Search, the thin client's
// ibDialogSearch): where to look — the columns shown, as a settings window's fields, the cursor's chosen; what for — a
// value of the column's type in a settings window's value cell (a reference chosen by the server on the way), or a
// text; how — by the value, from the start of the line, anywhere in it, the whole of it. A column holding no text is
// looked in by its value alone. Find answers the three (Column, How, Value), Close nothing.

import { _ } from '../localization.js';
import { Type } from '../settings/fieldTree.js';
import { SettingsEditor } from '../settings/settingsEditor.js';
import { SettingsWindow } from '../settings/settingsWindow.js';

/** @typedef {import('./questions.js').Questions} Questions */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../settings/fieldTree.js').Field} Field */

// How it is looked for (ibSearch::ibHow).
const How = Object.freeze({ Begins: 1, Contains: 2, Exact: 3, Value: 4 });

export class SearchWindow extends SettingsWindow {
	/**
	 * @param {Questions} questions what it is asked through
	 * @param {ProtocolNode} request
	 */
	constructor(questions, request) {
		super(questions, request);
		/** the value cell — a settings window's */
		this.cells = new SettingsEditor(this.fields, []);
		// What — what the current row holds in each column, and how it reads: the column picked takes its own (pickColumn),
		// the cursor's first.
		/** @type {Map<number, {held: object | undefined, text: string}>} */
		this.rowValues = new Map((request.Fields?.NodeChildren ?? []).map((/** @type {any} */ one) => {
			const { Presentation: presentation, ...held } = one.Value ?? {};
			return [Number(one.Id ?? -1), { held: held.t !== undefined ? held : undefined, text: String(presentation ?? '') }];
		}));
		/** @type {object | undefined} what By value looks for — none: nothing */
		this.held = undefined;
		/** how the value held reads */
		this.heldText = '';
		/** what the other ways look for */
		this.text = '';
		this.where = document.createElement('select');
		for (const field of this.fields.fields) {
			const option = document.createElement('option');
			option.value = String(field.id);
			option.textContent = field.title;
			option.selected = field.id === request.Column;
			this.where.append(option);
		}
		this.where.addEventListener('change', () => this.pickColumn());
		this.what = document.createElement('div');
		this.what.className = 'search-what';
		this.how = document.createElement('fieldset');
		const legend = document.createElement('legend');
		legend.textContent = _('How to look');
		this.how.append(legend);
		const ways = /** @type {[number, string][]} */ ([[How.Value, _('By value')],
			[How.Begins, _('From the start of the line')], [How.Contains, _('Anywhere in the line')],
			[How.Exact, _('Exact match')]]);
		/** @type {HTMLInputElement[]} */
		this.ways = ways.map(([way, caption]) => {
			const radio = document.createElement('input');
			radio.type = 'radio';
			radio.name = 'search-how';
			radio.value = String(way);
			radio.checked = way === (request.How ?? How.Contains);
			const label = document.createElement('label');
			label.append(radio, caption);
			this.how.append(label);
			return radio;
		});
		this.how.addEventListener('change', () => this.drawWhat());
		this.open(_('Advanced search'), 'search-window', [_('Find'), _('Close')]);
		this.pickColumn();
	}

	content() {
		const fields = document.createElement('div');
		fields.className = 'search-fields';
		/** @param {string} caption @param {HTMLElement} field */
		const labelled = (caption, field) => {
			const label = document.createElement('label');
			label.append(caption, field);
			return label;
		};
		fields.append(labelled(_('Where to look:'), this.where), labelled(_('What to look for:'), this.what), this.how);
		return fields;
	}

	/** The column, the way, and what it looks for: the value packed By value, else the text. */
	response() {
		const way = this.way();
		/** @type {Record<string, any>} */
		const answer = { Column: Number(this.where.value), How: way };
		if (way !== How.Value)
			answer.Value = this.text;
		else if (this.held !== undefined)
			answer.Value = this.held;
		return answer;
	}

	/** @returns {Field | undefined} the column picked */
	field() {
		return this.fields.fields.find((field) => String(field.id) === this.where.value);
	}

	/** @returns {number} the way checked */
	way() {
		return Number(this.ways.find((radio) => radio.checked)?.value ?? How.Contains);
	}

	/**
	 * The column picked: what the current row holds in it taken — by its value, and as it reads for the other ways — and
	 * the ways it is looked in enabled, a column holding no text by its value alone.
	 */
	pickColumn() {
		const field = this.field();
		const types = field?.types ?? [];
		const row = field !== undefined ? this.rowValues.get(field.id) : undefined;
		this.held = row?.held;
		this.heldText = row?.text ?? '';
		this.text = this.heldText;
		// A type a text is looked for in — a text, one nobody declared, a reference (by the fields of it that hold text).
		const text = field === undefined || types.length === 0 || types.includes(Type.String) || this.fields.isReference(field);
		for (const radio of this.ways) {
			if (Number(radio.value) !== How.Value)
				radio.disabled = !text;
		}
		if (!text)
			this.ways[0].checked = true;
		this.drawWhat();
	}

	/** The field for the way checked: the value's cell By value — one the server chooses with «…» and «×» — else the text. */
	drawWhat() {
		if (this.way() !== How.Value) {
			const input = document.createElement('input');
			input.type = 'text';
			input.value = this.text;
			input.addEventListener('input', () => { this.text = input.value; });
			this.what.replaceChildren(input);
			input.focus();
			input.select();
			return;
		}
		const type = this.field()?.types[0] ?? Type.String;
		const typed = [Type.Boolean, Type.Number, Type.Date, Type.String].includes(type);
		this.what.replaceChildren(this.cells.valueCell(type, this.held, this.heldText, (value, text) => {
			this.held = value;
			this.heldText = text;
			// A cell the server chooses for says what was chosen — drawn again with it; a typed value's shows it as typed.
			if (!typed)
				this.drawWhat();
		}, this.choose.bind(this)));
	}
}
