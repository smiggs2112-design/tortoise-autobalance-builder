"""Offline validation for the deterministic knowledge catalogue."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
CATALOGUE = ROOT / "enhancements" / "humanlike-knowledge.tsv"

def normalise(text):
    return re.sub(r"[^a-z0-9]+", " ", text.lower()).strip()

def has_phrase(line, phrase):
    return " " + normalise(phrase) + " " in " " + normalise(line) + " "

def read_records():
    records = []
    for lineno, raw in enumerate(CATALOGUE.read_text().splitlines(), start=1):
        if not raw or raw.startswith("#"):
            continue
        parts = raw.split("\t")
        assert len(parts) == 4, f"Line {lineno}: expected four TSV columns"
        name, aliases, answer, source = parts
        assert name and answer and source, f"Line {lineno}: blank required field"
        assert len(raw) <= 1200, f"Line {lineno}: exceeds server hard limit"
        entries = [normalise(a) for a in aliases.split("|") if normalise(a)]
        assert entries, f"Line {lineno}: no phrase aliases"
        records.append((name, entries, answer, source))
    return records

class KnowledgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.records = read_records()

    def test_minimum_coverage(self):
        self.assertGreaterEqual(len(self.records), 40)

    def test_ids_unique(self):
        names = [r[0] for r in self.records]
        self.assertEqual(len(names), len(set(names)))

    def test_aliases_not_ambiguous(self):
        alias_to_id = {}
        for name, aliases, _, _ in self.records:
            for alias in aliases:
                self.assertGreaterEqual(len(alias), 6)
                if alias in alias_to_id:
                    self.assertEqual(alias_to_id[alias], name, alias)
                alias_to_id[alias] = name

    def test_word_boundaries(self):
        self.assertTrue(has_phrase("How do I use a hearthstone?", "hearthstone"))
        self.assertFalse(has_phrase("I'm in Goldshire", "gold"))
        self.assertFalse(has_phrase("Goldsmithing", "gold"))

    def test_important_topics_have_aliases(self):
        by_id = {x[0]:x[1] for x in self.records}
        for key in ("combat.threat", "attributes.spirit", "world.hearthstone",
                    "professions.mining", "group.rebuff", "world.scarletmonastery"):
            self.assertIn(key, by_id)
        self.assertTrue(any(has_phrase("How does threat work?", alias)
                            for alias in by_id["combat.threat"]))

    def test_answers_are_bounded(self):
        for name, _, answer, source in self.records:
            self.assertLessEqual(len(answer), 230, name)
            self.assertTrue(source, name)

if __name__ == "__main__":
    unittest.main()
