#!/bin/sh
# Creates a demo repository with branches, merges, tags, a stash, a remote and
# uncommitted changes. Usage: create-demo.sh [target-dir] [remote-dir]
set -e
TARGET=${1:-/boot/home/TurboChookDemo}
REMOTE=${2:-/boot/home/TurboChookRemote.git}
rm -rf "$TARGET" "$REMOTE"
export GIT_AUTHOR_NAME="Demo User" GIT_AUTHOR_EMAIL="demo@example.com"
export GIT_COMMITTER_NAME="Demo User" GIT_COMMITTER_EMAIL="demo@example.com"
git init -q -b main "$TARGET"
cd "$TARGET"
git config user.name "Demo User"; git config user.email "demo@example.com"
stamp=1735689600
commit() { stamp=$((stamp + 3600 * 7)); export GIT_AUTHOR_DATE="@$stamp +1000" GIT_COMMITTER_DATE="@$stamp +1000"; git commit -q -m "$1"; }
mkdir -p src docs
printf '# Turbo Chook demo\n\nA small project used to exercise the Git client.\n' > README.md
printf 'int main() {\n    return 0;\n}\n' > src/main.cpp
git add -A; commit "Initial project layout"
printf 'build/\n*.o\n' > .gitignore; git add -A; commit "Ignore build output"
for i in 1 2 3 4 5; do printf 'void helper%d() {}\n' $i >> src/helpers.cpp; git add -A; commit "Add helper $i"; done
printf '\n## Building\n\nRun make.\n' >> README.md; git add -A; commit "Document the build"
git tag -a v0.1 -m "First tagged release"
git checkout -q -b feature/parser
printf 'struct Parser {};\n' > src/parser.cpp; git add -A; commit "Start the parser"
printf 'struct Parser { int depth; };\n' > src/parser.cpp; git add -A; commit "Track nesting depth"
printf 'struct Parser { int depth; bool strict; };\n' > src/parser.cpp; git add -A; commit "Add strict mode"
git checkout -q main
printf 'void log(const char*) {}\n' > src/log.cpp; git add -A; commit "Add logging"
printf 'void log(const char*, int) {}\n' >> src/log.cpp; git add -A; commit "Log with levels"
git merge -q --no-ff feature/parser -m "Merge feature/parser into main"
git checkout -q -b feature/network
printf 'int connect() { return 1; }\n' > src/net.cpp; git add -A; commit "Add network stub"
printf 'int connect() { return 2; }\n' > src/net.cpp; git add -A; commit "Retry connections"
git checkout -q main
printf '# Changelog\n\n- logging\n- parser\n' > docs/CHANGELOG.md; git add -A; commit "Add changelog"
git tag v0.2
git checkout -q -b bugfix/crash
printf 'int main() {\n    // guard against null\n    return 0;\n}\n' > src/main.cpp; git add -A; commit "Guard against null input"
git checkout -q main
for i in 1 2 3; do printf -- '- item %d\n' $i >> docs/CHANGELOG.md; git add -A; commit "Changelog entry $i"; done
git merge -q --no-ff bugfix/crash -m "Merge bugfix/crash into main"
git branch -d -q bugfix/crash
printf 'void helper6() {}\n' >> src/helpers.cpp; git add -A; commit "Add helper 6"
printf 'void helper7() {}\n' >> src/helpers.cpp; git add -A; commit "Add helper 7"
# A bare remote that is one commit ahead on main and has the feature branch.
git init -q --bare "$REMOTE"
git remote add origin "$REMOTE"
git push -q origin main feature/network feature/parser --tags
git branch -q --set-upstream-to=origin/main main
printf 'void helper8() {}\n' >> src/helpers.cpp; git add -A; commit "Add helper 8"
git push -q origin main
git reset -q --hard HEAD~1
# Stash, staged, unstaged and untracked changes.
printf 'void temporary() {}\n' >> src/helpers.cpp; git stash push -q -m "WIP: temporary helper"
printf '\n## Testing\n\nRun make check.\n' >> README.md; git add README.md
printf 'int main() {\n    // guard against null\n    return 42;\n}\n' > src/main.cpp
printf 'notes\n' > TODO.txt
echo "Demo repository at $TARGET with remote $REMOTE"
git log --oneline --graph --all | head -40
