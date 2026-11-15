// A LIST OF THE PLATFORM'S PARAMETERS — a line each, ticked or not, what it is called, its value (the thin client's
// ibParameterValuesDescription, which the appearance window and the other settings page both edit): none added, none
// left out — which ones is the kind's. A value is set in the property cell of its kind, the form editor's (D1); chosen,
// it is ticked (the kind may say otherwise of a value that means «as nobody said»); emptied, the parameter is
// forgotten; the tick keeps the value as it is, and an unticked one reads grey — kept, not applied. The node edited:
// {NodeChildren — <the kind's node>: Parameter, Use, Value (the value packed)}.

import { _ } from '../localization.js';
import { cellOf } from '../properties/index.js';
import { SettingsEditor, checkbox } from './settingsEditor.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** A value as it is packed, and whether it is in force. @typedef {{value: object, use: boolean}} Packed */

export class ParameterValuesEditor extends SettingsEditor {
	/** @param {string} nodeType the node a parameter's line is written as */
	constructor(nodeType) {
		super(null, [['', '2.25rem'], [_('Parameter'), '42%'], [_('Value'), '']]);
		this.nodeType = nodeType;
		/** @type {any} the values edited — a storey's own node, or a copy */
		this.values = { NodeChildren: [] };
		this.build([], false);
	}

	/**
	 * The values to edit, in place.
	 * @param {any} values {NodeChildren}
	 */
	show(values) {
		this.values = values;
		this.values.NodeChildren ??= [];
		this.draw();
	}

	draw() {
		this.drawRows(this.parameters().map(([parameter, caption]) => this.row(parameter, caption)));
	}

	/**
	 * The parameters listed, each with what it is called — said when drawn.
	 * @returns {[number, string][]}
	 */
	parameters() {
		return [];
	}

	/**
	 * The row a property cell reads a parameter's value from — its class, the value as that cell holds it.
	 * @param {number} parameter
	 * @param {any} value the value packed — none when nothing is kept
	 * @returns {ProtocolNode}
	 */
	cellRow(parameter, value) {
		return { Presentation: String(value?.v ?? '') };
	}

	/**
	 * A value set in its cell, packed; null — the parameter emptied.
	 * @param {number} parameter
	 * @param {any} changed as the cell holds it
	 * @returns {Packed | null}
	 */
	packed(parameter, changed) {
		return null;
	}

	/**
	 * A parameter's line: its tick, what it is called, its value in the cell of its kind.
	 * @param {number} parameter
	 * @param {string} caption
	 */
	row(parameter, caption) {
		const said = this.find(parameter);
		const ticked = said?.Use === true;
		const row = document.createElement('tr');
		row.classList.toggle('unticked', !ticked);
		const use = document.createElement('td');
		use.className = 'use';
		// The tick keeps the value as it is — a colour stays a colour; only the word «in force» changes.
		use.append(checkbox(ticked, (checked) => this.say(parameter, checked, said?.Value ?? {})));
		const name = document.createElement('td');
		name.textContent = caption;
		const value = document.createElement('td');
		// Titled as the line is — what a window the cell opens is called.
		const cell = cellOf({ Title: caption, ...this.cellRow(parameter, said?.Value) }, (changed) => {
			const packed = this.packed(parameter, changed);
			this.say(parameter, packed?.use ?? false, packed?.value ?? {});
		});
		cell.classList.add('property-cell');
		value.append(cell);
		row.append(use, name, value);
		return row;
	}

	/**
	 * A person's word on a parameter (ibParameterValuesDescription::Say): unticked with nothing kept forgets it.
	 * @param {number} parameter
	 * @param {boolean} use
	 * @param {any} value the value packed — {} for none
	 */
	say(parameter, use, value) {
		const lines = this.values.NodeChildren;
		const at = lines.findIndex((/** @type {any} */ one) => Number(one.Parameter) === parameter);
		if (!use && Object.keys(value).length === 0) {
			if (at >= 0)
				lines.splice(at, 1);
		}
		else if (at >= 0)
			Object.assign(lines[at], { Use: use, Value: value });
		else
			lines.push({ NodeType: this.nodeType, Parameter: parameter, Use: use, Value: value });
		this.draw();
	}

	/** @param {number} parameter */
	find(parameter) {
		return this.values.NodeChildren.find((/** @type {any} */ one) => Number(one.Parameter) === parameter);
	}
}
