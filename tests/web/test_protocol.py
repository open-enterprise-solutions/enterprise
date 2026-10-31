"""The page's reading of the protocol, by itself: packets, patches, a table's rows."""
import unittest

from harness import PageTest


class Protocol(PageTest):
    def run_in_page(self, script, arg=None):
        page = self.start({}, wait=False)
        return page.evaluate(f'async (arg) => {{ {script} }}', arg)

    def test_a_packet_reads_back_what_was_written(self):
        written = {'id': 7, 'result': {'Text': 'Привет', 'Whole': 42, 'Part': 1.5, 'Yes': True, 'No': False, 'None': None,
                                       'List': [1, 'two'], 'NodeId': 3,
                                       'Child': {'NodeType': 'Tablebox', 'NodeChildren': [{'NodeId': 1, 'Text': 'row'}]}}}
        read = self.run_in_page("""
            const { decode, encode, isPacket } = await import('/pub/src/protocol/packet.js');
            const bytes = encode(arg);
            return { packet: isPacket(bytes), read: decode(bytes) };""", written)
        self.assertTrue(read['packet'])
        self.assertEqual(read['read'], written)

    def test_bytes_that_are_no_packet_are_refused(self):
        said = self.run_in_page("""
            const { decode } = await import('/pub/src/protocol/packet.js');
            try { decode(new Uint8Array(64)); return null; } catch (error) { return error.message; }""")
        self.assertIn('no packet', said)

    def test_a_row_reads_as_its_cells_say(self):
        record = self.run_in_page("""
            const { recordOf } = await import('/pub/src/tablebox/rows.js');
            return recordOf({ NodeId: 5, Container: true, Picture: '9', ReadOnly: true, NodeChildren: [
                { NodeId: 2, Text: 'Bolt', Bold: true }, { NodeId: 3, Text: '10', Number: true, Value: { t: 'n', v: 10 } },
                { NodeId: 4, Text: '', Checked: true }] });""")
        self.assertEqual(record['handle'], 5)
        self.assertTrue(record['container'])
        self.assertEqual(record['c2'], 'Bolt')
        self.assertEqual(record['s2'], {'fontWeight': 'bold'})
        self.assertTrue(record['n3'])
        self.assertTrue(record['e3'])
        self.assertTrue(record['k4'])
        self.assertNotIn('e2', record)

    def test_a_page_says_where_it_stands(self):
        read = self.run_in_page("""
            const { readPage } = await import('/pub/src/tablebox/rows.js');
            return readPage({ Begin: true, CurrentRow: 2, NodeChildren: [{ NodeId: 2, NodeChildren: [{ NodeId: 1, Text: 'a' }] }] });""")
        self.assertTrue(read['begin'])
        self.assertFalse(read['end'])
        self.assertEqual(read['current'], 2)
        self.assertEqual(read['records'][0]['level'], 0)


if __name__ == '__main__':
    unittest.main()
