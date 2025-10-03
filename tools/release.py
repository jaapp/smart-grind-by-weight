#!/usr/bin/env python3
"""
Release helper script for smart-grind-by-weight
Creates tags and triggers automated releases
"""

import subprocess
import sys
import re
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
    
    # Strip RC/beta/alpha suffixes to get base version
    if '-' in version:
        version = version.split('-')[0]
    
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

def get_next_rc_version(base_version):
    """Get the next release candidate version by checking existing tags"""
    # Remove 'v' prefix if present
    base_version = base_version.lstrip('v')
    
    # Get all existing tags
    all_tags = run_command("git tag -l")
    if not all_tags:
        return f"v{base_version}-rc.1"
    
    existing_tags = all_tags.split('\n')
    
    # Find existing RC tags for this base version
    rc_numbers = []
    for tag in existing_tags:
        tag = tag.strip()
        if tag.startswith(f"v{base_version}-rc."):
            try:
                rc_part = tag.split(f"v{base_version}-rc.")[-1]
                rc_numbers.append(int(rc_part))
            except ValueError:
                continue
    
    # Get the next RC number
    if rc_numbers:
        next_rc = max(rc_numbers) + 1
    else:
        next_rc = 1
    
    return f"v{base_version}-rc.{next_rc}"

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

def parse_version(version_str):
    """Parse version string into (major, minor, patch) tuple"""
    version = version_str.lstrip('v')
    # Strip RC/beta/alpha suffixes
    if '-' in version:
        version = version.split('-')[0]
    try:
        parts = version.split('.')
        return (int(parts[0]), int(parts[1]), int(parts[2]))
    except (ValueError, IndexError):
        return None

