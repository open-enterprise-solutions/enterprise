// A FILTER, edited as the tree it is (the thin client's ibFilterEditor): the root and its groups join what is under
// them (And, Or, Not), a condition compares a field with a value — Use, Left value, Comparison, Right value; the root
// has no Use. A value is typed in its cell by the field's type, a reference chosen by the server. A new condition goes
// after the line taken (inside a group taken), its right side the TYPE'S OWN EMPTY — the line is whole once its field
// is picked, and a flag has no third state to show; a field of another type resets it, the same type keeps it. A line is
// copied, wrapped in a group of its own in its place, and a group lifted out into its lines (Copy, Group conditions,
// Ungroup — the thin client's tools). A line the author made inaccessible is applied and never shown to the reader.
// The node edited is the setting's Filter: {Kind, NodeChildren — CompositionFilterNode: Node, Use, Display, Text, Kind,
// LPath / LText / LLeaf, RPath / RText / RLeaf, RValue (the value packed)}.

import { _ } from '../localization.js';
import { Type } from './fieldTree.js';
import { SettingsEditor, checkbox, lineTools, moveIn, select } from './settingsEditor.js';

/** @typedef {import('./fieldTree.js').SettingsFieldTree} SettingsFieldTree */
/** @typedef {import('./fieldTree.js').Field} Field */
/** @typedef {import('./settingsEditor.js').Choose} Choose */

// What a filter line is, how a group joins, how a person meets it, what a condition compares (compositionDescription.h).
export const NodeKind = Object.freeze({ Condition: 0, Group: 1 });
// The words said when drawn, in the person's language.
const groupKinds = () => [_('And'), _('Or'), _('Not')];
const inaccessible = 2;
export const comparisons = () => [_('Equal'), _('Not equal'), _('Greater'), _('Less'), _('Greater or equal'),
	_('Less or equal'), _('Contains'), _('In'), _('In hierarchy'), _('Filled'), _('Not filled')];
const filled = 9;
const notFilled = 10;

/**
 * Whether a comparison has a right side — «filled» asks the field alone (ibComparisonTakesValue).
 * @param {number} kind
 */
export function takesValue(kind) {
	return kind !== filled && kind !== notFilled;
}
// The node a line is written as — named, for the server to look up.
const filterNode = 'CompositionFilterNode';

export class FilterEditor extends SettingsEditor {
	/**
	 * @param {SettingsFieldTree} fields
	 * @param {Choose} choose
	 */
	constructor(fields, choose) {
		super(fields, [[_('Use'), '2.25rem'], [_('Left value'), '34%'], [_('Comparison'), '24%'], [_('Right value'), '']]);
		this.choose = choose;
		/** @type {any} the filter edited — its root */
		this.root = { Kind: 0, NodeChildren: [] };
		/** @type {any} the line taken — the root itself when none */
		this.taken = null;
		const tools = lineTools(() => this.addCondition(this.fields?.taken ?? null), () => this.removeLine(),
			(step) => this.moveLine(step));
		// The thin client's order: New item, New group, Copy, Delete | ↑ ↓ | Group conditions, Ungroup.
		tools.splice(1, 0,
			{ caption: _('New group'), help: _('New group (Ctrl+F9)'), key: 'Ctrl+F9', press: () => this.addGroup() },
			{ caption: _('Copy'), help: _('Copy (F9)'), key: 'F9', press: () => this.copyLine() });
		tools.push(
			{ caption: _('Group conditions'), help: _('Group conditions'), press: () => this.groupLine() },
			{ caption: _('Ungroup'), help: _('Ungroup'), press: () => this.ungroupLine() });
		this.build(tools);
	}

	/** The filter drawn: the root, its groups and conditions, the line taken kept. */
	draw() {
		const rows = [this.row(this.root, null, 0)];
		const walk = (/** @type {any} */ group, /** @type {number} */ depth) => {
			for (const node of group.NodeChildren ?? []) {
				// Applied, never shown: the author's.
				if (node.Display === inaccessible)
					continue;
				rows.push(this.row(node, group, depth));
				if (node.Node === NodeKind.Group)
					walk(node, depth + 1);
			}
		};
		walk(this.root, 1);
		this.drawRows(rows);
	}

