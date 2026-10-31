import { type PropertyValues } from 'lit';
import { WebAwesomeFormAssociatedElement } from '../../internal/webawesome-form-associated-element.js';
import { LocalizeController } from '../../utilities/localize.js';
import '../icon/icon.js';
import '../tag/tag.js';
/**
 * @summary Tag inputs collect a list of short values, such as keywords, email addresses, or labels, as removable tags.
 *  Users add a tag by typing and pressing Enter or a delimiter, and each tag is submitted as its own form value.
 * @documentation https://webawesome.com/docs/components/tag-input
 * @status experimental
 * @since 3.13
 *
 * @dependency wa-icon
 * @dependency wa-tag
 *
 * @slot label - The tag input's label. Alternatively, you can use the `label` attribute.
 * @slot start - An element, such as `<wa-icon>`, placed at the start of the control.
 * @slot end - An element, such as `<wa-icon>`, placed at the end of the control.
 * @slot clear-icon - An icon to use in lieu of the default clear icon.
 * @slot hint - Text that describes how to use the tag input. Alternatively, you can use the `hint` attribute.
 *
 * @event blur - Emitted when the control loses focus.
 * @event change - Emitted when a tag is added, removed, or all tags are cleared by the user.
 * @event focus - Emitted when the control gains focus.
 * @event input - Emitted when the user types in the text box or when a tag is added or removed.
 * @event wa-create - Emitted before typed text becomes a tag. Call `event.preventDefault()` to reject it. The event
 *  `detail` contains `{ inputValue: string }`, the text that would become the tag.
 * @event wa-clear - Emitted when the clear button is activated.
 * @event wa-invalid - Emitted when the form control has been checked for validity and its constraints aren't satisfied.
 *
 * @csspart form-control-label - The label.
 * @csspart tag-input - The component's outer wrapper, the bordered box that holds the tags and text box.
 * @csspart start - The container that wraps the `start` slot.
 * @csspart tags - The list that holds the tags.
 * @csspart tag - Each tag, a `<wa-tag>`.
 * @csspart tag__content - The tag's content part.
 * @csspart tag__remove-button - The tag's remove button.
 * @csspart tag__remove-button__base - The tag's remove button base part.
 * @csspart input - The internal text box, an `<input>` element.
 * @csspart clear-button - The clear button.
 * @csspart end - The container that wraps the `end` slot.
 * @csspart hint - The hint's wrapper.
 *
 * @cssstate blank - The tag input has no tags.
 * @cssstate readonly - The tag input is readonly.
 */
