#!/bin/bash
# Install git hooks from scripts/githooks to .git/hooks

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HOOKS_DIR="$PROJECT_ROOT/scripts/githooks"
GIT_HOOKS_DIR="$PROJECT_ROOT/.git/hooks"

if [ ! -d "$HOOKS_DIR" ]; then
    echo "Error: githooks directory not found at $HOOKS_DIR"
    exit 1
fi

echo "Installing git hooks from $HOOKS_DIR to $GIT_HOOKS_DIR..."

# Copy all hooks
for hook in "$HOOKS_DIR"/*; do
    if [ -f "$hook" ]; then
        hook_name=$(basename "$hook")
        echo "  Installing $hook_name..."
        cp "$hook" "$GIT_HOOKS_DIR/$hook_name"
        chmod +x "$GIT_HOOKS_DIR/$hook_name"
    fi
done

echo "Git hooks installed successfully!"
echo ""
echo "Installed hooks:"
ls -la "$GIT_HOOKS_DIR" | grep -v "sample" | tail -n +2