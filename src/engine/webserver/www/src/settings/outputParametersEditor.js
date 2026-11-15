// THE OTHER SETTINGS PAGE — how a report behaves as a whole (the thin client's ibOutputParametersEditor; the line, the
// tick, the rule — parameterValuesEditor.js): its theme, its heading — the title, and whether the title, the filter
// and the parameters are printed under it. The page after Conditional appearance, on the report and on each storey
// under it; a storey lists the theme alone (ibOutputParameterScope::Node — it has no heading to title). A theme is one
// of the platform's palettes, a mode one of three words — «Auto», what the report does when nobody said, is not ticked.
// The node edited: a storey's OutputParameters {NodeChildren — CompositionOutputParameter: Parameter, Use, Value — a
// theme's id, a title's text, a mode's number}.

import { _, language } from '../localization.js';
import { readingOf } from '../properties/translate.js';
import { Type } from './fieldTree.js';
import { ParameterValuesEditor } from './parameterValuesEditor.js';

/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('./parameterValuesEditor.js').Packed} Packed */

// The platform's parameters (ibOutputParameter), in the order its page lists them (ibOutputParameters), each with what
// it is called there — said when drawn.
const Parameter = Object.freeze({ Theme: 0, Title: 1, ShowTitle: 2, ShowFilter: 3, ShowParameters: 4 });
const parameters = () => /** @type {[number, string][]} */ ([
	[Parameter.Theme, _('Theme')], [Parameter.Title, _('Title')], [Parameter.ShowTitle, _('Show title')],
	[Parameter.ShowFilter, _('Show filter')], [Parameter.ShowParameters, _('Show parameters')],
]);
// The palettes (ibCompositionThemes), by their ids, and the three words for whether a part is printed (ibShowMode).
const themes = () => /** @type {[string, string][]} */ ([['Meadow', _('Meadow')], ['Neutral', _('Neutral')],
	['Sea', _('Sea')], ['Sand', _('Sand')], ['BlackAndWhite', _('Black and white')]]);
const modes = () => /** @type {[number, string][]} */ ([[0, _('Auto')], [1, _('Show')], [2, _('Do not show')]]);
const auto = 0;

export class OutputParametersEditor extends ParameterValuesEditor {
	constructor() {
		super('CompositionOutputParameter');
		/** the report's own — the whole list; a storey under it lists the theme alone */
		this.wholeReport = true;
	}

	/**
	 * @param {any} values {NodeChildren}
	 * @param {boolean} [wholeReport]
	 */
	show(values, wholeReport = true) {
		this.wholeReport = wholeReport;
		super.show(values);
	}

	parameters() {
		return this.wholeReport ? parameters() : parameters().filter(([parameter]) => parameter === Parameter.Theme);
	}

	/**
	 * A theme by its name — none kept, nothing (what stands is the storey above's); a mode by its word — none kept,
	 * «Auto»; a title in the language in force.
	 * @param {number} parameter
	 * @param {any} value
	 * @returns {ProtocolNode}
	 */
	cellRow(parameter, value) {
		const kept = value?.v;
		if (parameter === Parameter.Theme) {
			const at = themes().findIndex(([id]) => id === kept);
			return { Class: 'ibPropertyEnum', Value: at, NodeChildren: [
				...(at < 0 ? [{ Id: -1, Title: '' }] : []),
				...themes().map(([, title], id) => ({ Id: id, Title: title }))] };
		}
		if (parameter === Parameter.Title) {
			const raw = String(kept ?? '');
			return { Class: 'ibPropertyTString', Value: raw, Presentation: readingOf(raw, language()) };
		}
		return { Class: 'ibPropertyEnum', Value: kept ?? auto,
			NodeChildren: modes().map(([id, title]) => ({ Id: id, Title: title })) };
	}

	/**
	 * @param {number} parameter
	 * @param {any} changed
	 * @returns {Packed | null}
	 */
	packed(parameter, changed) {
		if (parameter === Parameter.Theme)
			return changed === -1 ? null : { value: { t: Type.String, v: themes()[changed][0] }, use: true };
		if (parameter === Parameter.Title)
			return changed === '' ? null : { value: { t: Type.String, v: changed }, use: true };
		return { value: { t: Type.Number, v: changed }, use: changed !== auto };
	}
}
