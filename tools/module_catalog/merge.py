import re
import json
import os
from typing import Dict, Any, List, Tuple, Optional
try:
    from .models import ModuleInfo
    from .sources import (
        get_github_repo_info,
        get_github_raw_file,
        search_github_repositories,
        fetch_appstore_repo,
        fetch_ndeadly_list,
        query_gemini,
        github_api_get
    )
except ImportError:
    from models import ModuleInfo
    from sources import (
        get_github_repo_info,
        get_github_raw_file,
        search_github_repositories,
        fetch_appstore_repo,
        fetch_ndeadly_list,
        query_gemini,
        github_api_get
    )


# Invalid mappings that we must explicitly block
BLOCKED_MAPPINGS = {
    "0100000000000BD5": "MissionControl",
    "4200000000000010": "sys-clk",
    "420000000000000B": "ldn_mitm"
}

# Explicit valid mappings that we must enforce (or fallback to manual overrides)
MANDATORY_TIDS = {
    "0100000000000352": "emuiibo",
    "010000000000BD00": "MissionControl",
    "00FF0000636C6BFF": "sys-clk",
    "4200000000000010": "ldn_mitm",
    "420000000000000B": "sys-patch",
    "420000000000000E": "sys-ftpd",
    "420000000007E51A": "nx-ovlloader",
    "420000000007E51B": "nx-ovlreloader",
    "4200000000003103": "NSParentalControl",
    "690000000000000D": "sys-con",
    "0100000000C0FFEE": "pad-macro",
    "4200000000000FFF": "sys-triplayer",
    "0000000000534C56": "SaltyNX",
    "00FF0000A53BB665": "SysDVR",
    "0100000000000F12": "Fizeau",
    "010000000000C236": "PNGShot",
    "42000000000000A0": "sys-dock"
}

def normalize_tid(tid: str) -> Optional[str]:
    if not tid:
        return None
    tid_clean = tid.strip().replace(" ", "").upper()
    # Check if it's exactly 16 hex chars
    if len(tid_clean) == 16 and all(c in "0123456789ABCDEF" for c in tid_clean):
        return tid_clean
    return None

def clean_description(text: str) -> str:
    if not text:
        return ""
    # Remove HTML tags
    text = re.sub(r'<[^>]*>', ' ', text)
    # Remove badges and markdown images
    text = re.sub(r'!\[.*?\]\(.*?\)', '', text)
    # Convert markdown links [text](url) to text
    text = re.sub(r'\[(.*?)\]\(.*?\)', r'\1', text)
    # Remove inline code backticks
    text = re.sub(r'`(.*?)`', r'\1', text)
    # Remove bold/italic markup
    text = re.sub(r'[*_~]', '', text)
    
    # Replace multiple spaces/newlines with single space
    text = re.sub(r'\s+', ' ', text).strip()
    
    # Remove common patterns like versions: e.g. "v1.2.3", "v.1.0", "version 2.0"
    text = re.sub(r'\b[vV]er(sion)?\s*\d+(\.\d+)*\b', '', text)
    text = re.sub(r'\bv\d+(\.\d+)*\b', '', text)
    
    # Find the first sentence
    sentences = re.split(r'(?<=[.!?])\s+', text)
    if not sentences or not sentences[0]:
        return ""
    
    first_sentence = sentences[0].strip()
    # Remove spaces before punctuation
    first_sentence = re.sub(r'\s+([.,!?])', r'\1', first_sentence)
    first_sentence = re.sub(r'\s+', ' ', first_sentence).strip()
    
    # Ensure it ends with a dot if it doesn't already
    if first_sentence and not first_sentence[-1] in ['.', '!', '?']:
        first_sentence += '.'
        
    # Check length
    if len(first_sentence) > 180:
        cut = first_sentence[:177]
        last_space = cut.rfind(' ')
        if last_space > 100:
            first_sentence = first_sentence[:last_space] + "..."
        else:
            first_sentence = first_sentence[:177] + "..."
            
    return first_sentence


