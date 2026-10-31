// THE LABEL COLUMNS — the thin client's two passes (frmclient/visualView/visualHostClient.cpp, CalculateLabels and
// ApplyLabels), over the controls drawn. The widest label of a vertical run is found, then given to each label of the
// run, so the fields after them start at one x. A horizontal box is a row, not a run: it stops the column, and after
// a label in a row the next run starts from nothing. A notebook's page lays out what is in it on its own, and its
// labels are a column of their own.
//
// A control says whether it takes part and how wide its label is (Control.calculateLabelSize), and takes the column's
// width (applyLabelSize). The width is the font's, not the layout's — the desktop measures a label's text the same way
// (GetTextExtent) — so a page behind a tab not on show is measured all the same, and the pass can run again on every
// frame and come to the same widths.
//
// Not by CSS grid: it was tried in the old web client and given up — a grid loses what the sizers' items say (their
// proportion, stretch, border), all of which are flex (docs/private/web/label-alignment.md).

import { Control } from './control.js';

/**
 * The columns of a form, from its own box.
 * @param {Control} form
 */
export function alignLabels(form) {
	alignPage(form);
}

/**
 * A page — the form, or a notebook's page: a column of its own, measured in its box from nothing.
 * @param {Control} page
 */
function alignPage(page) {
	const width = { value: 0 };
	const vertical = !isRow(page);
	if (vertical) {
		for (const child of controlsIn(page))
			calculateLabels(child, false, width);
	}
	for (const child of controlsIn(page))
		applyLabels(child, !vertical, width);
}

/**
 * The widest label of the run.
 * @param {Control} control
 * @param {boolean} inRow whether the box it stands in is a row
 * @param {{value: number}} width
 */
function calculateLabels(control, inRow, width) {
	const own = control.calculateLabelSize();
	if (own !== null && own > width.value)
		width.value = own;
	if (inRow || isPage(control))
		return;
	const row = isBox(control) ? isRow(control) : inRow;
	for (const child of controlsIn(control))
		calculateLabels(child, row, width);
}

/**
 * The run's width given to each label of it.
 * @param {Control} control
 * @param {boolean} inRow
 * @param {{value: number}} width
 */
function applyLabels(control, inRow, width) {
	if (isPage(control)) {
		alignPage(control);
		return;
	}
	// What is in it sees a copy of the width: what it changes stays with it.
	const subtree = { value: width.value };
	const row = isBox(control) ? isRow(control) : inRow;
	for (const child of controlsIn(control))
		applyLabels(child, row, subtree);

	if (control.calculateLabelSize() !== null) {
		control.applyLabelSize(width.value);
		// After a label in a row the column is broken: the next run starts from nothing.
		if (inRow)
			width.value = 0;
	}
}

/**
 * The controls directly under it — the nearest ones down its tree, whatever holds them (a notebook's tab panel).
 * @param {Element} element
 * @returns {Control[]}
 */
function controlsIn(element) {
	const found = [];
	for (const child of element.children) {
		if (child instanceof Control)
			found.push(child);
		else
			found.push(...controlsIn(child));
	}
	return found;
}

/** A box lays out what is in it as a row or a column: the form, a sizer, a page. @param {Element} element */
function isBox(element) {
	return element.hasAttribute('orient');
}

/** @param {Element} element */
function isRow(element) {
	return element.getAttribute('orient') === 'horizontal';
}

/** A notebook's page: a window with a box of its own. @param {Element} element */
function isPage(element) {
	return element.localName === 'oes-notebookpage';
}
