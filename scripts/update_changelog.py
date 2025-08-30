import os
import re
import subprocess
from datetime import datetime

def get_version():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    platformio_path = os.path.join(project_dir, 'platformio.ini')
    
    with open(platformio_path, 'r') as f:
        content = f.read()
        version_match = re.search(r'version\s*=\s*"([^"]+)"', content)
        return version_match.group(1) if version_match else None

def get_last_tag():
    """Get the last non-beta tag for changelog generation"""
    try:
        # Get all tags sorted by version
        result = subprocess.run(['git', 'tag', '-l', '--sort=-version:refname'], 
                              capture_output=True, text=True)
        if result.returncode != 0:
            return None
            
        tags = result.stdout.strip().split('\n')
        
        # Find the first (newest) non-beta tag
        for tag in tags:
            if tag and not '-beta' in tag.lower():
                print(f"Using last stable tag for changelog: {tag}")
                return tag
        
        # Fallback: if no non-beta tags found, use the newest tag
        print("No stable tags found, using newest tag")
        if tags and tags[0]:
            return tags[0]
        return None
    except subprocess.CalledProcessError:
        return None

def categorize_commit(commit_msg):
    """Categorize commit messages based on conventional commits"""
    lower_msg = commit_msg.lower()
    if any(x in lower_msg for x in ['feat', 'add', 'new']):
        return 'Added'
    elif any(x in lower_msg for x in ['fix', 'bug']):
        return 'Fixed'
    else:
        return 'Changed'

def get_changes_from_git():
    """Get changes from git commits since last tag"""
    changes = {
        'Added': [],
        'Changed': [],
        'Fixed': []
    }
    
    last_tag = get_last_tag()
    
    # Get commits since last tag
    git_log_command = ['git', 'log', '--pretty=format:%s']
    if last_tag:
        git_log_command.append(f'{last_tag}..HEAD')
    
    try:
        result = subprocess.run(git_log_command, capture_output=True, text=True)
        commits = result.stdout.strip().split('\n')
        
        # Filter out empty commits and categorize
        for commit in commits:
            if commit:
                category = categorize_commit(commit)
                # Clean up commit message
                clean_msg = re.sub(r'^(feat|fix|chore|docs|style|refactor|perf|test)(\(.*\))?:', '', commit).strip()
                changes[category].append(clean_msg)
                
    except subprocess.CalledProcessError:
        print("Error: Failed to get git commits")
        return None
    
    return changes

def update_changelog():
    print("Starting changelog update...")
    version = get_version()
    print(f"Current version: {version}")
    today = datetime.now().strftime('%Y-%m-%d')
    
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    changelog_path = os.path.join(project_dir, 'CHANGELOG.md')
    
    # Get changes from git
    changes = get_changes_from_git()
    if not changes:
        print("No changes found or error occurred")
        return
    
    # Create changelog entry
    changelog_entry = f"## [{version}] - {today}\n"
    for section, entries in changes.items():
        if entries:  # Only add sections that have entries
            changelog_entry += f"### {section}\n"
            for entry in entries:
                changelog_entry += f"- {entry}\n"
            changelog_entry += "\n"
    
    if not os.path.exists(changelog_path):
        with open(changelog_path, 'w') as f:
            f.write(f"# Changelog\n\n{changelog_entry}")
        print(f"Created new changelog file with version {version}")
    else:
        with open(changelog_path, 'r') as f:
            content = f.read()
        
        if f"[{version}]" not in content:
            updated_content = content.replace("# Changelog\n", f"# Changelog\n\n{changelog_entry}")
            with open(changelog_path, 'w') as f:
                f.write(updated_content)
            print(f"Added new version {version} to changelog")
        else:
            # Version existiert bereits, aktualisiere die bestehenden Einträge
            version_pattern = f"## \\[{version}\\] - \\d{{4}}-\\d{{2}}-\\d{{2}}"
            next_version_pattern = "## \\[.*?\\] - \\d{4}-\\d{2}-\\d{2}"
            
            # Finde den Start der aktuellen Version
            version_match = re.search(version_pattern, content)
            if version_match:
                version_start = version_match.start()
                # Suche nach der nächsten Version
                next_version_match = re.search(next_version_pattern, content[version_start + 1:])
                
                if next_version_match:
                    # Ersetze den Inhalt zwischen aktueller und nächster Version
                    next_version_pos = version_start + 1 + next_version_match.start()
                    updated_content = content[:version_start] + changelog_entry + content[next_version_pos:]
                else:
                    # Wenn keine nächste Version existiert, ersetze bis zum Ende
                    updated_content = content[:version_start] + changelog_entry + "\n"
                
                with open(changelog_path, 'w') as f:
                    f.write(updated_content)
                print(f"Updated entries for version {version}")

if __name__ == "__main__":
    update_changelog()