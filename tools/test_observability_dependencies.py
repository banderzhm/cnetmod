"""Regression fixtures for the observability architecture guard."""

from pathlib import Path
import tempfile
import unittest

from check_observability_dependencies import module_graph, parse_unit, violations


class DependencyTests(unittest.TestCase):
    def test_comments_and_literals_are_not_edges(self):
        owner, edges = parse_unit('''export module cnetmod.protocol.example;
            // import cnetmod.observability;
            /* import cnetmod.application; */
            auto a = "import cnetmod.observability;";
            auto b = R"tag(import cnetmod.application;)tag";
            import cnetmod.instrumentation.tracing;
        ''')
        self.assertEqual(owner, 'cnetmod.protocol.example')
        self.assertEqual(edges, {'cnetmod.instrumentation.tracing'})

    def test_partition_and_multiline_export_import(self):
        owner, edges = parse_unit('''module;
            export module cnetmod.protocol.example:client;
            export import
                :common;
        ''')
        self.assertEqual(owner, 'cnetmod.protocol.example:client')
        self.assertEqual(edges, {'cnetmod.protocol.example:common'})

    def test_inactive_branch_is_checked(self):
        owner, edges = parse_unit('''export module cnetmod.protocol.example;
            #if 0
            import cnetmod.observability.otlp;
            #endif
        ''')
        self.assertEqual(violations({owner: edges}),
                         [(owner, 'cnetmod.observability.otlp')])

    def test_transitive_cycle_terminates_and_reports_chain(self):
        self.assertEqual(violations({
            'cnetmod.protocol.x': {'helper'},
            'helper': {'cycle'},
            'cycle': {'helper', 'cnetmod.application'},
        }), [('cnetmod.protocol.x', 'helper', 'cycle', 'cnetmod.application')])

    def test_similar_names_and_forward_adapters_allowed(self):
        self.assertEqual(violations({
            'cnetmod.observability.http': {'cnetmod.protocol.http'},
            'cnetmod.protocol.http': {'cnetmod.observability_free'},
        }), [])

    def test_implementation_edges_are_merged(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'x.cppm').write_text(
                'export module cnetmod.protocol.x; import :private_part;', encoding='utf-8')
            (root / 'x.cpp').write_text(
                'module cnetmod.protocol.x; import cnetmod.helper;', encoding='utf-8')
            (root / 'helper.cppm').write_text(
                'export module cnetmod.helper; import cnetmod.observability;', encoding='utf-8')
            graph = module_graph(root)
            self.assertEqual(graph['cnetmod.protocol.x'],
                             {'cnetmod.protocol.x:private_part', 'cnetmod.helper'})
            self.assertEqual(graph['cnetmod.helper'], {'cnetmod.observability'})
            self.assertEqual(violations(graph), [('cnetmod.protocol.x',
                             'cnetmod.helper', 'cnetmod.observability')])

    def test_empty_tree_fails_closed(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                module_graph(Path(directory))


if __name__ == '__main__':
    unittest.main()
