#!/bin/bash
# Script to update AstraDB version
# Usage: ./scripts/update_version.sh <major.minor.patch>

set -e

VERSION_FILE="${SCRIPT_DIR}/../VERSION"
CHANGELOG_FILE="${SCRIPT_DIR}/../CHANGELOG.md"

if [ -z "$1" ]; then
    echo "Usage: $0 <major.minor.patch>"
    echo "Example: $0 1.2.0"
    exit 1
fi

VERSION=$1

# Validate version format (major.minor.patch)
if [[ ! $VERSION =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "Error: Invalid version format. Use major.minor.patch (e.g., 1.2.0)"
    exit 1
fi

# Update VERSION file
echo "$VERSION" > "$VERSION_FILE"
echo "Updated VERSION to $VERSION"

# Add entry to CHANGELOG.md (if exists)
if [ -f "$CHANGELOG_FILE" ]; then
    DATE=$(date +%Y-%m-%d)
    
    # Create new changelog entry
    NEW_ENTRY="## [$VERSION] - $DATE

### Added
- 

### Changed
- 

### Fixed
- 

### Removed
- 

"

    # Insert after the first line (## Changelog)
    if grep -q "^## Changelog" "$CHANGELOG_FILE"; then
        sed -i "/^## Changelog/a\\$NEW_ENTRY" "$CHANGELOG_FILE"
        echo "Added entry to CHANGELOG.md"
    else
        # Add Changelog header if it doesn't exist
        echo "## Changelog
$NEW_ENTRY" > "$CHANGELOG_FILE"
        echo "Created CHANGELOG.md with initial entry"
    fi
else
    echo "CHANGELOG.md not found, skipping changelog update"
fi

echo "Version update complete: $VERSION"
echo "Remember to:"
echo "  1. Review and update CHANGELOG.md with actual changes"
echo "  2. Commit: git add VERSION CHANGELOG.md && git commit -m 'chore: Bump version to $VERSION'"
echo "  3. Tag: git tag -a v$VERSION -m 'Release v$VERSION'"
echo "  4. Push: git push origin develop && git push origin v$VERSION"