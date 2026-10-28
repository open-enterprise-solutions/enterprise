import WebAwesomeElement from '../../internal/webawesome-element.js';
/**
 * @summary Formats a number of bytes as a human-readable string with the appropriate unit, such as kB, MB, or GB.
 *  Supports both byte and bit units with configurable locale.
 * @documentation https://webawesome.com/docs/components/format-bytes
 * @status stable
 * @since 2.0
 */
export default class WaFormatBytes extends WebAwesomeElement {
    static get styles(): never[];
    private readonly localize;
    /** The number to format in bytes. */
    value: number;
    /** The type of unit to display. */
    unit: 'byte' | 'bit';
    /** Determines how to display the result, e.g. "100 bytes", "100 b", or "100b". */
    display: 'long' | 'short' | 'narrow';
    render(): string;
}
declare global {
    interface HTMLElementTagNameMap {
        'wa-format-bytes': WaFormatBytes;
    }
}