	/** @param {Field} field */
	pick(field) {
		this.addCondition(field);
	}

	/**
	 * The filter to edit — a node's or the setting's own.
	 * @param {any} root {Kind, NodeChildren}
	 */
	show(root) {
		this.root = root;
		this.root.NodeChildren ??= [];
		this.taken = null;
		this.draw();
	}

	/**
	 * A filter line: the root, a group, or a condition.
	 * @param {any} node
	 * @param {any} parent the group it stands in — none for the root
	 * @param {number} depth
	 */
	row(node, parent, depth) {
		const row = this.lineRow(node === (this.taken ?? this.root), () => { this.taken = node; });
		const use = document.createElement('td');
		use.className = 'use';
		// The root has no Use: switched off, every line under it would read as set while nothing filters.
		if (parent !== null)
			use.append(checkbox(node.Use !== 0, (checked) => { node.Use = checked ? 1 : 0; }));
		const left = document.createElement('td');
		left.style.paddingInlineStart = `${0.25 + depth * 1.125}rem`;
		const comparison = document.createElement('td');
		const right = document.createElement('td');
		if (parent === null || node.Node === NodeKind.Group) {
			const join = document.createElement('div');
			join.className = 'settings-join';
			if (parent === null)
				join.append(_('Filter'));
			join.append(select(groupKinds().map((caption, kind) => [kind, caption]), Number(node.Kind ?? 0),
				(kind) => { node.Kind = kind; }));
			left.append(join);
		}
		else {
			left.append(this.fieldSelect(String(node.LPath ?? ''), String(node.LText ?? ''), (field) => {
				const before = this.fields?.fieldAt(String(node.LPath ?? ''))?.types[0];
				node.LPath = field.path;
				node.LText = field.title;
				node.LLeaf = field.id;
				// Another type — the value it was compared with means nothing: its type's own empty instead. The same
				// type keeps what was typed.
				if (field.types[0] !== before)
					this.takeEmpty(node, field);
				this.draw();
			}));
			comparison.append(select(comparisons().map((caption, kind) => [kind, caption]), Number(node.Kind ?? 0),
				(kind) => {
					node.Kind = kind;
					this.draw();
				}));
			if (takesValue(Number(node.Kind ?? 0))) {
				// A field not offered now (one under a reference not opened) — the value's own type tells. A field of several
				// types — its type whole: the cell stands on none until a value is chosen (the thin client's AdjustValue).
				const field = this.fields?.fieldAt(String(node.LPath ?? ''));
				const type = field !== undefined && field.types.length > 1 && field.type !== undefined ? field.type
					: field?.types[0] ?? node.RValue?.t ?? Type.String;
				right.append(this.valueCell(type, node.RValue, String(node.RText ?? ''), (value, text) => {
					node.RValue = value;
					node.RText = text;
				}, this.choose));
			}
		}
		row.append(use, left, comparison, right);
		return row;
	}

	/**
	 * The group a new line goes into — the one taken, or the one the line taken stands in — and where in it.
	 * @returns {{group: any, at: number}}
	 */
	placeForNew() {
		const taken = this.taken ?? this.root;
		if (taken === this.root || taken.Node === NodeKind.Group) {
			taken.NodeChildren ??= [];
			return { group: taken, at: taken.NodeChildren.length };
		}
		const parent = this.parentOf(taken) ?? this.root;
		return { group: parent, at: parent.NodeChildren.indexOf(taken) + 1 };
	}

	/**
	 * @param {any} node
	 * @returns {any} the group the line stands in
	 */
	parentOf(node) {
		/** @type {(group: any) => any} */
		const find = (group) => {
			for (const child of group.NodeChildren ?? []) {
				if (child === node)
					return group;
				const found = child.Node === NodeKind.Group ? find(child) : null;
				if (found !== null)
					return found;
			}
			return null;
		};
		return find(this.root);
	}

