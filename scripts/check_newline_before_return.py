#!/usr/bin/env python3
"""
Check for missing blank lines before return statements in C/H files.

This script enforces a coding style where return statements should be preceded
by a blank line, except in these cases:
  - Right after an opening brace {
  - Right after a comment (/* */ or //) that itself has a blank line before it

Usage:
    # Check only modified lines in files changed from origin/main
    ./check_newline_before_return.py

    # Check all lines in all C/H files in the project
    ./check_newline_before_return.py --all

"""

import subprocess
import sys
import re
import argparse
from pathlib import Path

def format_git_ref(ref):
    """Format a git reference for comparison.

    Args:
        ref: Branch name or commit hash

    Returns:
        Formatted reference (adds origin/ prefix for branch names, leaves commit hashes as-is)
    """
    # If it looks like a commit hash (alphanumeric, no slashes), use as-is
    if re.match(r'^[0-9a-f]{6,40}$', ref, re.IGNORECASE):
        return ref
    # If it already has origin/ or another remote, use as-is
    if '/' in ref:
        return ref
    # Otherwise, assume it's a branch name and prepend origin/
    return f"origin/{ref}"

def get_changed_files(base_ref='main'):
    """Get list of changed C/H files in the current branch

    Args:
        base_ref: The base branch or commit to compare against (default: 'main')
    """
    try:
        git_ref = format_git_ref(base_ref)
        result = subprocess.run(
            ["git", "diff", "--name-only", f"{git_ref}.."],
            capture_output=True,
            text=True,
            check=True
        )
        files = [f for f in result.stdout.strip().split('\n') if f.endswith(('.c', '.h'))]
        return files if files != [''] else []
    except subprocess.CalledProcessError:
        return []

def get_changed_line_numbers(filepath, base_ref='main'):
    """Get line numbers that were changed in the given file

    Args:
        filepath: Path to the file to check
        base_ref: The base branch or commit to compare against (default: 'main')
    """
    try:
        git_ref = format_git_ref(base_ref)
        result = subprocess.run(
            ["git", "diff", "-U0", f"{git_ref}..", filepath],
            capture_output=True,
            text=True,
            check=False
        )
        changed_lines = set()
        for line in result.stdout.split('\n'):
            # Parse hunk headers like @@ -10,3 +10,4 @@
            match = re.search(r'@@.*\+(\d+)(?:,(\d+))?', line)
            if match:
                start_line = int(match.group(1))
                count = int(match.group(2)) if match.group(2) else 1
                for i in range(start_line, start_line + count):
                    changed_lines.add(i)
        return changed_lines
    except:
        return set()

def get_all_project_files():
    """Get all C/H files in current directory and subdirectories"""
    project_path = Path(".")
    files = []
    exclude_patterns = ["build", "generated"]

    for pattern in ["**/*.c", "**/*.h"]:
        for filepath in project_path.glob(pattern):
            # Skip files in excluded directories or directories starting with twister-out
            parts = filepath.parts
            if any(excluded in parts for excluded in exclude_patterns):
                continue
            if any(part.startswith("twister-out") for part in parts):
                continue
            files.append(str(filepath))

    return files

def check_newline_before_return(filepath, changed_lines=None):
    """Check for missing newlines before return statements

    Args:
        filepath: Path to the file to check
        changed_lines: Set of line numbers to check. If None, check all lines.
    """
    try:
        with open(filepath, 'r') as f:
            lines = f.readlines()
    except:
        return []

    issues = []
    for i in range(1, len(lines)):
        # If changed_lines is specified, only check lines that changed
        if changed_lines is not None and (i + 1) not in changed_lines:
            continue

        if re.match(r'^\s*return\b', lines[i]):
            prev_line = lines[i-1].strip()
            prev_line_unstripped = lines[i-1]

            # Skip if previous line is empty
            if not prev_line:
                continue

            # Skip if previous line is just whitespace
            if re.match(r'^\s*$', prev_line_unstripped):
                continue

            # Check if previous line is any type of comment
            is_single_line_comment = re.match(r'^\s*//', prev_line_unstripped)
            is_comment_start = re.match(r'^\s*/\*', prev_line_unstripped)
            is_comment_end = re.search(r'\*/', prev_line_unstripped)
            is_comment_continuation = re.match(r'^\s*\*(?!/)', prev_line_unstripped)

            is_any_comment = (is_single_line_comment or is_comment_start or
                             is_comment_end or is_comment_continuation)

            if is_any_comment:
                # Find the start of the comment block
                comment_start_idx = i - 1

                # For single-line comments (//), already at start
                if not is_single_line_comment:
                    # Trace back to find where multiline comment starts
                    for j in range(i - 2, -1, -1):
                        line_stripped = lines[j].strip()
                        # Check if this line starts the comment
                        if re.match(r'^\s*/\*', lines[j]):
                            comment_start_idx = j
                            break
                        # If we hit a line that's not part of comment, stop
                        if not (re.match(r'^\s*\*', lines[j]) or re.search(r'/\*', lines[j])):
                            break

                # Now check if there's a blank line before the comment block
                if comment_start_idx > 0:
                    line_before_comment = lines[comment_start_idx - 1].strip()
                    line_before_comment_unstripped = lines[comment_start_idx - 1]

                    # Allow if line before comment is blank or ends with {
                    if not line_before_comment or re.match(r'^\s*$', line_before_comment_unstripped):
                        continue
                    if re.search(r'\{\s*$', line_before_comment):
                        continue
                    # If no blank line before comment, report issue
                # Comment at start of function is also an issue
                issues.append({
                    'file': filepath,
                    'line': i + 1,
                    'message': 'Missing blank line before return statement'
                })
            else:
                # Skip if previous line ends with { or is just } or )
                if re.search(r'\{\s*$', prev_line):
                    continue

                # If we reach here, we have actual code before return with no blank line
                issues.append({
                    'file': filepath,
                    'line': i + 1,
                    'message': 'Missing blank line before return statement'
                })
    return issues

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Check for missing newlines before return statements')
    parser.add_argument('--all', action='store_true',
                       help='Check all files in project (default: only changed lines in changed files)')
    parser.add_argument('--base-ref', default='main',
                       help='Base branch or commit to compare against (default: main)')
    args = parser.parse_args()

    all_issues = []

    if args.all:
        files = get_all_project_files()
        for filepath in files:
            all_issues.extend(check_newline_before_return(filepath))
    else:
        files = get_changed_files(args.base_ref)
        for filepath in files:
            changed_lines = get_changed_line_numbers(filepath, args.base_ref)
            all_issues.extend(check_newline_before_return(filepath, changed_lines))

    if all_issues:
        for issue in all_issues:
            print(f"{issue['file']}:{issue['line']}: {issue['message']}")
        print(f"\nTotal issues found: {len(all_issues)}")
        sys.exit(1)
    print("✓ No newline issues found")

