#!/usr/bin/env bash
# ECHO OS — pre-commit / pre-push credential scan (constraint #3).
#
# Refuses the commit if a real API key, OAuth secret, token file, or `.env` is
# about to enter the repo. Run manually before every push, or wire it as a git
# pre-commit hook:
#
#   ln -sf ../../scripts/check_secrets.sh .git/hooks/pre-commit
#
# Exit 0 = clean, 1 = something suspicious is staged (nothing is committed).
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

# Files staged for commit (added/copied/modified). If nothing is staged, scan the
# whole working tree instead so a manual run is still useful.
staged="$(git diff --cached --name-only --diff-filter=ACM || true)"
if [ -z "$staged" ]; then
    files="$(git ls-files)"
else
    files="$staged"
fi

fail=0
note() { echo "  ✗ $1"; fail=1; }

# 1. Never allow a real .env or a token cache to be tracked.
while IFS= read -r f; do
    [ -z "$f" ] && continue
    case "$f" in
        .env|.env.*)
            [ "$f" = ".env.example" ] || note "tracked env file: $f (should be gitignored)";;
        .echo-tokens/*|*/.echo-tokens/*)
            note "tracked token cache: $f";;
    esac
done <<< "$files"

# 2. Content patterns that indicate a real secret slipped into a tracked file.
#    Skip the template and this script itself.
patterns=(
    'AIza[0-9A-Za-z_-]{35}'                 # Google API key
    'ya29\.[0-9A-Za-z_-]+'                  # Google OAuth access token
    '-----BEGIN [A-Z ]*PRIVATE KEY-----'    # private key
    'ECHO_[A-Z_]*SECRET=[^[:space:]]{10,}'   # a filled-in *_SECRET var (real length)
    'ECHO_[A-Z_]*KEY=[A-Za-z0-9_-]{16,}'     # a filled-in *_KEY var
    '"refresh_token"[[:space:]]*:[[:space:]]*"[^"]{20,}"'  # a real refresh token
)

while IFS= read -r f; do
    [ -z "$f" ] && continue
    [ -f "$f" ] || continue
    case "$f" in
        .env.example|scripts/check_secrets.sh|scripts/check_secrets.ps1) continue;;
    esac
    for p in "${patterns[@]}"; do
        if grep -Eaq "$p" "$f" 2>/dev/null; then
            note "possible secret in $f (matched /$p/)"
        fi
    done
done <<< "$files"

if [ "$fail" -ne 0 ]; then
    echo ""
    echo "✗ check_secrets: potential credentials detected — commit blocked."
    echo "  Move secrets into .env (gitignored). See .env.example."
    exit 1
fi
echo "✓ check_secrets: no credentials detected in tracked/staged files."
