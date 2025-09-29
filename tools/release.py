#!/usr/bin/env python3
"""
Release helper script for smart-grind-by-weight
Creates tags and triggers automated releases
"""

import subprocess
import sys
import re
import json
import urllib.request
from datetime import datetime
from pathlib import Path

def run_command(cmd, capture_output=True):
    """Run a shell command and return the result"""
    result = subprocess.run(cmd, shell=True, capture_output=capture_output, text=True)
    if result.returncode != 0 and capture_output:
        print(f"Error running command: {cmd}")
        print(f"Error: {result.stderr}")
        return None
    return result.stdout.strip() if capture_output else result.returncode == 0

def get_current_version():
    """Get the latest git tag or return v0.0.0 if none exists"""
    latest_tag = run_command("git describe --tags --abbrev=0 2>/dev/null")
    return latest_tag if latest_tag else "v0.0.0"

def increment_version(version, increment_type):
    """Increment version number based on type (major, minor, patch)"""
    # Remove 'v' prefix if present
    version = version.lstrip('v')
    
    try:
        major, minor, patch = map(int, version.split('.'))
    except ValueError:
        print(f"Invalid version format: {version}")
        return None
    
    if increment_type == 'major':
        major += 1
        minor = 0
        patch = 0
    elif increment_type == 'minor':
        minor += 1
        patch = 0
    elif increment_type == 'patch':
        patch += 1
    
    return f"v{major}.{minor}.{patch}"

def check_git_status():
    """Check if git working directory is clean"""
    status = run_command("git status --porcelain")
    if status:
        print("Warning: You have uncommitted changes:")
        print(status)
        response = input("Continue anyway? (y/N): ")
        return response.lower() == 'y'
    return True

def get_firmware_version():
    """Get current firmware version from build_info.h"""
    try:
        import re
        build_info_path = Path(__file__).parent.parent / "src/config/build_info.h"
        if build_info_path.exists():
            content = build_info_path.read_text()
            match = re.search(r'#define BUILD_FIRMWARE_VERSION "([^"]*)"', content)
            if match:
                return match.group(1)
    except Exception:
        pass
    return "unknown"

def get_contributors():
    """Fetch contributors from GitHub API and filter out bots/Claude"""
    try:
        # Try to get repo URL from git remote
        remote_url = run_command("git remote get-url origin")
        if not remote_url:
            return []
        
        # Extract owner/repo from GitHub URL
        if "github.com" in remote_url:
            # Handle both SSH and HTTPS URLs
            repo_path = remote_url.split("github.com")[-1].strip(":/").replace(".git", "")
            owner, repo = repo_path.split("/")[:2]
        else:
            return []
        
        # Fetch contributors from GitHub API
        api_url = f"https://api.github.com/repos/{owner}/{repo}/contributors"
        
        with urllib.request.urlopen(api_url) as response:
            contributors = json.loads(response.read())
        
        # Filter out bots and Claude-related accounts
        excluded_patterns = [
            'claude', 'bot', 'github-actions', 'dependabot', 
            'renovate', 'greenkeeper', 'codecov'
        ]
        
        human_contributors = []
        for contributor in contributors:
            login = contributor.get('login', '').lower()
            if not any(pattern in login for pattern in excluded_patterns):
                human_contributors.append({
                    'login': contributor['login'],
                    'contributions': contributor['contributions'],
                    'html_url': contributor['html_url']
                })
        
        # Sort by contribution count
        human_contributors.sort(key=lambda x: x['contributions'], reverse=True)
        return human_contributors
        
    except Exception as e:
        print(f"Note: Could not fetch contributors: {e}")
        return []

def format_contributors_section(contributors):
    """Format contributors into a nice acknowledgment section"""
    if not contributors:
        return ""
    
    section = "\n## 🙏 Contributors\n\n"
    section += "Special thanks to everyone who contributed to this release:\n\n"
    
    for contributor in contributors:
        section += f"- [@{contributor['login']}]({contributor['html_url']}) "
        section += f"({contributor['contributions']} contribution{'s' if contributor['contributions'] != 1 else ''})\n"
    
    section += "\nYour contributions help make Smart Grind-by-Weight better for everyone! 🎉\n"
    return section

def update_firmware_version(new_version):
    """Update firmware version in build_info.h"""
    try:
        import re
        build_info_path = Path(__file__).parent.parent / "src/config/build_info.h"
        if build_info_path.exists():
            content = build_info_path.read_text()
            # Remove 'v' prefix if present
            version_number = new_version.lstrip('v')
            # Update the version string
            updated_content = re.sub(
                r'#define BUILD_FIRMWARE_VERSION "([^"]*)"',
                f'#define BUILD_FIRMWARE_VERSION "{version_number}"',
                content
            )
            build_info_path.write_text(updated_content)
            print(f"Updated BUILD_FIRMWARE_VERSION to: {version_number}")
            return True
    except Exception as e:
        print(f"Failed to update firmware version: {e}")
        return False
    return False

