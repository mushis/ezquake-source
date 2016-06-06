#!/bin/bash

# ezQuake build script, uploads output to gh-pages
# based on guide/script from https://gist.github.com/domenic/ec8b0fc8ab45f39403dd

set -e # Exit with nonzero exit code if anything fails

SOURCE_BRANCH="ezquake_30_build"
TARGET_BRANCH="gh-pages"

# Pull requests and commits to other branches shouldn't try to deploy, just build to verify
if [ "$TRAVIS_PULL_REQUEST" != "false" -o "$TRAVIS_BRANCH" != "$SOURCE_BRANCH" ]; then
    echo "Skipping deploy; just doing a build."
    make
    exit 0
fi

# travis osx image doesn't have any hosts included, script needs github.com to upload
if [ ! -e ~/.ssh/known_hosts ]; then
    echo "*** Missing known_hosts file: configure travis.yml (https://docs.travis-ci.com/user/ssh-known-hosts/)"
    exit 1
fi

# Save some useful information
REPO=`git config remote.origin.url`
SSH_REPO=${REPO/https:\/\/github.com\//git@github.com:}
SHA=`git rev-parse --verify HEAD`

# ezQuake uses number of commits for versioning: travis uses --depth 50, so we need to get the rest
git fetch --unshallow

# use this for description on commit to gh-pages
COMMITREF=`./version.sh -v`
SYSTEM="unknown"

# use this to create an include for gh-pages to update download links
BUILDEXPLANATION=`date +"%a %d %b %Y, %T ($COMMITREF)"`

# Clone the existing gh-pages for this repo into out/
# Create a new empty branch if gh-pages doesn't exist yet (should only happen on first deply)
git clone $REPO out
cd out
git checkout $TARGET_BRANCH || git checkout --orphan $TARGET_BRANCH
cd ..

# TODO: some kind of cvar/cmd sync here?  .json to .md, or vice-versa?

# Run our compile script
if [ "$TRAVIS_OS_NAME" = "osx" ]; then
    make strip
    echo "Creating OSX archive..."
    sh misc/install/create_osx_bundle.sh
    mv ezquake.zip osx.zip
    ARCHIVEFILE=osx.zip
    SYSTEM=osx
elif [ "$EZ_CONFIG_FILE" == ".config_windows" ]; then
    make strip
    echo "Creating Windows archive..."
    zip windows.zip ezquake.exe LICENSE
    ARCHIVEFILE=windows.zip
    SYSTEM=windows
else
    make strip
    echo "Creating Linux archive..."
    tar -cvzf linux.tar.gz ezquake-*-* LICENSE
    ARCHIVEFILE=linux.tar.gz
    SYSTEM=linux
fi

mv $ARCHIVEFILE out/media/latest/
cd out
git add -N media/latest/$ARCHIVEFILE

echo $BUILDEXPLANATION > _includes/build_latest_$SYSTEM.html
git add -N _includes/build_latest_$SYSTEM.html

# Now let's go have some fun with the cloned repo
git config user.name "Travis CI"
git config user.email "$COMMIT_AUTHOR_EMAIL"

# If there are no changes to the compiled out (e.g. this is a README update) then just bail.
if [ ! "`git diff --exit-code`" ]; then
    echo "No changes to the output on this push; exiting."
    exit 0
fi

# Commit the "changes", i.e. the new version.
# The delta will show diffs between new and old versions.
echo "Changes detected, updating " $TARGET_BRANCH
git add .
git commit -m "Deploy to GitHub Pages: ${COMMITREF}, $SYSTEM build"

# Get the deploy key by using Travis's stored variables to decrypt deploy_key.enc
ENCRYPTED_KEY_VAR="encrypted_${ENCRYPTION_LABEL}_key"
ENCRYPTED_IV_VAR="encrypted_${ENCRYPTION_LABEL}_iv"
ENCRYPTED_KEY=${!ENCRYPTED_KEY_VAR}
ENCRYPTED_IV=${!ENCRYPTED_IV_VAR}
openssl aes-256-cbc -K $ENCRYPTED_KEY -iv $ENCRYPTED_IV -in ../misc/travis-build/deploy_key.enc -out ./deploy_key -d
chmod 600 ./deploy_key
eval `ssh-agent -s`
ssh-add ./deploy_key

# Now that we're all set up, we can push.
git push $SSH_REPO $TARGET_BRANCH
