# Nintendo Switch Sysmodule Catalog Generator

This tool collects, verifies, and generates a database of homebrew sysmodules for the Nintendo Switch. It is a developer tool used to generate the offline sysmodules database and candidate files for English localization.

## Directory Structure

```text
tools/module_catalog/
├── cache/                     # HTTP cache directory (git-ignored)
├── tests/                     # Automated test suite
│   └── test_catalog.py
├── manual_overrides.json      # Curated manual adjustments (highest priority)
├── merge.py                   # Data merge, cleanup, and validation logic
├── models.py                  # Module data structures
├── sources.py                 # Network, cache, and Gemini/GitHub API layers
├── update_module_catalog.py   # Main CLI interface entry point
├── modules_research.json      # Full generated research database (git-committed)
├── i18n_en_candidates.json    # Candidate file for English translations
└── unresolved.json            # Registry of unverified or missing modules
```

## Data Sources

1. **Base Title ID List**:
   `https://gist.githubusercontent.com/ndeadly/a4b8c01bb453028cd0008f282098f696/raw/homebrew_sysmodules.txt`
   Used as the primary list of Title IDs and names.

2. **Homebrew App Store Repo**:
   `https://switch.cdn.fortheusers.org/repo.json`
   Used to resolve repository URLs, additional metadata, and application descriptions.

3. **Official GitHub Repositories**:
   Used to search and verify Title IDs inside raw source code (e.g., `toolbox.json`, `Makefile`, `CMakeLists.txt`, `config.json`, `npdm.json`, `*.ini` etc.).

4. **Gemini Web2API**:
   Used as an AI assistant to fetch descriptions and find missing repository URLs when direct APIs fail.

## Command Line Interface (CLI)

```powershell
# Standard run (uses cache, falls back to network)
python tools/module_catalog/update_module_catalog.py

# Force refreshing all network data, bypassing the cache
python tools/module_catalog/update_module_catalog.py --refresh

# Run completely offline using only cached data
python tools/module_catalog/update_module_catalog.py --offline

# Process only a single Title ID for debugging/testing
python tools/module_catalog/update_module_catalog.py --tid 00FF0000636C6BFF
```

## Confidence Levels

- `verified`: Title ID found directly in the repository configuration files or source code.
- `probable`: Matches App Store naming and repository metadata, but TID verification in source code files failed.
- `unresolved`: Insufficient data to determine the repository or verify the module.
- `conflict`: Contradicting repository or naming mappings between sources.

## Rate Limits & GITHUB_TOKEN

GitHub API has strict rate limits:
- **Anonymous**: 60 requests per hour.
- **Authenticated**: 5,000 requests per hour.

To run full updates, configure the `GITHUB_TOKEN` environment variable:
```powershell
$env:GITHUB_TOKEN="your_personal_access_token_here"
python tools/module_catalog/update_module_catalog.py --refresh
```

## Manual Verification Process

1. Look through `tools/module_catalog/unresolved.json` to identify modules that need manually verified details.
2. Search and find the correct GitHub repository or verify the description manually.
3. Open `tools/module_catalog/manual_overrides.json` and add the override details for the Title ID:
   ```json
   "010000000000BD00": {
     "repository": "https://github.com/ndeadly/MissionControl",
     "description_en": "Use controllers from other consoles natively over Bluetooth.",
     "confidence": "verified"
   }
   ```
4. Run `python tools/module_catalog/update_module_catalog.py` to regenerate the catalogs.
5. Review candidates in `tools/module_catalog/i18n_en_candidates.json` and manually transfer verified strings into `assets/romfs/i18n/en.json`.
