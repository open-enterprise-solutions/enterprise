// A REPORT'S SETTINGS — what its reader sets (the server's question ComposerSettings, the thin client's
// ibDialogComposerSettings on the reader's road): the STRUCTURE on the left — the report, its outputs (a table's two
// axes, Rows and Columns), the groupings on them, each inside the one before — and the settings of
// the node taken on the right: the parameters the author left for the reader (the report's own), what the grouping
// groups by (a level's), the fields shown, the filter, the sort, the conditional appearance, the other settings. A
// setting of the report reaches every output; a level's is its own. The setting goes back whole — the structure is
// part of it — with the reader's parameter values as its own lines (Name, Value).
//
// The node format (core/description/compositionDescription.cpp): Settings {SelectedFields, Filter, Order, Group,
// ConditionalAppearance, OutputParameters, Structure {CompositionOutput: Name, OutputKind (0 grouping, 1 table),
// Selected, Filter, Order, ConditionalAppearance, OutputParameters, Rows / Columns {CompositionLevel: LevelKind
// (0 grouping, 1 detail records), Group, Selected, Filter, Order, ConditionalAppearance, OutputParameters, Children
// {…}}}, CompositionParameter children: Name, Expression, Value}.

import { _ } from '../localization.js';
import { ConditionalAppearanceEditor } from '../settings/conditionalAppearanceEditor.js';
import { Type } from '../settings/fieldTree.js';
import { FilterEditor } from '../settings/filterEditor.js';
import { GroupEditor, SelectedFieldsEditor, SortEditor } from '../settings/lineEditor.js';
import { OutputParametersEditor } from '../settings/outputParametersEditor.js';
import { SettingsEditor, lineTools, moveIn, smallButton } from '../settings/settingsEditor.js';
import { SettingsWindow, editWindow, tabGroup, withoutEmpty } from '../settings/settingsWindow.js';

/** @typedef {import('./questions.js').Questions} Questions */
/** @typedef {import('../protocol/patch.js').ProtocolNode} ProtocolNode */
/** @typedef {import('../settings/settingsEditor.js').Choose} Choose */
/** @typedef {import('../settings/fieldTree.js').SettingsFieldTree} SettingsFieldTree */
/**
 * A line of the structure: what it is, the node it edits, the run it stands in (moved and deleted there), the output
 * it belongs to, how deep it stands.
 * @typedef {{kind: 'report' | 'output' | 'axis' | 'level', node: any, lines: any[] | null, output: any, depth: number,
 *   axis?: string}} StructureLine
 */

// What an output is (ibCompositionOutputKind), and what a level is (ibCompositionLevelKind).
const OutputKind = Object.freeze({ Grouping: 0, Table: 1 });
const LevelKind = Object.freeze({ Grouping: 0, Details: 1 });

export class ComposerSettings extends SettingsWindow {
	/**
	 * @param {Questions} questions what it is asked through
	 * @param {ProtocolNode} request
	 */
	constructor(questions, request) {
		super(questions, request);
		const composition = request.Composition ?? {};
		titleFields(this.fields, composition);
		const choose = this.choose.bind(this);
		/** @type {ProtocolNode[]} the parameters the author left for the reader */
		this.declared = (composition.NodeChildren ?? [])
			.filter((/** @type {any} */ one) => one.NodeType === 'CompositionParameter' && Number(one.ForUser) === 1);
		this.parameters = new ParametersEditor(this.fields, choose);
		this.grouping = new GroupEditor(this.fields, () => this.groupingChanged());
		/** a grouping being made, in the window it is composed in before it is added */
		this.groupingForm = new GroupEditor(this.fields);
		this.selected = new SelectedFieldsEditor(this.fields);
		this.filter = new FilterEditor(this.fields, choose);
		this.sort = new SortEditor(this.fields);
		this.appearance = new ConditionalAppearanceEditor(this.fields, choose);
		this.outputParameters = new OutputParametersEditor();
		/** @type {StructureLine} the line taken — its settings on the right */
		this.taken = { kind: 'report', node: this.settings, lines: null, output: null, depth: 0 };
		/** the page on show — kept as another line is taken, where that line has it */
		this.page = 'selected';
		this.list = document.createElement('div');
		this.list.className = 'settings-structure-list';
		this.header = document.createElement('div');
		this.header.className = 'settings-header';
		this.pages = document.createElement('div');
		this.pages.className = 'settings-node';
		this.open(_('Data composer settings'), 'composer-settings');
		this.take(this.taken);
	}

