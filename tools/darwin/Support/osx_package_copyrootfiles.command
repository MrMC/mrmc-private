#!/bin/bash

echo "copy root files"

if [ "$ACTION" == build ] || [ "$ACTION" == install ] ; then

function package_skin
{
  SYNC_CMD="${1}"
  SKIN_PATH="${2}"
  SKIN_NAME=$(basename "${SKIN_PATH}")

  echo "Packaging ${SKIN_NAME}"

  if [ -f "$SKIN_PATH/addon.xml" ]; then
    SYNCSKIN_CMD=${SYNC_CMD}
    if [ -f "$SKIN_PATH/media/Textures.xbt" ]; then
      SYNCSKIN_CMD="${SYNC_CMD} --exclude *.png --exclude *.jpg --exclude *.gif --exclude media/Makefile*"
    fi
    ${SYNCSKIN_CMD} "$SKIN_PATH" "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/addons"
    # these might have image files so just sync them
    # look for both background and backgrounds, skins do not seem to follow a dir naming convention
    if [ -d "$SKIN_PATH/background" ]; then
      ${SYNC_CMD} "$SKIN_PATH/background" "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/addons/$SKIN_NAME"
    fi
    if [ -d "$SKIN_PATH/backgrounds" ]; then
      ${SYNC_CMD} "$SKIN_PATH/backgrounds" "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/addons/$SKIN_NAME"
    fi
    if [ -f "$SKIN_PATH/icon.png" ]; then
      ${SYNC_CMD} "$SKIN_PATH/icon.png" "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/addons/$SKIN_NAME"
    fi
  fi
}

# for external testing
#TARGET_NAME=$APP_NAME.app
#SRCROOT=/Users/Shared/xbmc_svn/$APP_NAME
#TARGET_BUILD_DIR=/Users/Shared/xbmc_svn/$APP_NAME/build/Debug

PLATFORM="--exclude .DS_Store*"
BUILDSYS="--exclude *.a --exclude *.dll --exclude *.DLL --exclude *.zlib --exclude *linux.* --exclude *x86-osx.*"
BUILDSRC="--exclude CVS* --exclude .svn* --exclude .cvsignore* --exclude .cvspass* --exclude *.bat --exclude *README --exclude *README.txt"
BUILDDBG="--exclude *.dSYM --exclude *.bcsymbolmap"
# rsync command with exclusions for items we don't want in the app package
SYNC="rsync -aq ${PLATFORM} ${BUILDSYS} ${BUILDDBG}"

# rsync command for language pacs
LANGSYNC="rsync -aq ${PLATFORM} ${BUILDSRC} ${BUILDSYS} --exclude resource.uisounds*"

# rsync command for including everything but the skins
DEFAULTSKIN_EXCLUDES="--exclude addons/skin.nationwide --exclude addons/skin.mrmc --exclude addons/skin.re-touched --exclude addons/skin.amber --exclude addons/skin.pm3.hd --exclude addons/skin.sio2 --exclude addons/skin.nationwide"
ADDONSYNC="rsync -aq ${PLATFORM} ${BUILDSRC} ${BUILDDBG} ${DEFAULTSKIN_EXCLUDES} --exclude addons/lib --exclude addons/share  --exclude *changelog.* --exclude *library.*/*.h --exclude *library.*/*.cpp --exclude *xml.in --exclude *pvr.* --exclude *visualization.*"
echo "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME"

mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME"
mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/addons"
mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/media"
mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/system"
mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/userdata"
mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/media"
mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/tools/darwin/runtime"
mkdir -p "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/extras/user"

${SYNC} "$SRCROOT/LICENSE.GPL" 	"$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/"
${SYNC} "$SRCROOT/xbmc/platform/darwin/Credits.html" 	"$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/"
${SYNC} "$SRCROOT/tools/darwin/runtime"	"$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/tools/darwin"
${ADDONSYNC} "$SRCROOT/addons"		"$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME"
${SYNC} "$SRCROOT/media" 		"$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME"

${SYNC} "$SRCROOT/system" 		"$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME"
${SYNC} "$SRCROOT/userdata" 	"$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME"

# always sync nwmn skin
package_skin "${SYNC}" "$SRCROOT/addons/skin.nationwide"

# copy extra packages if applicable
if [ -d "$SRCROOT/extras/system" ]; then
	${SYNC} "$SRCROOT/extras/system/" "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME"
fi

# copy extra user packages if applicable
if [ -d "$SRCROOT/extras/user" ]; then
	${SYNC} "$SRCROOT/extras/user/" "$TARGET_BUILD_DIR/$TARGET_NAME/Contents/Resources/$APP_NAME/extras/user"
fi

# magic that gets the icon to update
touch "$TARGET_BUILD_DIR/$TARGET_NAME"

# not sure we want to do this with out major testing, many scripts cannot handle the spaces in the app name
#mv "$TARGET_BUILD_DIR/$TARGET_NAME" "$TARGET_BUILD_DIR/$APP_NAME Media Center.app"

fi
