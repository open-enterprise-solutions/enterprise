// A SPREADSHEET (Gridbox) — a form's sheet: a report's result, a document's printed form. Its State says which sheet
// is on show by its Version; a new one comes in the same State (Sheet, ReadOnly) or is fetched whole. A report's
// gridbox also says what its sheet is to the report (ComposeState): composing — the old sheet stays on show under
// the word until the new one is delivered; outdated — the settings changed, and it waits for the person to compose.
// A double click on a cell is the report's to answer (Cell {Row, Col}, from 1) — a drill-down, a document opened.
// Cells are taken, copied and summed as in a spreadsheet program (selection.js).
//
// Read-only for now: a sheet edited here would go back whole (Change {Sheet}), as the thin client's does.

import { Control, EventKind, defineControl } from '../controls/control.js';
import { CommandBar } from '../controls/commandBar.js';
import { drawSheet } from './sheet.js';
import { CellSelection } from './selection.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

// What a sheet is to its report (ibSpreadsheet::ibComposeState).
const ComposeState = Object.freeze({ Composed: 0, Composing: 1, Outdated: 2 });

class Gridbox extends Control {
	/** @param {ProtocolNode} node */
	build(node) {
		this.bar = new CommandBar(this);
		this.host = document.createElement('div');
		this.host.className = 'sheet-host';
		this.composing = document.createElement('div');
		this.composing.className = 'compose-state';
		this.composing.hidden = true;
		const summary = document.createElement('div');
		summary.className = 'selection-summary';
		summary.hidden = true;
		this.selection = new CellSelection(this.host, summary);
		this.append(this.bar.element, this.host, this.composing, summary);
		/** @type {number | undefined} the Version of the sheet on show */
		this.version = undefined;
		this.composeState = ComposeState.Composed;
		this.update(node);
	}

	/** @param {ProtocolNode} node */
	update(node) {
		this.bar.update(this.state(node).CommandBar);
		return true;
	}

	/**
	 * No children: the sheet follows the State's Version, the word over it its ComposeState.
	 * @param {ProtocolNode} node
	 */
	drawChildren(node) {
		const state = this.state(node);
		this.showComposeState(state.ComposeState ?? ComposeState.Composed);
		if (state.Version === this.version)
			return;
		this.version = state.Version;
		if (state.Sheet !== undefined)
			this.draw(state.Sheet, state.GridLines === true);
		else
			this.load(state.Version, state.GridLines === true);
	}

	/**
	 * The sheet read whole, unless another came while it was on its way.
	 * @param {number | undefined} version
	 * @param {boolean} gridLines
	 */
	async load(version, gridLines) {
		try {
			const answer = await this.context.fetch(this.node.ControlId, {});
			if (version === this.version && answer?.Sheet !== undefined)
				this.draw(answer.Sheet, gridLines);
		}
		catch {
			// The form went, or the server refused: the sheet on show stays.
		}
	}

	/**
	 * @param {any} sheet
	 * @param {boolean} gridLines
	 */
	draw(sheet, gridLines) {
		const table = drawSheet(sheet, {
			gridLines,
			onCell: (row, col) => this.send(EventKind.Cell, { Row: row, Col: col }),
		});
		this.host.replaceChildren(table);
		this.selection.attach(table);
	}

	/** @param {number} state */
	showComposeState(state) {
		if (state === this.composeState)
			return;
		this.composeState = state;
		this.composing.hidden = state === ComposeState.Composed;
		this.composing.replaceChildren();
		if (state === ComposeState.Composing)
			this.composing.append(document.createElement('wa-spinner'));
		this.composing.append(state === ComposeState.Composing ? 'Composing...'
			: 'The settings have changed. Press Compose to build the report.');
	}
}

defineControl('Gridbox', Gridbox);