	content() {
		const structure = document.createElement('div');
		structure.className = 'settings-structure';
		const bar = document.createElement('div');
		bar.className = 'settings-tools';
		const [, remove, up, down] = lineTools(() => {}, () => this.removeNode(), (step) => this.moveNode(step));
		bar.append(...[{ caption: _('Add grouping'), help: _('Add grouping'), press: () => this.addGrouping() },
			{ caption: _('Add table'), help: _('Add table (rows and columns)'), press: () => this.addTable() }, remove, up,
			down]
			.map((tool) => smallButton(tool.caption, tool.help, tool.press)));
		structure.append(bar, this.list);
		const settings = document.createElement('div');
		settings.className = 'settings-node-side';
		settings.append(this.header, this.pages);
		const main = document.createElement('div');
		main.className = 'settings-composer';
		main.append(structure, settings);
		return main;
	}

	/** The setting as edited — an empty part of any node taken away: absence reads back as the storey above. */
	written() {
		const settings = structuredClone(this.settings);
		const parts = ['Filter', 'Order', 'ConditionalAppearance', 'OutputParameters'];
		withoutEmpty(settings, ['SelectedFields', 'Group', ...parts]);
		const levels = (/** @type {any} */ axis) => {
			for (const level of axis?.NodeChildren ?? []) {
				withoutEmpty(level, ['Selected', 'Group', ...parts]);
				levels(level.Children);
				withoutEmpty(level, ['Children']);
			}
		};
		for (const output of settings.Structure?.NodeChildren ?? []) {
			withoutEmpty(output, ['Selected', ...parts]);
			levels(output.Rows);
			levels(output.Columns);
			withoutEmpty(output, ['Rows', 'Columns']);
		}
		withoutEmpty(settings, ['Structure']);
		return settings;
	}

	// ---- the structure -------------------------------------------------------------------------------------------

	/** @returns {any[]} the outputs, the node made when there is none */
	outputs() {
		return ((this.settings.Structure ??= {}).NodeChildren ??= []);
	}

	/**
	 * Every line of the structure, in the order shown. An axis is a LADDER — a level contains the next one, so the run
	 * reads on screen as the nesting it is (the thin client's tree).
	 */
	lines() {
		/** @type {StructureLine[]} */
		const lines = [{ kind: 'report', node: this.settings, lines: null, output: null, depth: 0 }];
		const levels = (/** @type {any[]} */ run, /** @type {any} */ output, /** @type {number} */ depth) => {
			run.forEach((level, at) => lines.push({ kind: 'level', node: level, lines: run, output, depth: depth + at }));
		};
		const outputs = this.outputs();
		for (const output of outputs) {
			lines.push({ kind: 'output', node: output, lines: outputs, output, depth: 1 });
			if (Number(output.OutputKind) === OutputKind.Table) {
				for (const axis of ['Rows', 'Columns']) {
					lines.push({ kind: 'axis', node: output, lines: null, output, depth: 2, axis });
					levels((output[axis] ??= {}).NodeChildren ??= [], output, 3);
				}
			}
			else
				levels((output.Rows ??= {}).NodeChildren ??= [], output, 2);
		}
		return lines;
	}

	/** The structure drawn, the line taken in the highlight. */
	drawStructure() {
		this.list.replaceChildren(...this.lines().map((line) => {
			const row = document.createElement('div');
			row.className = 'settings-field';
			row.classList.toggle('taken', isSame(line, this.taken));
			row.style.paddingInlineStart = `${0.375 + line.depth * 1.125}rem`;
			row.textContent = this.caption(line);
			row.addEventListener('click', () => this.take(line));
			return row;
		}));
	}

	/**
	 * How a line of the structure reads (the thin client's): the report; an output by what it is and its name; an axis;
	 * a level by every field it groups by, or as the detail records.
	 * @param {StructureLine} line
	 */
	caption(line) {
		if (line.kind === 'report')
			return _('Report');
		if (line.kind === 'output') {
			const kind = Number(line.node.OutputKind) === OutputKind.Table ? _('Table') : _('Output');
			return line.node.Name ? `${kind} - ${line.node.Name}` : kind;
		}
		if (line.kind === 'axis')
			return line.axis === 'Columns' ? _('Columns') : _('Rows');
		const fields = (line.node.Group?.NodeChildren ?? [])
			.map((/** @type {any} */ one) => this.fields.titleOf(String(one.Field ?? ''))).join(', ');
		return fields === '' ? _('Detail records') : `${_('Grouping')}: ${fields}`;
	}

