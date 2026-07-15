import os
import json
import hashlib
import urllib.request
import urllib.error
from typing import Dict, Any, Optional, Tuple

CACHE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cache")

def get_github_token() -> Optional[str]:
    return os.environ.get("GITHUB_TOKEN")

def init_cache():
    if not os.path.exists(CACHE_DIR):
        os.makedirs(CACHE_DIR)

def get_cache_path(url: str) -> str:
    # Use MD5 hash of the URL to generate a unique filename
    url_hash = hashlib.md5(url.encode("utf-8")).hexdigest()
    return os.path.join(CACHE_DIR, f"{url_hash}.json")

def read_from_cache(url: str) -> Optional[Tuple[int, str]]:
    init_cache()
    cache_path = get_cache_path(url)
    if os.path.exists(cache_path):
        try:
            with open(cache_path, "r", encoding="utf-8") as f:
                data = json.load(f)
                return data.get("status_code", 200), data.get("content", "")
        except Exception:
            return None
    return None

def write_to_cache(url: str, status_code: int, content: str):
    init_cache()
    cache_path = get_cache_path(url)
    try:
        with open(cache_path, "w", encoding="utf-8") as f:
            json.dump({"url": url, "status_code": status_code, "content": content}, f, ensure_ascii=False, indent=2)
    except Exception as e:
        print(f"Error writing to cache for {url}: {e}")

def http_get(url: str, headers: Optional[Dict[str, str]] = None, offline: bool = False, refresh: bool = False) -> Tuple[int, str]:
    if not refresh and not offline:
        cached = read_from_cache(url)
        if cached is not None:
            return cached

    if offline:
        cached = read_from_cache(url)
        if cached is not None:
            return cached
        raise urllib.error.URLError(f"Offline mode: URL not in cache: {url}")

    # Build request
    req_headers = {
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36"
    }
    if headers:
        req_headers.update(headers)

    req = urllib.request.Request(url, headers=req_headers)
    try:
        with urllib.request.urlopen(req, timeout=15) as response:
            status_code = response.status
            content = response.read().decode("utf-8", errors="replace")
            write_to_cache(url, status_code, content)
            return status_code, content
    except urllib.error.HTTPError as e:
        status_code = e.code
        try:
            content = e.read().decode("utf-8", errors="replace")
        except Exception:
            content = str(e)
        # Even errors like 404 should be cached to avoid spamming the API
        write_to_cache(url, status_code, content)
        return status_code, content
    except Exception as e:
        # Don't cache connection failures or timeouts
        raise e

def fetch_ndeadly_list(offline: bool = False, refresh: bool = False) -> str:
    url = "https://gist.githubusercontent.com/ndeadly/a4b8c01bb453028cd0008f282098f696/raw/homebrew_sysmodules.txt"
    _, content = http_get(url, offline=offline, refresh=refresh)
    return content

def fetch_appstore_repo(offline: bool = False, refresh: bool = False) -> Dict[str, Any]:
    url = "https://switch.cdn.fortheusers.org/repo.json"
    status, content = http_get(url, offline=offline, refresh=refresh)
    if status == 200:
        try:
            return json.loads(content)
        except Exception:
            return {}
    return {}

def github_api_get(endpoint: str, offline: bool = False, refresh: bool = False) -> Tuple[int, str]:
    url = f"https://api.github.com{endpoint}"
    headers = {
        "Accept": "application/vnd.github.v3+json"
    }
    token = get_github_token()
    if token:
        headers["Authorization"] = f"token {token}"
    
    return http_get(url, headers=headers, offline=offline, refresh=refresh)

def get_github_repo_info(owner: str, repo: str, offline: bool = False, refresh: bool = False) -> Dict[str, Any]:
    endpoint = f"/repos/{owner}/{repo}"
    status, content = github_api_get(endpoint, offline=offline, refresh=refresh)
    if status == 200:
        try:
            return json.loads(content)
        except Exception:
            return {}
    return {}

def search_github_repositories(query: str, offline: bool = False, refresh: bool = False) -> Dict[str, Any]:
    # URL encode query manually to avoid external dependency
    encoded_query = urllib.parse.quote(query)
    endpoint = f"/search/repositories?q={encoded_query}&per_page=5"
    status, content = github_api_get(endpoint, offline=offline, refresh=refresh)
    if status == 200:
        try:
            return json.loads(content)
        except Exception:
            return {}
    return {}

def get_github_raw_file(owner: str, repo: str, branch: str, path: str, offline: bool = False, refresh: bool = False) -> Tuple[int, str]:
    url = f"https://raw.githubusercontent.com/{owner}/{repo}/{branch}/{path}"
    # Use standard raw domain without auth headers to bypass API limits where possible
    try:
        return http_get(url, offline=offline, refresh=refresh)
    except Exception:
        # Fallback to API if raw fails due to connection issues, but raw is preferred
        endpoint = f"/repos/{owner}/{repo}/contents/{path}?ref={branch}"
        status, content = github_api_get(endpoint, offline=offline, refresh=refresh)
        if status == 200:
            try:
                data = json.loads(content)
                if data.get("encoding") == "base64":
                    import base64
                    decoded = base64.b64decode(data["content"]).decode("utf-8", errors="replace")
                    return 200, decoded
            except Exception:
                pass
        return status, content

def query_gemini(prompt: str, offline: bool = False, refresh: bool = False) -> Optional[str]:
    # We can cache Gemini queries to prevent hitting the local server too often
    url = f"gemini://{hashlib.md5(prompt.encode('utf-8')).hexdigest()}"
    if not refresh and not offline:
        cached = read_from_cache(url)
        if cached is not None:
            return cached[1]
    elif offline:
        cached = read_from_cache(url)
        if cached is not None:
            return cached[1]
        return None

    api_url = "http://localhost:20128/v1/chat/completions"
    data = {
        "model": "kiro/claude-sonnet-4.5",
        "messages": [{"role": "user", "content": prompt}],
        "temperature": 0.1,
        "stream": False
    }
    try:
        req_data = json.dumps(data).encode("utf-8")
        req = urllib.request.Request(
            api_url,
            data=req_data,
            headers={
                "Content-Type": "application/json",
                "Authorization": "Bearer sk-a42ea38fcbf6f291-02aa37-e9755ddc"
            }
        )
        with urllib.request.urlopen(req, timeout=30) as response:
            res_data = json.loads(response.read().decode("utf-8"))
            content = res_data["choices"][0]["message"]["content"].strip()
            write_to_cache(url, 200, content)
            return content
    except Exception as e:
        print(f"Error querying AI API for {url}: {e}")
        # Fallback to cache if available even if not in offline mode, but don't fail
        cached = read_from_cache(url)
        if cached is not None:
            return cached[1]
        return None


