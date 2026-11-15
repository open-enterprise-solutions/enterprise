// LIST SETTINGS — what a person sets of a list: its filter, its sort, how it groups, its conditional appearance (the
// server's question ListSettings, the thin client's ibDialogListSettings over a reader's setting, 660×450): a page
// each, the available fields beside the lines (src/settings — the editors and the window's road, the report's window
// shares them). A line the author made inaccessible is applied and never shown.

import { _ } from '../localization.js';
import { ConditionalAppearanceEditor } from '../settings/conditionalAppearanceEditor.js';
import { FilterEditor } from '../settings/filterEditor.js';
import { GroupEditor, SortEditor } from '../settings/lineEditor.js';
import { SettingsWindow, tabGroup, withoutEmpty } from '../settings/settingsWindow.js';

/** @typedef {import('./questions.js').Questions} Questions */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */

export class ListSettings extends SettingsWindow {
	/**
	 * @param {Questions} questions what it is asked through
	 * @param {ProtocolNode} request
	 */
	constructor(questions, request) {
		super(questions, request);
		const choose = this.choose.bind(this);
		this.filter = new FilterEditor(this.fields, choose);
		this.sort = new SortEditor(this.fields);
		this.group = new GroupEditor(this.fields);
		this.appearance = new ConditionalAppearanceEditor(this.fields, choose);
		this.open(_('List settings'), 'list-settings');
		this.filter.show(this.settings.Filter ??= { Kind: 0, NodeChildren: [] });
		this.sort.show((this.settings.Order ??= {}).NodeChildren ??= []);
		this.group.show((this.settings.Group ??= {}).NodeChildren ??= []);
		this.appearance.show(this.settings.ConditionalAppearance ??= { NodeChildren: [] });
	}

	content() {
		return tabGroup([
			['filter', _('Filter'), this.filter.element],
			['sort', _('Sort'), this.sort.element],
			['group', _('Group'), this.group.element],
			['appearance', _('Conditional appearance'), this.appearance.element],
		]);
	}

	/** The setting as edited — what the person left empty taken away. */
	written() {
		const settings = structuredClone(this.settings);
		withoutEmpty(settings, ['Filter', 'Order', 'Group', 'ConditionalAppearance']);
		return settings;
	}
}