	/**
	 * A condition on the field — the one taken, or the first — added where the line taken is.
	 * @param {Field | null} field
	 */
	addCondition(field) {
		const chosen = field ?? this.fields?.allFields()[0];
		if (chosen === undefined)
			return;
		const { group, at } = this.placeForNew();
		const node = { NodeType: filterNode, Node: NodeKind.Condition, Use: 1, Display: 0, Text: '', Kind: 0,
			LPath: chosen.path, LText: chosen.title, LLeaf: chosen.id, RPath: '', RText: '', RLeaf: -1 };
		this.takeEmpty(node, chosen);
		group.NodeChildren.splice(at, 0, node);
		this.taken = node;
		this.draw();
	}

	/**
	 * A condition's right-hand side set to its field's type's own empty (the thin client's AdjustValue) — none for a field
	 * of several types, which has no one type to be the empty of.
	 * @param {any} node
	 * @param {Field} field
	 */
	takeEmpty(node, field) {
		const empty = field.types.length > 1 ? undefined : this.fields?.emptyOf(field.types[0] ?? Type.String);
		node.RText = '';
		if (empty !== undefined)
			node.RValue = empty;
		else
			delete node.RValue;
	}

	/** A group (And), added where the line taken is, taken itself. */
	addGroup() {
		const { group, at } = this.placeForNew();
		const node = { NodeType: filterNode, Node: NodeKind.Group, Use: 1, Display: 0, Text: '', Kind: 0,
			NodeChildren: [] };
		group.NodeChildren.splice(at, 0, node);
		this.taken = node;
		this.draw();
	}

	/** A copy of the line taken — a group with what is under it — added where a new line goes, taken. */
	copyLine() {
		const taken = this.taken;
		if (taken === null || taken === this.root)
			return;
		// Placed before it is made: the place is read from the line taken, which the copy is not.
		const { group, at } = this.placeForNew();
		const copy = structuredClone(taken);
		group.NodeChildren.splice(at, 0, copy);
		this.taken = copy;
		this.draw();
	}

	/** The line taken wrapped in a group (And) in its own place — the line stays taken (ibFilterEditor's Group conditions). */
	groupLine() {
		const taken = this.taken;
		const parent = taken !== null ? this.parentOf(taken) : null;
		if (parent === null)
			return;
		const group = { NodeType: filterNode, Node: NodeKind.Group, Use: 1, Display: 0, Text: '', Kind: 0,
			NodeChildren: [taken] };
		parent.NodeChildren.splice(parent.NodeChildren.indexOf(taken), 1, group);
		this.draw();
	}

	/** The group taken lifted out: its lines take its place, in their order — Group conditions undone; the root stays. */
	ungroupLine() {
		const taken = this.taken;
		const parent = taken !== null && taken.Node === NodeKind.Group ? this.parentOf(taken) : null;
		if (parent === null)
			return;
		const children = taken.NodeChildren ?? [];
		parent.NodeChildren.splice(parent.NodeChildren.indexOf(taken), 1, ...children);
		this.taken = children[0] ?? parent;
		this.draw();
	}

	/** The line taken removed — a group with what is under it. */
	removeLine() {
		const taken = this.taken;
		const parent = taken !== null ? this.parentOf(taken) : null;
		if (parent === null)
			return;
		const at = parent.NodeChildren.indexOf(taken);
		parent.NodeChildren.splice(at, 1);
		this.taken = parent.NodeChildren[Math.min(at, parent.NodeChildren.length - 1)] ?? parent;
		this.draw();
	}

	/**
	 * The line taken moved among the lines of its group.
	 * @param {number} step
	 */
	moveLine(step) {
		const taken = this.taken;
		const parent = taken !== null ? this.parentOf(taken) : null;
		if (parent !== null && moveIn(parent.NodeChildren, taken, step))
			this.draw();
	}
}
