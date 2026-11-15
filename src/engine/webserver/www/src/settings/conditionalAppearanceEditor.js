// THE CONDITIONAL APPEARANCE PAGE — a list of rules, each read as a sentence: WHEN its condition holds, the FIELDS it
// names wear its APPEARANCE (the thin client's ibConditionalAppearanceEditor) — one page for a list and for a report.
// Only the tick is set in the line; each other part is read out and its «…» opens what edits it, and nothing here is a
// second editor of anything: the condition is the filter editor in a window of its own, the fields a list of fields,
// the appearance the appearance window. A later rule paints over an earlier one — the lines are moved. The rules are
// the setting's ConditionalAppearance: {NodeChildren — CompositionAppearanceRule: Use, Filter {Kind, NodeChildren},
// Fields {NodeChildren — CompositionField: Path}, Appearance (appearanceEditor.js)}; a new rule holds for every row,
// dresses the whole row and wears nothing — a part left empty is not written.

import { _ } from '../localization.js';
import { appearanceText, editAppearance } from './appearanceEditor.js';
import { Type, emptyDate } from './fieldTree.js';
import { FilterEditor, NodeKind, comparisons, takesValue } from './filterEditor.js';
import { FieldListEditor } from './lineEditor.js';
import { SettingsEditor, checkbox, lineTools, moveIn, smallButton } from './settingsEditor.js';
import { editWindow } from './settingsWindow.js';

/** @typedef {import('./fieldTree.js').SettingsFieldTree} SettingsFieldTree */
/** @typedef {import('./settingsEditor.js').Choose} Choose */

const ruleNode = 'CompositionAppearanceRule';
// How a group joins (ibFilterGroupKind) — the thin client reads a condition out in these words, untranslated.
const GroupKind = Object.freeze({ And: 0, Or: 1, Not: 2 });

export class ConditionalAppearanceEditor extends SettingsEditor {
	/**
	 * @param {SettingsFieldTree} fields
	 * @param {Choose} choose
	 */
	constructor(fields, choose) {
		super(fields, [['', '2.25rem'], [_('Condition'), '36%'], [_('Fields'), '28%'], [_('Appearance'), '']]);
		/** @type {any} the rules edited — the setting's own node */
		this.rules = { NodeChildren: [] };
		/** @type {any} the rule taken */
		this.taken = null;
		// The windows a condition and the fields are edited in — made once, shown for each rule: a field tree keeps
		// every pane it draws.
		this.condition = new FilterEditor(fields, choose);
		this.fieldList = new FieldListEditor(fields);
		this.build(lineTools(() => this.addRule(), () => this.removeRule(), (step) => this.moveRule(step)), false);
	}

	/**
	 * The rules to edit — a storey's own, edited in place.
	 * @param {any} rules {NodeChildren}
	 */
	show(rules) {
		this.rules = rules;
		this.rules.NodeChildren ??= [];
		this.taken = null;
		this.draw();
	}

	draw() {
		this.drawRows(this.rules.NodeChildren.map((/** @type {any} */ rule) => this.row(rule)));
	}

	/**
	 * A rule's line: its tick, and its three parts read out.
	 * @param {any} rule
	 */
	row(rule) {
		const row = this.lineRow(rule === this.taken, () => { this.taken = rule; });
		const use = document.createElement('td');
		use.className = 'use';
		use.append(checkbox(rule.Use !== false, (checked) => { rule.Use = checked; }));
		row.append(use,
			this.partCell(conditionText(rule.Filter, this.fields), _('Condition'), () => this.editCondition(rule)),
			this.partCell(fieldsText(rule.Fields, this.fields), _('Fields'), () => this.editFields(rule)),
			this.partCell(appearanceText(rule.Appearance), _('Appearance'), () => this.editAppearance(rule)));
		return row;
	}

	/**
	 * A part of a rule as it reads, and the «…» to the window it is edited in.
	 * @param {string} text
	 * @param {string} caption
	 * @param {() => void} edit
	 */
	partCell(text, caption, edit) {
		const cell = document.createElement('td');
		const part = document.createElement('div');
		part.className = 'settings-part';
		const said = document.createElement('span');
		said.textContent = text;
		said.title = text;
		part.append(said, smallButton('…', caption, edit));
		cell.append(part);
		return cell;
	}

	/** A rule added at the end — switched on, holding for every row, dressing the whole row — and taken. */
	addRule() {
		const rule = { NodeType: ruleNode, Use: true };
		this.rules.NodeChildren.push(rule);
		this.taken = rule;
		this.draw();
	}