export default class WaTagInput extends WebAwesomeFormAssociatedElement {
    static css: import("lit").CSSResult[];
    static get validators(): import("../../internal/webawesome-form-associated-element.js").Validator<WebAwesomeFormAssociatedElement>[];
    assumeInteractionOn: string[];
    private readonly hasSlotController;
    /** @internal Used by the validator to localize validation messages. */
    readonly localize: LocalizeController;
    input: HTMLInputElement;
    /** The index of the tag that has roving focus, or -1 when the text box or nothing has focus. */
    private focusedTagIndex;
    private _value;
    /**
     * The tags as an array of strings, submitted as one entry per tag under `name`. Set the `value` attribute to a
     * delimiter-separated string for an initial value.
     */
    get value(): string[];
    set value(val: string[] | string | FormData | null);
    /**
     * The default value of the form control as a delimiter-separated string. Primarily used for resetting the form
     * control.
     */
    defaultValue: string | null;
    /** The text currently typed in the text box that hasn't become a tag yet. */
    inputValue: string;
    /**
     * The characters that turn typed text into a tag. Each character is a separate delimiter, so `",;"` accepts both
     * commas and semicolons. Pasted text is split on the same characters. Set to an empty string so only Enter adds a
     * tag. Also used to parse the `value` attribute, which falls back to a comma when the delimiter is empty.
     */
    delimiter: string;
    /** The maximum number of tags that can be added. Once reached, no more tags can be added until one is removed. */
    maxTags: number;
    /** The minimum number of tags required for the control to be valid. Has no effect when there are no tags. */
    minTags: number;
    /** Allows the same tag to be added more than once. By default, duplicates are ignored. */
    allowDuplicates: boolean;
    /** Adds a clear button that removes all tags. */
    withClear: boolean;
    /** Placeholder text to show in the text box. Hidden once the maximum number of tags is reached. */
    placeholder: string;
    /** The tag input's label. If you need to display HTML, use the `label` slot instead. */
    label: string;
    /** The tag input's hint. If you need to display HTML, use the `hint` slot instead. */
    hint: string;
    /**
     * Only required for SSR. Set to `true` if you're slotting in a `label` element so the server-rendered markup
     * includes the label before the component hydrates on the client.
     */
    withLabel: boolean;
    /**
     * Only required for SSR. Set to `true` if you're slotting in a `hint` element so the server-rendered markup
     * includes the hint before the component hydrates on the client.
     */
    withHint: boolean;
    /** The tag input's size. Also applied to each tag. */
    size: 'xs' | 's' | 'm' | 'l' | 'xl' | 'small' | 'medium' | 'large';
    handleSizeChange(): void;
    /** The tag input's visual appearance. */
    appearance: 'filled' | 'outlined' | 'filled-outlined';
    /** Draws a pill-style tag input, and pill-style tags, with rounded edges. */
    pill: boolean;
    /** Makes the tag input readonly. Tags stay visible and are still submitted, but can't be added or removed. */
    readonly: boolean;
    /** Makes the tag input a required field, so at least one tag must be added. */
    required: boolean;
    /** Controls whether and how text input is automatically capitalized as it is entered by the user. */
    autocapitalize: 'off' | 'none' | 'on' | 'sentences' | 'words' | 'characters';
    /**
     * Indicates whether the browser's autocorrect feature is on or off. When set as an attribute, use `"off"` or `"on"`.
     * When set as a property, use `true` or `false`.
     */
    autocorrect: boolean;
    /**
     * Specifies what permission the browser has to provide assistance in filling out form field values. Refer to
     * [this page on MDN](https://developer.mozilla.org/en-US/docs/Web/HTML/Attributes/autocomplete) for available values.
     * Defaults to `off`.
     */
    autocomplete: string;
    /** Used to customize the label or icon of the Enter key on virtual keyboards. */
    enterkeyhint: 'enter' | 'done' | 'go' | 'next' | 'previous' | 'search' | 'send';
    /** Enables spell checking on the text box. */
    spellcheck: boolean;
    /**
     * Tells the browser what type of data will be entered by the user, allowing it to display the appropriate virtual
     * keyboard on supportive devices.
     */
    inputmode: 'none' | 'text' | 'decimal' | 'numeric' | 'tel' | 'search' | 'email' | 'url';
    /** A regex that matches any delimiter character, or null when delimiters are turned off. */
    private get delimiterRegex();
    private get isAtMax();
    /** Splits a delimiter-separated string into trimmed, non-empty tags. */
    private parseDelimited;
    private get tagElements();
    /** Finds the index of the tag that contains the event target, or -1 if the target isn't inside a tag. */
    private getTagIndex;
    /** Updates the text box and the inputValue property together so they never drift apart. */
    private setInputText;
    private emitInputAndChange;
    /**
     * Adds a single tag from user input, subject to trimming, the tag limit, duplicate rules, and the cancelable
     * wa-create event. Returns true when the tag was added. Doesn't emit input or change.
     */
    private tryAddTag;
    /**
     * Adds every string as a tag that passes the rules. Emits input and change once if any were added. Returns the
     * texts that were refused, so the caller can put them back in the text box instead of discarding them.
     */
    private addTags;
    /** Removes the tag at the given index in response to user input. Returns true when a tag was removed. */
    private removeTagAt;
    /** Briefly shakes an existing tag to show why a duplicate was rejected. */
    private shakeTag;
    /** Moves roving focus to the tag at the given index after the next render. */
    private focusTag;
    private handleInput;
    private handlePaste;
    private handleKeyDown;
    private handleInputFocus;
    private handleInputBlur;
    private handleTagKeyDown;
    private handleTagClick;
    private handleTagFocusIn;
    private handleTagFocusOut;
    private handleTagRemove;
    private handleWrapperMouseDown;
    private handleWrapperClick;
    private handleClearMouseDown;
    private handleClearClick;
    updated(changedProperties: PropertyValues<this>): void;
    formResetCallback(): void;
    /** Sets focus on the text box. */
    focus(options?: FocusOptions): void;
    /** Removes focus from the text box. */
    blur(): void;
    render(): import("lit-html").TemplateResult<1>;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-tag-input': WaTagInput;
    }
}
