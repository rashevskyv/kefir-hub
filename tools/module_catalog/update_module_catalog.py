import os
import sys
import json
import argparse
import urllib.error
import urllib.request
from datetime import datetime, timezone
from typing import Dict, Any, Optional

# Adjust paths to import from same directory
sys.path.append(os.path.dirname(os.path.abspath(__file__)))
from models import ModuleInfo
from merge import merge_catalog, normalize_tid

FORBIDDEN_HOMEBREW_TIDS = {
    "0100000000000035",  # Nintendo grc system module, not homebrew sys-ftpd.
}

def get_current_utc_time() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")

def save_json_file(path: str, data: Any):
    # Ensure directory exists
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2, sort_keys=True)

def load_json_file(path: str) -> Optional[Any]:
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return None
    return None

def validate_catalog(modules: Dict[str, ModuleInfo]):
    for tid, mod in modules.items():
        if normalize_tid(tid) != tid:
            raise ValueError(f"Invalid normalized Title ID: {tid}")
        if tid in FORBIDDEN_HOMEBREW_TIDS:
            raise ValueError(f"Nintendo system Title ID leaked into homebrew catalog: {tid}")
        if mod.confidence == "verified":
            if not mod.repository:
                raise ValueError(f"Verified module has no repository: {tid}")
            if not mod.tid_evidence:
                raise ValueError(f"Verified module has no Title ID evidence: {tid}")
            if not mod.description_en:
                raise ValueError(f"Verified module has no English description candidate: {tid}")

def validate_evidence_urls(modules: Dict[str, ModuleInfo]):
    for tid, mod in sorted(modules.items()):
        if mod.confidence != "verified":
            continue
        for url in mod.tid_evidence:
            request = urllib.request.Request(
                url,
                headers={"User-Agent": "sphaira-module-catalog-validator"},
            )
            try:
                with urllib.request.urlopen(request, timeout=30) as response:
                    content = response.read().decode("utf-8", errors="ignore").upper()
                    if not 200 <= response.status < 300:
                        raise ValueError(f"Evidence returned HTTP {response.status}: {url}")
                    if tid not in content:
                        raise ValueError(f"Evidence does not contain {tid}: {url}")
            except urllib.error.URLError as error:
                raise ValueError(f"Evidence is unavailable for {tid}: {url}: {error}") from error

def main():
    parser = argparse.ArgumentParser(description="Nintendo Switch Homebrew Sysmodule Catalog Generator")
    parser.add_argument("--refresh", action="store_true", help="Ignore HTTP cache and fetch fresh data")
    parser.add_argument("--offline", action="store_true", help="Run in offline mode using only HTTP cache")
    parser.add_argument("--tid", type=str, help="Process only a single Title ID for debugging")
    parser.add_argument(
        "--validate-evidence",
        action="store_true",
        help="Fetch every verified evidence URL and require its exact Title ID",
    )
    
    args = parser.parse_args()
    
    base_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.abspath(os.path.join(base_dir, "..", ".."))
    
    research_path = os.path.join(base_dir, "modules_research.json")
    i18n_candidates_path = os.path.join(base_dir, "i18n_en_candidates.json")
    runtime_catalog_path = os.path.join(project_root, "assets", "romfs", "modules", "homebrew_sysmodules.json")
    unresolved_path = os.path.join(base_dir, "unresolved.json")
    

    
    # Load previous research data to maintain generated_at if nothing changed
    prev_research = load_json_file(research_path)
    prev_generated_at = None
    prev_modules_data = {}
    if prev_research and isinstance(prev_research, dict):
        prev_generated_at = prev_research.get("generated_at")
        prev_modules_data = prev_research.get("modules", {})
        
    start_time = get_current_utc_time()
    
    def on_progress_callback(current_modules: Dict[str, ModuleInfo]):

        # 1. Save modules_research.json
        research_modules = {}
        for tid, mod in sorted(current_modules.items()):
            research_modules[tid] = mod.to_dict()
        research_output = {
            "schema_version": 1,
            "generated_at": start_time,
            "sources": {
                "title_ids": "https://gist.githubusercontent.com/ndeadly/a4b8c01bb453028cd0008f282098f696/raw/homebrew_sysmodules.txt",
                "appstore": "https://switch.cdn.fortheusers.org/repo.json"
            },
            "modules": research_modules
        }
        save_json_file(research_path, research_output)
        
        # 2. Save i18n_en_candidates.json
        i18n_candidates = {}
        for tid, mod in sorted(current_modules.items()):
            if mod.confidence == "verified" and mod.repository and mod.description_en:
                i18n_candidates[f"module.{tid}.description"] = mod.description_en
        save_json_file(i18n_candidates_path, i18n_candidates)
        
        # 3. Save assets/romfs/modules/homebrew_sysmodules.json (runtime catalog)
        runtime_modules = {}
        for tid, mod in sorted(current_modules.items()):
            if mod.confidence == "verified":
                runtime_modules[tid] = {
                    "name": mod.name,
                    "repository": mod.repository
                }
        runtime_output = {
            "schema_version": 1,
            "generated_at": start_time,
            "modules": runtime_modules
        }
        save_json_file(runtime_catalog_path, runtime_output)
        
        # 4. Save unresolved.json
        unresolved_modules = {}
        for tid, mod in sorted(current_modules.items()):
            reasons = []
            if mod.confidence == "unresolved":
                reasons.append("Repository not found")
            elif mod.confidence == "conflict":
                reasons.append("Conflict between sources")
            elif mod.confidence == "probable":
                reasons.append("TID not verified in repository primary sources")
                
            if not mod.description_en:
                reasons.append("Description is empty")
                
            if mod.confidence != "verified":
                unresolved_modules[tid] = {
                    "name": mod.name,
                    "confidence": mod.confidence,
                    "repository": mod.repository,
                    "reasons": reasons
                }
        unresolved_output = {
            "schema_version": 1,
            "generated_at": start_time,
            "modules": unresolved_modules
        }
        save_json_file(unresolved_path, unresolved_output)

    # Perform catalog generation
    try:
        modules = merge_catalog(
            offline=args.offline, 
            refresh=args.refresh, 
            specific_tid=args.tid,
            on_progress_callback=on_progress_callback
        )
    except Exception as e:
        print(f"Error generating catalog: {e}", file=sys.stderr)
        sys.exit(1)

    validate_catalog(modules)
    if args.validate_evidence:
        validate_evidence_urls(modules)
        
    # Build final research modules dictionary for comparison
    final_research_modules = {}
    for tid, mod in sorted(modules.items()):
        final_research_modules[tid] = mod.to_dict()
        
    # Check if modules data changed from previous run
    data_changed = True
    if prev_modules_data:
        if args.tid:
            norm_tid = normalize_tid(args.tid)
            if norm_tid:
                data_changed = (final_research_modules.get(norm_tid) != prev_modules_data.get(norm_tid))
            else:
                data_changed = True
        else:
            data_changed = (final_research_modules != prev_modules_data)
    
    # If no data changed, restore the previous timestamp to keep files completely unchanged
    if not data_changed and prev_generated_at:
        start_time = prev_generated_at
        on_progress_callback(modules)
        print("No changes detected. Restored previous generated_at timestamp for idempotency.")
    else:
        on_progress_callback(modules)
        print("Catalog generation complete. Saved updated database.")

if __name__ == "__main__":
    main()
