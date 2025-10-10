#!/usr/bin/env python3
"""
download_exes_on_github.py

Search GitHub for repos matching a query, list their releases, and download release assets ending in .exe
to a structured local folder.

Usage:
    GITHUB_TOKEN=ghp_xxx python download_exes_on_github.py

Requirements:
    pip install requests
"""

import os
import sys
import time
import requests
from pathlib import Path

# ---- Config ----
GITHUB_TOKEN = os.environ.get("GITHUB_TOKEN")
HEADERS = {"Accept": "application/vnd.github+json"}
if GITHUB_TOKEN:
    HEADERS["Authorization"] = f"Bearer {GITHUB_TOKEN}"

QUERY = "windows installer in:description language:C++"
MAX_REPOS = 10
PER_PAGE = 30
SLEEP_BETWEEN = 1.0
DOWNLOAD_DIR = Path("downloads")   # all exe files go here

SEARCH_REPOS_URL = "https://api.github.com/search/repositories"
LIST_RELEASES_URL = "https://api.github.com/repos/{owner}/{repo}/releases"

# ---- Functions ----

def safe_filename(name: str) -> str:
    """Strip or replace characters that aren't filesystem-safe."""
    return "".join(c if c.isalnum() or c in "._- " else "_" for c in name)

def search_repositories(query, max_repos=100):
    repos = []
    page = 1
    while len(repos) < max_repos:
        params = {"q": query, "per_page": PER_PAGE, "page": page}
        r = requests.get(SEARCH_REPOS_URL, headers=HEADERS, params=params)
        if r.status_code != 200:
            print(f"Error searching repos: {r.status_code} {r.text}", file=sys.stderr)
            break
        payload = r.json()
        items = payload.get("items", [])
        if not items:
            break
        repos.extend(items)
        if "next" not in r.links:
            break
        page += 1
        time.sleep(SLEEP_BETWEEN)
    return repos[:max_repos]

def list_releases(owner, repo):
    releases = []
    page = 1
    while True:
        url = LIST_RELEASES_URL.format(owner=owner, repo=repo)
        params = {"per_page": PER_PAGE, "page": page}
        r = requests.get(url, headers=HEADERS, params=params)
        if r.status_code == 404:
            break
        if r.status_code != 200:
            print(f"Error listing releases for {owner}/{repo}: {r.status_code}", file=sys.stderr)
            break
        data = r.json()
        if not data:
            break
        releases.extend(data)
        if "next" not in r.links:
            break
        page += 1
        time.sleep(SLEEP_BETWEEN)
    return releases

def download_file(url, dest_path: Path):
    """Download a file if it doesn't already exist."""
    dest_path.parent.mkdir(parents=True, exist_ok=True)
    if dest_path.exists():
        print(f"[skip] Already exists: {dest_path}")
        return
    print(f"[downloading] {url} → {dest_path}")
    with requests.get(url, headers=HEADERS, stream=True) as r:
        r.raise_for_status()
        with open(dest_path, "wb") as f:
            for chunk in r.iter_content(chunk_size=8192):
                f.write(chunk)

# ---- Main ----

def main():
    print(f"Searching GitHub repos with query: {QUERY!r} (max {MAX_REPOS})", file=sys.stderr)
    repos = search_repositories(QUERY, max_repos=MAX_REPOS)
    print(f"Found {len(repos)} repos; scanning releases...", file=sys.stderr)

    for repo_meta in repos:
        full_name = repo_meta["full_name"]
        owner, repo = full_name.split("/")
        releases = list_releases(owner, repo)
        if not releases:
            continue

        for rel in releases:
            tag = rel.get("tag_name", "<no-tag>")
            assets = rel.get("assets", [])
            for asset in assets:
                name = asset.get("name", "")
                url = asset.get("browser_download_url")
                if not name.lower().endswith(".exe") or not url:
                    continue

                # Structure: downloads/owner/repo/tag/filename.exe
                dest = DOWNLOAD_DIR / safe_filename(name)
                # dest = DOWNLOAD_DIR / safe_filename(owner) / safe_filename(repo) / safe_filename(tag) / safe_filename(name)
                try:
                    download_file(url, dest)
                except Exception as e:
                    print(f"[error] Failed to download {url}: {e}", file=sys.stderr)
                time.sleep(0.5)

if __name__ == "__main__":
    main()