	/** The rule taken removed, the next one taken. */
	removeRule() {
		const lines = this.rules.NodeChildren;
		const at = lines.indexOf(this.taken);
		if (at < 0)
			return;
		lines.splice(at, 1);
		this.taken = lines[Math.min(at, lines.length - 1)] ?? null;
		this.draw();
	}

	/** @param {number} step */
	moveRule(step) {
		if (moveIn(this.rules.NodeChildren, this.taken, step))
			this.draw();
	}

	/**
	 * The condition in the filter editor, a window of its own, over a copy that lands on OK.
	 * @param {any} rule
	 */
	async editCondition(rule) {
		const edited = structuredClone(rule.Filter ?? { Kind: GroupKind.And, NodeChildren: [] });
		this.condition.show(edited);
		if (!await editWindow(_('Condition'), 'condition-window', this.condition.element))
			return;
		if (edited.NodeChildren.length > 0)
			rule.Filter = edited;
		else
			delete rule.Filter;
		this.draw();
	}

	/**
	 * The fields the rule dresses, a list of them in a window of its own; none — the whole row.
	 * @param {any} rule
	 */
	async editFields(rule) {
		const edited = structuredClone(rule.Fields?.NodeChildren ?? []);
		this.fieldList.show(edited);
		if (!await editWindow(_('Fields'), 'fields-window', this.fieldList.element))
			return;
		if (edited.length > 0)
			rule.Fields = { NodeChildren: edited };
		else
			delete rule.Fields;
		this.draw();
	}

	/**
	 * What the rule wears, in the appearance window.
	 * @param {any} rule
	 */
	async editAppearance(rule) {
		const edited = structuredClone(rule.Appearance ?? { NodeChildren: [] });
		if (!await editAppearance(edited))
			return;
		if (edited.NodeChildren.length > 0)
			rule.Appearance = edited;
		else
			delete rule.Appearance;
		this.draw();
	}
}

/**
 * A condition read out — what its cell shows (the thin client's ConditionText): the lines switched on, each side as the
 * filter's own cell shows it, the comparison in its word; a group bracketed. None — a rule that holds for every row.
 * @param {any} filter {Kind, NodeChildren} — none at all, no condition
 * @param {SettingsFieldTree | null} fields
 */
function conditionText(filter, fields) {
	/**
	 * @param {number} kind
	 * @param {any[]} nodes
	 * @returns {string}
	 */
	const joined = (kind, nodes) => nodes
		.filter((node) => node.Use !== 0)
		.map((node) => {
			if (node.Node === NodeKind.Group) {
				const inner = joined(Number(node.Kind ?? 0), node.NodeChildren ?? []);
				return inner === '' ? '' : `${Number(node.Kind) === GroupKind.Not ? 'NOT (' : '('}${inner})`;
			}
			const comparison = Number(node.Kind ?? 0);
			const left = fields?.titleOf(String(node.LPath ?? ''), String(node.LText ?? '')) ?? String(node.LText ?? '');
			const said = `${left} ${comparisons()[comparison] ?? ''}`;
			return takesValue(comparison) ? `${said} ${rightText(node, fields)}` : said;
		})
		.filter((part) => part !== '')
		.join(kind === GroupKind.Or ? ' OR ' : ' AND ');
	return filter !== undefined ? joined(Number(filter.Kind ?? 0), filter.NodeChildren ?? []) : '';
}

/**
 * A condition's right side as it reads: a field by its whole path, a value as it is written — a reference by how it reads.
 * @param {any} node
 * @param {SettingsFieldTree | null} fields
 */
function rightText(node, fields) {
	if (node.RPath)
		return fields?.titleOf(String(node.RPath), String(node.RText ?? '')) ?? String(node.RText ?? '');
	if (node.RText)
		return String(node.RText);
	const value = node.RValue?.v;
	if (node.RValue?.t === Type.Boolean)
		return value === true ? _('True') : _('False');
	if (value === undefined || value === emptyDate)
		return '';
	return String(value);
}

/**
 * The fields a rule dresses, as they read, joined by commas; none — the whole row, said by nothing.
 * @param {any} list {NodeChildren} — none at all, the whole row
 * @param {SettingsFieldTree | null} fields
 */
function fieldsText(list, fields) {
	return (list?.NodeChildren ?? [])
		.map((/** @type {any} */ one) => fields?.titleOf(String(one.Path ?? '')) ?? String(one.Path ?? ''))
		.join(', ');
}