def clean_old_rc_tags():
    """Remove RC tags for old semantic versions, keeping current semver tags"""
    print("=== Clean Old RC Tags ===\n")

    # Get all tags
    all_tags = run_command("git tag -l")
    if not all_tags:
        print("No tags found.")
        return True

    tags = all_tags.split('\n')

    # Parse all versions and find the latest
    versions = []
    for tag in tags:
        tag = tag.strip()
        if not tag:
            continue
        parsed = parse_version(tag)
        if parsed:
            versions.append((parsed, tag))

    if not versions:
        print("No valid version tags found.")
        return True

    # Get the current (latest) semver
    current_semver = max(v[0] for v in versions)
    print(f"Current semver: v{current_semver[0]}.{current_semver[1]}.{current_semver[2]}")

    # Find all RC tags for old semvers
    tags_to_delete = []
    for (major, minor, patch), tag in versions:
        # Only delete RC tags from old semvers
        if '-rc.' in tag and (major, minor, patch) < current_semver:
            tags_to_delete.append(tag)

    if not tags_to_delete:
        print("\nNo old RC tags to clean. All RC tags are for the current semver.")
        return True

    # Group by semver for display
    by_semver = {}
    for tag in tags_to_delete:
        parsed = parse_version(tag)
        semver_key = f"v{parsed[0]}.{parsed[1]}.{parsed[2]}"
        if semver_key not in by_semver:
            by_semver[semver_key] = []
        by_semver[semver_key].append(tag)

    # Show confirmation screen
    print("\n" + "="*60)
    print("TAGS TO BE DELETED (LOCAL AND REMOTE)")
    print("="*60)
    for semver_key in sorted(by_semver.keys()):
        print(f"\n{semver_key}:")
        for tag in sorted(by_semver[semver_key]):
            print(f"  • {tag}")

    print("\n" + "="*60)
    print(f"Total: {len(tags_to_delete)} RC tags from old semvers")
    print(f"Keeping: All tags for current semver v{current_semver[0]}.{current_semver[1]}.{current_semver[2]}")
    print("="*60)

    response = input("\n⚠️  Proceed with deletion (local AND remote)? (y/N): ")
    if response.lower() != 'y':
        print("Cleanup cancelled.")
        return False

    # Delete tags locally and remotely
    print("\nDeleting tags...")
    failed_tags = []
    for tag in sorted(tags_to_delete):
        print(f"  {tag}...", end=" ")

        # Delete local tag
        result = run_command(f'git tag -d {tag}')
        if result is None:
            print("❌ Failed (local)")
            failed_tags.append(tag)
            continue

        # Delete remote tag
        result = run_command(f'git push origin :refs/tags/{tag}')
        if result is None:
            print("⚠️  Deleted locally, failed remotely")
            failed_tags.append(tag)
            continue

        print("✅")

    if failed_tags:
        print(f"\n⚠️  Failed to fully delete {len(failed_tags)} tags:")
        for tag in failed_tags:
            print(f"  - {tag}")
        return False

    print(f"\n✅ Successfully deleted {len(tags_to_delete)} old RC tags (local and remote)!")
    return True

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
        print("⚠️  Warning: Creating a release without new commits.")
        response = input("Continue anyway? (y/N): ")
        if response.lower() != 'y':
            print("Release cancelled.")
            return False

    print("\nWhat would you like to do?")
    print("1. Patch (bug fixes): v1.0.0 -> v1.0.1")
    print("2. Minor (new features): v1.0.0 -> v1.1.0")
    print("3. Major (breaking changes): v1.0.0 -> v2.0.0")
    print("4. Release Candidate (testing): auto-increments RC number")
    print("5. Custom version")
    print("6. Clean old RCs (remove RC tags from previous semvers)")

    choice = input("\nEnter choice (1-6): ").strip()

    if choice == '6':
        return clean_old_rc_tags()
    
    if choice == '1':
        new_version = increment_version(current_version, 'patch')
    elif choice == '2':
        new_version = increment_version(current_version, 'minor')
    elif choice == '3':
        new_version = increment_version(current_version, 'major')
    elif choice == '4':
        # Release candidate - check if current version is already an RC
        current_clean = current_version.lstrip('v')
        if '-rc.' in current_clean:
            # Current version is already an RC, just increment it
            base_version = current_clean.split('-rc.')[0]
            new_version = get_next_rc_version(base_version)
            print(f"Current version is already an RC. Next RC version: {new_version}")
        else:
            # Ask for base version
            print("\nRelease Candidate Options:")
            print("1. RC for patch version")
            print("2. RC for minor version") 
            print("3. RC for major version")
            print("4. RC for custom version")
            
            rc_choice = input("Enter choice (1-4): ").strip()
            
            if rc_choice == '1':
                base_version = increment_version(current_version, 'patch')
            elif rc_choice == '2':
                base_version = increment_version(current_version, 'minor')
            elif rc_choice == '3':
                base_version = increment_version(current_version, 'major')
            elif rc_choice == '4':
                base_version = input("Enter base version for RC (e.g., 1.2.3): ").strip()
                if base_version.startswith('v'):
                    base_version = base_version[1:]
            else:
                print("Invalid choice.")
                return False
            
            new_version = get_next_rc_version(base_version)
            print(f"Next RC version: {new_version}")
        
    elif choice == '5':
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
    
    # No manual release notes - GitHub will handle this
    final_release_notes = f"Release {new_version}"
    
    # Show preview
    print(f"\n--- Release Preview ---")
    print(f"Version: {new_version}")
    print("Release Notes: GitHub will generate automatically")
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
    
    commit_message = f"chore: Creating release {new_version}"
    if not run_command(f'git commit -m "{commit_message}"', capture_output=False):
        print("Failed to commit version update.")
        return False
    
    # Create tag with simple release message
    tag_message = f"Release {new_version}"
    
    print(f"Creating annotated tag {new_version}...")
    # Use subprocess directly for complex multiline messages
    try:
        result = subprocess.run(['git', 'tag', '-a', new_version, '-m', tag_message], 
                              capture_output=False, text=True)
        if result.returncode != 0:
            print("Failed to create tag.")
            return False
    except Exception as e:
        print(f"Failed to create tag: {e}")
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
    if len(sys.argv) > 1:
        if sys.argv[1] == "--help":
            print("Usage: python3 tools/release.py [command]")
            print("\nCommands:")
            print("  (none)           Interactive script to create tagged releases")
            print("  clean-old-rcs    Remove old RC tags for previous semvers")
            print("  --help           Show this help message")
            sys.exit(0)
        elif sys.argv[1] == "clean-old-rcs":
            try:
                success = clean_old_rc_tags()
                sys.exit(0 if success else 1)
            except KeyboardInterrupt:
                print("\n\nCleanup cancelled by user.")
                sys.exit(1)
        else:
            print(f"Unknown command: {sys.argv[1]}")
            print("Run 'python3 tools/release.py --help' for usage")
            sys.exit(1)

    try:
        create_release()
    except KeyboardInterrupt:
        print("\n\nRelease cancelled by user.")
        sys.exit(1)