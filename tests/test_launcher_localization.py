from pathlib import Path
import json
import re
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import generate_catalogue


class LauncherLocalizationTests(unittest.TestCase):
    def setUp(self):
        self.root = Path(__file__).resolve().parents[1]

    def test_every_used_translation_has_three_nonempty_texts_and_unique_key(self):
        literal = r'"(?:\\.|[^"\\])*"'
        header = (self.root / 'launcher/strings.h').read_text(encoding='utf8')
        entries = re.findall(r'Translation\{L(' + literal + r'),L(' + literal + r'),L(' + literal + r')\}', header)
        self.assertGreater(len(entries), 50)
        keys = set()
        for entry in entries:
            chinese, japanese, english = [json.loads(text) for text in entry]
            self.assertTrue(chinese and japanese and english)
            self.assertNotIn(chinese, keys)
            keys.add(chinese)
        main = (self.root / 'launcher/main.cpp').read_text(encoding='utf8')
        for used in re.findall(r'T\(L(' + literal + r')\)', main):
            self.assertIn(json.loads(used), keys)

    def test_bilingual_titles_are_independent_of_game_identity_and_language(self):
        games = generate_catalogue.load(self.root / 'games/catalogue.json')
        self.assertTrue(all(game['nameJapanese'] and game['name'] != game['nameJapanese'] for game in games))
        generated = generate_catalogue.generate(games)
        self.assertIn('怒首領蜂大往生 臨廻転生', generated)
        self.assertIn('DoDonPachi DaiOuJou', generated)
        self.assertEqual(games[4]['id'], 'daioujou')
        self.assertTrue(games[4]['integrated'])

    def test_japanese_name_validation(self):
        import copy
        from test_catalogue import CatalogueTests
        helper = CatalogueTests()
        helper.setUp()
        raw = generate_catalogue.read_json(self.root / 'games/catalogue.json')
        for name in ('', '\ninvalid', 42, 'あ' * 129):
            with self.subTest(name=name):
                value = copy.deepcopy(raw)
                value['games'][0]['nameJapanese'] = name
                helper.reject(value)


if __name__ == '__main__':
    unittest.main()