def load_manual_overrides() -> Dict[str, Any]:
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "manual_overrides.json")
    if os.path.exists(path):
        try:
            with open(path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception as e:
            print(f"Error loading manual overrides: {e}")
    return {}

def extract_apps_from_repo(repo_data: Any) -> List[Dict[str, Any]]:
    apps = []
    if isinstance(repo_data, list):
        for item in repo_data:
            if isinstance(item, dict):
                if "apps" in item and isinstance(item["apps"], list):
                    apps.extend(item["apps"])
                elif "name" in item:
                    apps.append(item)
    elif isinstance(repo_data, dict):
        if "apps" in repo_data and isinstance(repo_data["apps"], list):
            apps.extend(repo_data["apps"])
    return apps

def parse_github_url(url: str) -> Optional[Tuple[str, str]]:
    if not url or "github.com" not in url:
        return None
    # Normalize URL: remove trailing slashes, git extension
    url = url.strip().rstrip("/")
    if url.endswith(".git"):
        url = url[:-4]
    
    # Extract owner and repo
    match = re.search(r'github\.com/([^/]+)/([^/]+)', url)
    if match:
        owner, repo = match.group(1), match.group(2)
        # Handle cases where subpaths might be in URL
        repo = repo.split("/")[0]
        return owner, repo
    return None

def verify_tid_in_repo(
    owner: str, repo: str, tid: str, default_branch: str, offline: bool = False, refresh: bool = False
) -> Tuple[bool, List[str]]:
    evidence = []
    tid_lower = tid.lower()
    
    # Files to try for toolbox.json (most reliable)
    toolbox_paths = [
        "toolbox.json",
        f"atmosphere/contents/{tid}/toolbox.json",
        f"atmosphere/contents/{tid_lower}/toolbox.json",
        "sysmodule/toolbox.json",
        "sysmodules/toolbox.json",
        "sysmod/toolbox.json",
        f"{repo}/toolbox.json",
        f"{repo.lower()}/toolbox.json"
    ]
    
    for path in toolbox_paths:
        status, content = get_github_raw_file(owner, repo, default_branch, path, offline, refresh)
        if status == 200:
            try:
                data = json.loads(content)
                raw_tid = data.get("tid") or data.get("titleid") or data.get("title_id")
                if raw_tid:
                    norm_raw = normalize_tid(str(raw_tid))
                    if norm_raw == tid:
                        evidence.append(f"https://github.com/{owner}/{repo}/blob/{default_branch}/{path}")
                        return True, evidence
            except Exception:
                pass

    # Files to try for textual TID matching
    text_paths = [
        "Makefile",
        "CMakeLists.txt",
        "README.md",
        "Readme.md",
        "readme.md",
        "config.json",
        "npdm.json",
        "sysmod/sys-patch.json",
        "sysmod/sys-dock.json",
        "Sysmodule/sys-triplayer.json",
        "source/main.cpp",
        "src/main.cpp",
        "source/constants.hpp",
        "src/constants.hpp",
        "source/main.c",
        "src/main.c"
    ]
    for path in text_paths:
        status, content = get_github_raw_file(owner, repo, default_branch, path, offline, refresh)
        if status == 200:
            if tid in content.upper() or tid_lower in content:
                evidence.append(f"https://github.com/{owner}/{repo}/blob/{default_branch}/{path}")
                return True, evidence

    # Check releases
    releases_endpoint = f"/repos/{owner}/{repo}/releases"
    status, content = github_api_get(releases_endpoint, offline=offline, refresh=refresh)
    if status == 200:
        try:
            releases = json.loads(content)
            for release in releases:
                # Check release body or name
                body = release.get("body", "") or ""
                name = release.get("name", "") or ""
                if tid in body.upper() or tid_lower in body or tid in name.upper() or tid_lower in name:
                    evidence.append(f"https://github.com/{owner}/{repo}/releases/tag/{release.get('tag_name')}")
                    return True, evidence
                # Check assets
                for asset in release.get("assets", []):
                    asset_name = asset.get("name", "") or ""
                    if tid in asset_name.upper() or tid_lower in asset_name:
                        evidence.append(f"https://github.com/{owner}/{repo}/releases/download/{release.get('tag_name')}/{asset_name}")
                        return True, evidence
        except Exception:
            pass

    return False, evidence


def get_readme_first_paragraph(
    owner: str, repo: str, default_branch: str, offline: bool = False, refresh: bool = False
) -> Tuple[str, str]:
    readme_names = ["README.md", "readme.md", "README.txt", "readme.txt"]
    for name in readme_names:
        status, content = get_github_raw_file(owner, repo, default_branch, name, offline, refresh)
        if status == 200:
            # Simple markdown readme parser for first paragraph
            # Remove title headers, badges, etc.
            lines = content.split("\n")
            paragraphs = []
            current_para = []
            for line in lines:
                line_stripped = line.strip()
                if not line_stripped:
                    if current_para:
                        paragraphs.append(" ".join(current_para))
                        current_para = []
                    continue
                # Skip headings, badges, lists, blockquotes, tables
                if (line_stripped.startswith("#") or 
                    line_stripped.startswith("!") or 
                    line_stripped.startswith("-") or 
                    line_stripped.startswith("*") or 
                    line_stripped.startswith(">") or 
                    line_stripped.startswith("|") or
                    line_stripped.startswith("[!")):
                    continue
                current_para.append(line_stripped)
            if current_para:
                paragraphs.append(" ".join(current_para))
            
            for p in paragraphs:
                p_clean = p.strip()
                if p_clean and len(p_clean) > 10:
                    return p_clean, f"https://github.com/{owner}/{repo}/blob/{default_branch}/{name}"
    return "", ""

def process_ndeadly_line(line: str) -> Optional[Tuple[str, str]]:
    line = line.strip()
    if not line or line.startswith("#"):
        return None
    parts = line.split(None, 1)
    if len(parts) == 2:
        tid, name = parts
        norm = normalize_tid(tid)
        if norm:
            return norm, name
    return None

def merge_catalog(
    offline: bool = False, refresh: bool = False, specific_tid: Optional[str] = None, on_progress_callback = None
) -> Dict[str, ModuleInfo]:

    # 1. Fetch source lists
    ndeadly_content = fetch_ndeadly_list(offline, refresh)
    appstore_data = fetch_appstore_repo(offline, refresh)
    manual_overrides = load_manual_overrides()
    
    # Load previous research data to prevent data loss on partial runs
    prev_research = {}
    research_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "modules_research.json")
    if os.path.exists(research_path):
        try:
            with open(research_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                prev_research = data.get("modules", {})
        except Exception:
            pass

    # Parse ndeadly list
    ndeadly_modules: Dict[str, str] = {}
    for line in ndeadly_content.split("\n"):
        res = process_ndeadly_line(line)
        if res:
            tid, name = res
            ndeadly_modules[tid] = name
            
    # Parse App Store applications
    appstore_apps = extract_apps_from_repo(appstore_data)
    
    result: Dict[str, ModuleInfo] = {}
    # Keep cached research only for IDs that still belong to the authoritative
    # homebrew list (plus explicitly curated mandatory IDs). This prevents stale
    # or misclassified IDs from surviving forever in generated catalogs.
    allowed_tids = set(ndeadly_modules) | set(MANDATORY_TIDS)
    for k, v in prev_research.items():
        norm_k = normalize_tid(k) or k
        if norm_k in allowed_tids:
            result[norm_k] = ModuleInfo.from_dict(norm_k, v)

    
    # Process only the specific TID if specified
    tids_to_process = [specific_tid] if specific_tid else list(ndeadly_modules.keys())
    # Ensure mandatory TIDs are also in the list if we're doing a full run
    for tid in MANDATORY_TIDS:
        if tid not in tids_to_process:
            tids_to_process.append(tid)
            

                
    for tid in tids_to_process:
        norm_tid = normalize_tid(tid)

        if not norm_tid:
            continue
            
        # Check if already verified in previous run, and we are not in refresh mode
        if not refresh and norm_tid in prev_research and norm_tid not in manual_overrides:
            prev_mod_data = prev_research[norm_tid]
            if prev_mod_data.get("confidence") == "verified":
                # Ensure it remains verified and trigger callback
                result[norm_tid] = ModuleInfo.from_dict(norm_tid, prev_mod_data)
                if on_progress_callback:
                    on_progress_callback(result)
                continue

        # Initialize basic info
        name = ndeadly_modules.get(norm_tid, MANDATORY_TIDS.get(norm_tid, f"Unknown_{norm_tid}"))
        module = ModuleInfo(tid=norm_tid, name=name)
        
        # Apply Blocked Mappings check
        blocked_name = BLOCKED_MAPPINGS.get(norm_tid)
        
        # Check App Store app candidates
        appstore_candidates = []
        for app in appstore_apps:
            app_tid = normalize_tid(app.get("titleid") or app.get("tid") or "")
            app_name = app.get("name", "") or app.get("title", "")
            
            # Skip blocked mappings
            if blocked_name and blocked_name.lower() in app_name.lower():
                continue
                
            if app_tid == norm_tid:
                appstore_candidates.append(app)
            elif app_name.lower() == name.lower():
                appstore_candidates.append(app)
                
        # Choose the best App Store candidate
        appstore_app = None
        if appstore_candidates:
            # Prefer the one with matching TID, then the one with repo
            appstore_candidates.sort(key=lambda a: (
                normalize_tid(a.get("titleid") or a.get("tid") or "") == norm_tid,
                bool(a.get("repository") or a.get("url"))
            ), reverse=True)
            appstore_app = appstore_candidates[0]
            
        # Get repository from App Store
        repo_url = ""
        appstore_desc = ""
        appstore_desc_src = ""
        if appstore_app:
            repo_url = appstore_app.get("repository") or appstore_app.get("url") or ""
            appstore_desc = appstore_app.get("description", "")
            appstore_desc_src = "https://switch.cdn.fortheusers.org/repo.json"
            if not repo_url.startswith("http") and appstore_app.get("details"):
                repo_url = appstore_app.get("details")
                
        # If no repo url, try to search github (only on non-offline or if specific_tid, to save rate limits)
        if not repo_url and not offline and (specific_tid or norm_tid in MANDATORY_TIDS):
            search_query = f"{name} switch sysmodule"
            search_res = search_github_repositories(search_query, offline, refresh)
            items = search_res.get("items", [])
            if items:
                repo_url = items[0].get("html_url", "")
                
        # Resolve GitHub Info
        github_verified = False
        github_evidence = []
        github_desc = ""
        github_desc_src = ""
        resolved_repo = ""
        
        parsed_github = parse_github_url(repo_url)
        if parsed_github:
            owner, repo_name = parsed_github
            resolved_repo = f"https://github.com/{owner}/{repo_name}"
            
            # Fetch repo info
            repo_info = get_github_repo_info(owner, repo_name, offline, refresh)
            if repo_info:
                default_branch = repo_info.get("default_branch", "master")
                github_desc = repo_info.get("description", "")
                github_desc_src = f"https://github.com/{owner}/{repo_name}"
                
                # Verify TID in files
                github_verified, github_evidence = verify_tid_in_repo(
                    owner, repo_name, norm_tid, default_branch, offline, refresh
                )
                
                # Fetch README paragraph if description is empty or to try as fallback
                if not github_desc:
                    readme_p, readme_src = get_readme_first_paragraph(
                        owner, repo_name, default_branch, offline, refresh
                    )
                    if readme_p:
                        github_desc = readme_p
                        github_desc_src = readme_src
                        
        # Fill module details
        module.repository = resolved_repo or repo_url
        
        # Decide description based on priority:
        # 1. GitHub repo description
        # 2. README paragraph
        # 3. App Store description
        if github_desc:
            module.description_en = clean_description(github_desc)
            module.description_source = github_desc_src
        elif appstore_desc:
            module.description_en = clean_description(appstore_desc)
            module.description_source = appstore_desc_src
            
        module.tid_evidence = github_evidence
        
        # Decide confidence
        if github_verified:
            module.confidence = "verified"
        elif module.repository and appstore_app:
            module.confidence = "probable"
        else:
            module.confidence = "unresolved"
            
        # AI-based fallback for unresolved/probable modules or missing descriptions
        if not (blocked_name and blocked_name.lower() in module.name.lower()):
            # 1. Try to find repository URL if not found yet
            if module.confidence == "unresolved" and not module.repository:
                prompt_repo = f"Find the official GitHub repository URL for the Nintendo Switch sysmodule '{module.name}' with Title ID '{norm_tid}'. Return ONLY the URL (starting with https://github.com/), or return 'None' if you cannot find it."
                ai_repo = query_gemini(prompt_repo, offline=offline, refresh=refresh)
                if ai_repo and ai_repo.startswith("https://github.com/"):
                    ai_repo = ai_repo.strip()
                    # Try to verify it
                    parsed_github = parse_github_url(ai_repo)
                    if parsed_github:
                        owner, repo_name = parsed_github
                        resolved_repo = f"https://github.com/{owner}/{repo_name}"
                        repo_info = get_github_repo_info(owner, repo_name, offline, refresh)
                        if repo_info:
                            default_branch = repo_info.get("default_branch", "master")
                            github_verified, github_evidence = verify_tid_in_repo(
                                owner, repo_name, norm_tid, default_branch, offline, refresh
                            )
                            module.repository = resolved_repo
                            module.tid_evidence = github_evidence
                            if github_verified:
                                module.confidence = "verified"
                                # Fetch repo description
                                github_desc = repo_info.get("description", "")
                                github_desc_src = f"https://github.com/{owner}/{repo_name}"
                                if not github_desc:
                                    readme_p, readme_src = get_readme_first_paragraph(
                                        owner, repo_name, default_branch, offline, refresh
                                    )
                                    if readme_p:
                                        github_desc = readme_p
                                        github_desc_src = readme_src
                                if github_desc:
                                    module.description_en = clean_description(github_desc)
                                    module.description_source = github_desc_src
                            else:
                                module.confidence = "probable"

            # 2. Try to get description if still empty
            if not module.description_en:
                prompt_desc = f"Summarize what the Nintendo Switch sysmodule '{module.name}' does. Provide a single, short, neutral English sentence of up to 180 characters, without version numbers, HTML or markdown. Do not start with generic phrases."
                ai_desc = query_gemini(prompt_desc, offline=offline, refresh=refresh)
                if ai_desc and ai_desc.lower() != "none" and len(ai_desc) > 10:
                    module.description_en = clean_description(ai_desc)
                    module.description_source = "gemini-3.5-flash"


            
        # Apply Manual Overrides (Highest Priority)
        if norm_tid in manual_overrides:
            override = manual_overrides[norm_tid]
            if "name" in override:
                module.name = override["name"]
            if "aliases" in override:
                module.aliases = override["aliases"]
            if "repository" in override:
                module.repository = override["repository"]
            if "description_en" in override:
                module.description_en = clean_description(override["description_en"])
            if "description_source" in override:
                module.description_source = override["description_source"]
            if "tid_evidence" in override:
                module.tid_evidence = override["tid_evidence"]
            if "confidence" in override:
                module.confidence = override["confidence"]
            if "notes" in override:
                module.notes = override["notes"]
                
        # Validate that name mapping doesn't violate rules
        if blocked_name and blocked_name.lower() in module.name.lower():
            module.confidence = "conflict"
            module.notes = f"Conflict: Blocked mapping. {norm_tid} must not be associated with {blocked_name}."
            module.repository = ""
            module.description_en = ""
            module.tid_evidence = []
            

            
        result[norm_tid] = module
        if on_progress_callback:
            on_progress_callback(result)
            
    return result