	/**
	 * A line taken: its settings on the right, under what they reach.
	 * @param {StructureLine} line
	 */
	take(line) {
		this.taken = line;
		this.drawStructure();
		const node = line.node;
		const onLevel = line.kind === 'level';
		this.header.textContent = `${_('Settings of')}: ${this.caption(line)}  `
			+ (onLevel ? _('(this grouping only)') : line.kind === 'report' ? _('(applies to every output)') : '');
		/** @type {[string, string, HTMLElement][]} */
		const pages = [];
		if (line.kind === 'report' && this.declared.length > 0) {
			this.parameters.show(this.declared, this.settings);
			pages.push(['parameters', _('Parameters'), this.parameters.element]);
		}
		if (onLevel) {
			this.grouping.show((node.Group ??= {}).NodeChildren ??= []);
			pages.push(['grouping', _('Grouping'), this.grouping.element]);
		}
		// An axis holds levels and nothing else: it has no settings of its own.
		if (line.kind !== 'axis') {
			const selected = line.kind === 'report' ? 'SelectedFields' : 'Selected';
			this.selected.show((node[selected] ??= {}).NodeChildren ??= []);
			this.filter.show(node.Filter ??= { Kind: 0, NodeChildren: [] });
			this.sort.show((node.Order ??= {}).NodeChildren ??= []);
			this.appearance.show(node.ConditionalAppearance ??= { NodeChildren: [] });
			// The report's own lists its heading too; a storey under it, the theme alone.
			this.outputParameters.show(node.OutputParameters ??= { NodeChildren: [] }, line.kind === 'report');
			pages.push(['selected', _('Selected fields'), this.selected.element],
				['filter', _('Filter'), this.filter.element], ['sort', _('Sort'), this.sort.element],
				['appearance', _('Conditional appearance'), this.appearance.element],
				['output', _('Other settings'), this.outputParameters.element]);
		}
		// The page on show stays where the line has it.
		const group = tabGroup(pages, pages.some(([name]) => name === this.page) ? this.page : undefined);
		group.addEventListener('wa-tab-show', (/** @type {any} */ event) => {
			this.page = event.detail?.name ?? this.page;
		});
		this.pages.replaceChildren(group);
	}

	/** What the grouping groups by changed: the level is the detail records while it groups by nothing. */
	groupingChanged() {
		if (this.taken.kind !== 'level')
			return;
		const level = this.taken.node;
		level.LevelKind = (level.Group?.NodeChildren?.length ?? 0) > 0 ? LevelKind.Grouping : LevelKind.Details;
		this.drawStructure();
		this.header.textContent = `${_('Settings of')}: ${this.caption(this.taken)}  ${_('(this grouping only)')}`;
	}

	/**
	 * A grouping composed in a window of its own first (the thin client's grouping form): the fields welded into its
	 * one heading, each with its unfold — none is an answer too, the detail records, a node all the same. Then added
	 * innermost on the run the line taken is on (the thin client's AxisForCommand): on the report, a new output unless
	 * the last holds nothing yet.
	 */
	async addGrouping() {
		/** @type {any[]} */
		const lines = [];
		this.groupingForm.show(lines);
		if (!await editWindow(_('Grouping'), 'grouping-window', this.groupingForm.element))
			return;
		const level = { NodeType: 'CompositionLevel', LevelKind: lines.length > 0 ? LevelKind.Grouping : LevelKind.Details,
			Group: { NodeChildren: lines } };
		const line = this.taken;
		/** @type {any[]} */
		let run;
		if (line.kind === 'level' && line.lines !== null)
			run = line.lines;
		else if (line.kind === 'axis')
			run = line.node[line.axis ?? 'Rows'].NodeChildren;
		else if (line.kind === 'output')
			run = (line.node.Rows ??= {}).NodeChildren ??= [];
		else {
			const last = this.outputs().at(-1);
			const output = last !== undefined && isUntouched(last) ? last : this.addOutput();
			run = (output.Rows ??= {}).NodeChildren ??= [];
		}
		run.push(level);
		this.take(this.lines().find((one) => one.node === level) ?? this.taken);
	}

	/**
	 * A table — the other shape of output, its Rows and Columns there with it, neither deletable (the thin client's
	 * OnStructureAddTable): always a new output, but for a last grouping nobody has put anything in — a filled one
	 * would take its levels onto the rows of a shape nobody asked for. Its Rows taken: the first heading goes there.
	 */
	addTable() {
		const last = this.outputs().at(-1);
		const output = last !== undefined && Number(last.OutputKind) !== OutputKind.Table && isUntouched(last)
			? last : this.addOutput();
		output.OutputKind = OutputKind.Table;
		this.take(this.lines().find((one) => one.kind === 'axis' && one.node === output && one.axis === 'Rows')
			?? this.taken);
	}

	/** @returns {any} a grouping output added after the last */
	addOutput() {
		const output = { NodeType: 'CompositionOutput', Name: '', OutputKind: OutputKind.Grouping };
		this.outputs().push(output);
		return output;
	}

