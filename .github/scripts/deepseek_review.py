#!/usr/bin/env python3
"""
Shared script for DeepSeek code review.
Called by GitHub Actions workflows.

Configuration via environment variables:
  - DEEPSEEK_API_KEY       (required)
  - DEEPSEEK_API_URL       (optional, default: https://api.deepseek.com/chat/completions)
  - DEEPSEEK_MODEL         (optional, default: deepseek-chat)
  - DEEPSEEK_MAX_TOKENS    (optional, default: 4096)
  - DEEPSEEK_RATE_LIMIT    (optional, default: 10 — max reviews per GITHUB_RUN_ID window)
  - GITHUB_REPOSITORY      (set by Actions)
  - GITHUB_RUN_ID          (set by Actions)
  - RUNNER_TEMP            (set by Actions, used for unique temp paths)
  - GITHUB_OUTPUT          (set by Actions)
"""

import json
import os
import sys
import time
import urllib.request
import urllib.error


def get_env_or_fail(key: str) -> str:
    """Get a required environment variable or exit with a clear error."""
    value = os.environ.get(key)
    if not value:
        print(f"::error::Missing required environment variable: {key}")
        sys.exit(1)
    return value


def get_unique_path(basename: str) -> str:
    """Create a unique temp path using RUNNER_TEMP and GITHUB_RUN_ID to avoid
    collisions when multiple workflows run on the same runner."""
    run_id = os.environ.get("GITHUB_RUN_ID", str(os.getpid()))
    temp_dir = os.environ.get("RUNNER_TEMP", "/tmp")
    return os.path.join(temp_dir, f"deepseek_{run_id}_{basename}")


DIFF_PATH = None
OUTPUT_PATH = None


def init_paths():
    """Initialize unique file paths for this workflow run."""
    global DIFF_PATH, OUTPUT_PATH
    DIFF_PATH = get_unique_path("pr_diff.txt")
    OUTPUT_PATH = get_unique_path("review_output.txt")


def save_diff_path() -> str:
    """Return the path where the diff should be written by the calling step."""
    if DIFF_PATH is None:
        init_paths()
    return DIFF_PATH


def output_path() -> str:
    """Return the path where the review output will be written."""
    if OUTPUT_PATH is None:
        init_paths()
    return OUTPUT_PATH


def parse_max_tokens(raw: str) -> int:
    """Parse and validate max_tokens. Must be a positive integer <= 65536."""
    try:
        val = int(raw)
    except (ValueError, TypeError):
        print(f"::error::DEEPSEEK_MAX_TOKENS must be an integer, got '{raw}'")
        sys.exit(1)

    if val < 1:
        print(f"::error::DEEPSEEK_MAX_TOKENS must be >= 1, got {val}")
        sys.exit(1)

    if val > 65536:
        print(f"::warning::DEEPSEEK_MAX_TOKENS capped at 65536, got {val}")
        val = 65536

    return val


def check_rate_limit() -> None:
    """
    Simple file-based rate limit to prevent spam.
    Allows DEEPSEEK_RATE_LIMIT calls per workflow run.
    Default: 10.
    """
    run_id = os.environ.get("GITHUB_RUN_ID", "unknown")
    max_calls = int(os.environ.get("DEEPSEEK_RATE_LIMIT", "10"))

    # Rate limit file is scoped to the run_id
    counter_file = os.path.join(
        os.environ.get("RUNNER_TEMP", "/tmp"),
        f"deepseek_rate_{run_id}.count"
    )

    count = 0
    if os.path.exists(counter_file):
        try:
            with open(counter_file, "r") as f:
                count = int(f.read().strip())
        except (ValueError, OSError):
            count = 0

    count += 1

    with open(counter_file, "w") as f:
        f.write(str(count))

    if count > max_calls:
        print(f"::error::Rate limit exceeded: {count} calls > {max_calls} max "
              f"per workflow run. Set DEEPSEEK_RATE_LIMIT to increase, or "
              f"reduce /review usage.")
        sys.exit(1)

    print(f"::debug::API call {count}/{max_calls} for this run")


def call_deepseek(
    system_prompt: str,
    user_prompt: str,
) -> str:
    """
    Call DeepSeek Chat API with the given prompts.
    Returns the response text on success, or exits with error on failure.
    """
    api_key = get_env_or_fail("DEEPSEEK_API_KEY")
    api_url = os.environ.get(
        "DEEPSEEK_API_URL",
        "https://api.deepseek.com/chat/completions"
    )
    model = os.environ.get("DEEPSEEK_MODEL", "deepseek-chat")
    max_tokens = parse_max_tokens(
        os.environ.get("DEEPSEEK_MAX_TOKENS", "4096")
    )

    check_rate_limit()

    payload = json.dumps({
        "model": model,
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user", "content": user_prompt}
        ],
        "max_tokens": max_tokens,
        "temperature": 0.2,
    })

    # Log safely: mask the key in the truncated header log
    safe_url = api_url
    print(f"::debug::Calling DeepSeek API: model={model}, "
          f"url={safe_url}, max_tokens={max_tokens}")

    req = urllib.request.Request(
        api_url,
        data=payload.encode("utf-8"),
        headers={
            "Content-Type": "application/json",
            "Authorization": f"Bearer {api_key}",
        },
    )

    try:
        with urllib.request.urlopen(req, timeout=120) as resp:
            response_json = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8", errors="replace")
        # Mask any occurrence of the API key or first 20 chars to prevent leakage
        safe_body = error_body.replace(api_key, "***")
        for i in range(len(api_key) - 5):
            partial = api_key[i:i+20]
            safe_body = safe_body.replace(partial, "***")
        print(f"::error::DeepSeek API call failed with status {e.code}")
        print(f"::debug::API error response (key masked): {safe_body[:200]}")
        sys.exit(1)
    except urllib.error.URLError as e:
        print(f"::error::Network error calling DeepSeek API: {e.reason}")
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"::error::Invalid JSON response from DeepSeek API: {e}")
        sys.exit(1)

    try:
        return response_json["choices"][0]["message"]["content"]
    except (KeyError, IndexError) as e:
        print(f"::error::Unexpected API response structure: {e}")
        safe_keys = {k: type(v).__name__ for k, v in response_json.items()}
        print(f"::debug::Response top-level keys/types: {safe_keys}")
        sys.exit(1)


def load_diff(path: str = None) -> str:
    """Load the diff from file, with size info."""
    if path is None:
        path = DIFF_PATH or save_diff_path()

    if not os.path.exists(path):
        print(f"::error::Diff file not found at {path}")
        sys.exit(1)

    with open(path, "r") as f:
        content = f.read()

    if not content.strip():
        print("::warning::Diff is empty — nothing to review.")
        return ""

    size_kb = len(content) / 1024
    print(f"::debug::Loaded diff: {len(content)} bytes ({size_kb:.1f} KB)")
    return content


def write_review_output(text: str, path: str = None):
    """Write the review result to a file for downstream steps."""
    if path is None:
        path = OUTPUT_PATH or output_path()

    with open(path, "w") as f:
        f.write(text)
    print(f"::debug::Review output written to {path} ({len(text)} chars)")


def write_github_output(key: str, value: str):
    """Write to GITHUB_OUTPUT for sharing between steps."""
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a") as f:
            f.write(f"{key}={value}\n")