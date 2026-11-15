// THE APPEARANCE WINDOW — how a value is shown, as the list of the PLATFORM's parameters (the thin client's
// ibEditAppearance; parameterValuesEditor.js — the line, the tick, the rule): a colour `r,g,b`, a font, one of the sheet's
// three alignments, a text in the language in force, each set in its property cell; chosen, a value is ticked. A
// report's field and a rule of the conditional appearance both carry one. The node edited is the one the server wrote:
// {NodeChildren — CompositionAppearance: Parameter, Use, Value (the value packed)}.

import { _, language } from '../localization.js';
import { fontText } from '../properties/runtime.js';
import { readingOf } from '../properties/translate.js';
import { Type } from './fieldTree.js';
import { ParameterValuesEditor } from './parameterValuesEditor.js';
import { editWindow } from './settingsWindow.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('./parameterValuesEditor.js').Packed} Packed */

// The platform's parameters (ibAppearanceParameter), in the order its window lists them (ibAppearanceParameters), each
// with what it is called there — said when drawn.
const Parameter = Object.freeze({ Format: 0, BackgroundColour: 1, TextColour: 2, Font: 3, HorizontalAlignment: 4, Text: 5 });
const parameters = () => /** @type {[number, string][]} */ ([
	[Parameter.BackgroundColour, _('Background colour')], [Parameter.TextColour, _('Text colour')],
	[Parameter.Font, _('Font')], [Parameter.HorizontalAlignment, _('Horizontal alignment')],
	[Parameter.Format, _('Format')], [Parameter.Text, _('Text')],
]);
// The types a value is packed as, by their ids as Type has them: the engine's Colour (VL_COLOR) and Font (VL_FONT), the
// sheet's SpreadsheetHorizontalAlignment (EN_SHOAL) — its members the sheet's own numbers.
const ValueType = Object.freeze({ Colour: '215820108143635031', Font: '155522846696186765',
	Alignment: '373501727169334788' });
const alignments = () => /** @type {[number, string][]} */ ([[0, _('Left')], [0x0900, _('Center')], [0x0200, _('Right')]]);

/**
 * The appearance edited in a window of its own, changed in place on OK; false — cancelled, or nothing changed.
 * @param {any} appearance {NodeChildren}
 * @returns {Promise<boolean>}
 */
export async function editAppearance(appearance) {
	const editor = new AppearanceEditor();
	const before = JSON.stringify(appearance.NodeChildren ?? []);
	editor.show(structuredClone(appearance));
	if (!await editWindow(_('Appearance'), 'appearance-window', editor.element))
		return false;
	const edited = editor.values.NodeChildren;
	if (JSON.stringify(edited) === before)
		return false;
	appearance.NodeChildren = edited;
	return true;
}

/**
 * What a cell shows for an appearance (ibAppearanceSummary): its ticked parameters, `Font: Arial, 9pt`, joined by
 * commas — not `;`, a format is written with those. Empty when nothing is set.
 * @param {any} appearance {NodeChildren} — none at all, nothing set
 */
export function appearanceText(appearance) {
	const said = appearance?.NodeChildren ?? [];
	return parameters()
		.map(([parameter, caption]) => [said.find((/** @type {any} */ one) => Number(one.Parameter) === parameter), caption])
		.filter(([one]) => one?.Use === true)
		.map(([one, caption]) => `${caption}: ${valueText(Number(one.Parameter), one.Value)}`)
		.join(', ');
}

class AppearanceEditor extends ParameterValuesEditor {
	constructor() {
		super('CompositionAppearance');
	}

	parameters() {
		return parameters();
	}

	/**
	 * @param {number} parameter
	 * @param {any} value
	 * @returns {ProtocolNode}
	 */
	cellRow(parameter, value) {
		const kept = value?.v;
		switch (parameter) {
			case Parameter.BackgroundColour:
			case Parameter.TextColour:
				return { Class: 'ibPropertyColour', Value: kept ?? '' };
			case Parameter.Font:
				return { Class: 'ibPropertyFont', Value: kept ?? '' };
			case Parameter.HorizontalAlignment:
				// None kept — a line of its own, as the thin client's cell is empty until one is chosen.
				return { Class: 'ibPropertyEnum', Value: kept ?? -1, NodeChildren: [
					...(kept === undefined ? [{ Id: -1, Title: '' }] : []),
					...alignments().map(([id, title]) => ({ Id: id, Title: title }))] };
			default: {
				const raw = String(kept ?? '');
				return { Class: 'ibPropertyTString', Value: raw, Presentation: readingOf(raw, language()) };
			}
		}
	}

	/**
	 * @param {number} parameter
	 * @param {any} changed
	 * @returns {Packed | null}
	 */
	packed(parameter, changed) {
		if (changed === '' || changed === -1)
			return null;
		const type = parameter === Parameter.BackgroundColour || parameter === Parameter.TextColour ? ValueType.Colour
			: parameter === Parameter.Font ? ValueType.Font
			: parameter === Parameter.HorizontalAlignment ? ValueType.Alignment : Type.String;
		return { value: { t: type, v: changed }, use: true };
	}
}

/**
 * A parameter's value as it reads — a colour's numbers, a font as the form editor says it, an alignment's word, a text
 * in the language in force.
 * @param {number} parameter
 * @param {any} value the value packed
 */
function valueText(parameter, value) {
	const kept = value?.v;
	if (kept === undefined)
		return '';
	if (parameter === Parameter.Font)
		return fontText(String(kept));
	if (parameter === Parameter.HorizontalAlignment)
		return alignments().find(([id]) => id === Number(kept))?.[1] ?? '';
	if (parameter === Parameter.Format || parameter === Parameter.Text)
		return readingOf(String(kept), language());
	return String(kept);
}