	/**
	 * The line taken removed (the thin client's OnStructureRemove): a level with every level under it — removing a
	 * grouping breaks the chain, and pulling the deeper ones up would make another report —, taken then what stood
	 * above it; an output whole, never the last — a composition always has one. The report and an axis stay.
	 */
	removeNode() {
		const line = this.taken;
		if (line.lines === null)
			return;
		if (line.kind === 'output') {
			if (line.lines.length < 2)
				return;
			const at = line.lines.indexOf(line.node);
			line.lines.splice(at, 1);
			const next = line.lines[Math.min(at, line.lines.length - 1)];
			this.take(this.lines().find((one) => one.kind === 'output' && one.node === next) ?? this.lines()[0]);
			return;
		}
		// What stood above it is the line before — the level it unfolded from, or the axis or output it hung under.
		const at = this.lines().findIndex((one) => isSame(one, line));
		line.lines.splice(line.lines.indexOf(line.node));
		this.take(this.lines()[at - 1]);
	}

	/** @param {number} step */
	moveNode(step) {
		const line = this.taken;
		if (line.lines !== null && moveIn(line.lines, line.node, step))
			this.take(this.lines().find((one) => isSame(one, line)) ?? line);
	}
}

/**
 * The same line of the structure — its node, and for an axis which of the two.
 * @param {StructureLine} a
 * @param {StructureLine} b
 */
function isSame(a, b) {
	return a.node === b.node && a.kind === b.kind && a.axis === b.axis;
}

/**
 * An output nobody has put anything in — no level on either axis; a command adding one takes it instead of a second.
 * @param {any} output
 */
function isUntouched(output) {
	return (output.Rows?.NodeChildren?.length ?? 0) === 0 && (output.Columns?.NodeChildren?.length ?? 0) === 0;
}

/**
 * The composition's fields titled as its author titled them (CompositionSelect: Path, UseTitle, Title) — a report's
 * fields come from its query, named, not presented.
 * @param {SettingsFieldTree} fields
 * @param {any} composition
 */
function titleFields(fields, composition) {
	const select = (composition.NodeChildren ?? []).find((/** @type {any} */ one) => one.NodeType === 'CompositionSelect');
	for (const info of select?.NodeChildren ?? []) {
		const field = fields.fields.find((one) => one.path === info.Path);
		// A field of the composition's own — the first storey: its path is its name, its title its presentation.
		if (field !== undefined && info.UseTitle === true && info.Title)
			field.title = field.presentation = String(info.Title);
	}
}

/**
 * A name read out loud (core's ibTitleFromName): a capital starts a word, unless it stands inside a run of capitals;
 * one that holds a space is a title already.
 * @param {string} name
 */
function titleFromName(name) {
	if (name === '' || name.includes(' '))
		return name;
	return name.replace(/(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])/g, ' ');
}

/** The parameters the author left for the reader, a value each (the thin client's reader's parameter page). */
class ParametersEditor extends SettingsEditor {
	/**
	 * @param {SettingsFieldTree} fields
	 * @param {Choose} choose
	 */
	constructor(fields, choose) {
		super(fields, [[_('Parameter'), '40%'], [_('Value'), '']]);
		this.choose = choose;
		/** @type {ProtocolNode[]} */
		this.declared = [];
		/** @type {any} the setting the values are written into */
		this.settings = {};
		/** @type {Map<string, string>} how a reference chosen here reads, by the parameter — its value goes bare */
		this.texts = new Map();
		this.build([], false);
	}

	draw() {
		this.drawRows(this.declared.map((declared) => {
			const name = String(declared.Name ?? '');
			const row = this.lineRow(false, () => {});
			const title = document.createElement('td');
			title.textContent = titleFromName(name);
			const value = document.createElement('td');
			const own = this.own(name);
			const type = String(declared.Type?.Types?.[0]?.TypeId ?? own?.Value?.t ?? Type.String);
			value.append(this.valueCell(type, own?.Value ?? declared.Value, this.texts.get(name) ?? '',
				(chosen, text) => this.write(name, chosen, text), this.choose));
			row.append(title, value);
			return row;
		}));
	}

	/**
	 * @param {ProtocolNode[]} declared
	 * @param {any} settings
	 */
	show(declared, settings) {
		this.declared = declared;
		this.settings = settings;
		this.draw();
	}

	/**
	 * The reader's own value of a parameter — a line of the setting — if they set one.
	 * @param {string} name
	 */
	own(name) {
		return (this.settings.NodeChildren ?? [])
			.find((/** @type {any} */ one) => one.NodeType === 'CompositionParameter' && one.Name === name);
	}

	/**
	 * A value the reader set, written as a variant's is: name and value, no declaration.
	 * @param {string} name
	 * @param {object} value
	 * @param {string} text how a reference reads — kept beside it for the window
	 */
	write(name, value, text) {
		this.texts.set(name, text);
		const own = this.own(name);
		if (own !== undefined)
			own.Value = value;
		else {
			(this.settings.NodeChildren ??= [])
				.push({ NodeType: 'CompositionParameter', Name: name, Expression: '', Value: value });
		}
	}
}
