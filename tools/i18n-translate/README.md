# i18n-translate

Fills `assets/romfs/i18n/<code>.json` from `en.json` with an LLM behind a local OpenAI-compatible proxy
(default `http://127.0.0.1:8081/v1/chat/completions`, Gemini Web2API). Keys are the English UI strings.

## Files

| File | Purpose |
|---|---|
| `languages.json` | Target languages: `code -> English name`. The app offers every bundled JSON that has `__language_name`. |
| `translate.py` | One string per request, all missing languages at once. Resumes: only keys missing from a target file are sent. |
| `batch_translate.py` | Same, but many strings per request (`--batch-size`); for filling a new language quickly. |
| `run.bat` / `run_tests.bat` | Windows wrappers: create `.venv` (needs `requests`), check the proxy, run `translate.py` / the tests. |
| `test_translate.py`, `test_language_logic.py` | Offline tests of the tool (`--live` also hits the proxy). |
| `context.json` | Translator notes for ambiguous keys (`"Install"` is a verb, `"Target"` is a destination). `translate.py` sends the note with the string. `batch_translate.py` does not. |

## Translate new strings

1. Write the English text in code as `"..."_i18n` (or `i18n::get("...")`).
2. Run `run.bat` (or `python translate.py`). By default it first syncs `en.json`: every `"..."_i18n` literal in
   `sphaira/` missing from `en.json` is added (`--no-sync` skips this). Then it translates the missing keys.
3. Useful flags: `--langs uk,de` (subset), `--limit 20` (smoke test), `--force` (re-translate existing keys),
   `--threads N`, `--model`, `--url`. Failures go to `failures.log` (git-ignored); re-run to retry them.

## Ambiguous strings
When a short key can be read two ways, add a note to `context.json` with the key and where it appears. Then re-translate
just those keys in every language: `python translate.py --only-context --force` (add `--langs uk` for one language).

## Add a language

1. Add `"<code>": "<English name>"` to `languages.json`. The code is the file name used in `assets/romfs/i18n/`.
2. Create `assets/romfs/i18n/<code>.json` with `{"__language_name": "<native name>"}`.
3. Run `python batch_translate.py --langs <code>` (or `translate.py --langs <code>`) until no keys are missing.
4. Run the parity check below. No code change is needed: the language picker lists bundled JSON files.

## Parity check

```sh
python3 tests/test_i18n_deployment_contract.py
```

From the repo root. It requires, for every language in `languages.json`: the file exists, all `en.json` keys are
present and non-empty, printf specifiers and newline counts match English, `__language_name` is set, and long
English sentences are not left untranslated. `ru.json` must not exist. It also runs as part of `tests/run.sh`.