def create_release():
    """Interactive release creation"""
    print("=== Smart Grind Release Helper ===\n")
    
    # Check git status
    if not check_git_status():
        print("Aborting release.")
        return False
    
    # Get current version
    current_version = get_current_version()
    firmware_version = get_firmware_version()
    print(f"Current git version: {current_version}")
    print(f"Current firmware version: v{firmware_version}")
    
    # Show recent commits
    print("\nRecent commits since last tag:")
    if current_version != "v0.0.0":
        commits = run_command(f"git log --oneline {current_version}..HEAD")
    else:
        commits = run_command("git log --oneline -10")
    
    if commits:
        print(commits)
    else:
        print("No new commits since last tag.")
        return False
    
    print("\nWhat type of release is this?")
    print("1. Patch (bug fixes): v1.0.0 -> v1.0.1")
    print("2. Minor (new features): v1.0.0 -> v1.1.0") 
    print("3. Major (breaking changes): v1.0.0 -> v2.0.0")
    print("4. Custom version")
    
    choice = input("\nEnter choice (1-4): ").strip()
    
    if choice == '1':
        new_version = increment_version(current_version, 'patch')
    elif choice == '2':
        new_version = increment_version(current_version, 'minor')
    elif choice == '3':
        new_version = increment_version(current_version, 'major')
    elif choice == '4':
        custom_version = input("Enter custom version (e.g., v1.2.3): ").strip()
        if not custom_version.startswith('v'):
            custom_version = 'v' + custom_version
        new_version = custom_version
    else:
        print("Invalid choice.")
        return False
    
    if not new_version:
        return False
    
    print(f"\nPreparing release: {new_version}")
    
    # Ask for release notes
    print("\nEnter release notes (optional, leave empty for basic release):")
    print("You can use markdown formatting. Press Ctrl+D (Unix) or Ctrl+Z+Enter (Windows) when finished.")
    print("Example:")
    print("🚀 Smart Grind v1.2.3")
    print("")
    print("## New Features")
    print("- Added time-based grinding mode")
    print("- Improved BLE connection stability")
    print("")
    print("## Bug Fixes")
    print("- Fixed load cell calibration drift")
    print("")
    print("Enter your release notes:")
    
    release_notes_lines = []
    try:
        while True:
            line = input()
            release_notes_lines.append(line)
    except EOFError:
        pass
    
    release_notes = '\n'.join(release_notes_lines).strip()
    
    # Fetch contributors and add to release notes
    print("\nFetching contributors...")
    contributors = get_contributors()
    contributors_section = format_contributors_section(contributors)
    
    # Combine release notes with contributors
    final_release_notes = release_notes
    if contributors_section:
        final_release_notes += contributors_section
    
    # Show preview
    print(f"\n--- Release Preview ---")
    print(f"Version: {new_version}")
    if final_release_notes:
        print(f"Release Notes:")
        print(final_release_notes)
    else:
        print("Release Notes: (Basic release message)")
    print("--- End Preview ---")
    
    # Confirm
    response = input(f"\nProceed with release {new_version}? (y/N): ")
    if response.lower() != 'y':
        print("Release cancelled.")
        return False
    
    # Update firmware version locally
    print(f"\nUpdating firmware version to {new_version}...")
    if not update_firmware_version(new_version):
        print("Failed to update firmware version.")
        return False
    
    # Commit the version update
    print("Committing version update...")
    if not run_command('git add src/config/build_info.h', capture_output=False):
        print("Failed to stage version update.")
        return False
    
    commit_message = f"chore: Update firmware version to {new_version.lstrip('v')}"
    if not run_command(f'git commit -m "{commit_message}"', capture_output=False):
        print("Failed to commit version update.")
        return False
    
    # Create tag with release notes (including contributors)
    tag_message = final_release_notes if final_release_notes else f"Release {new_version}"
    
    print(f"Creating annotated tag {new_version}...")
    if not run_command(f'git tag -a {new_version} -m "{tag_message}"', capture_output=False):
        print("Failed to create tag.")
        return False
    
    # Push commit and tag
    print("Pushing version commit...")
    if not run_command('git push', capture_output=False):
        print("Failed to push version commit.")
        return False
    
    print(f"Pushing tag {new_version}...")
    if not run_command(f'git push origin {new_version}', capture_output=False):
        print("Failed to push tag.")
        return False
    
    print(f"\n✅ Release {new_version} created successfully!")
    print("GitHub Actions will now:")
    print("  1. Build firmware with the updated version")
    print("  2. Create GitHub release with your custom release notes")
    print("  3. Include automatic changelog from commits")
    print("  4. Upload firmware binaries for users")
    print(f"\nCheck the progress at: https://github.com/jaapp/smart-grind-by-weight/actions")
    
    return True

if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--help":
        print("Usage: python3 tools/release.py")
        print("Interactive script to create tagged releases")
        sys.exit(0)
    
    try:
        create_release()
    except KeyboardInterrupt:
        print("\n\nRelease cancelled by user.")
        sys.exit(1)