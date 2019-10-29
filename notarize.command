#!/bin/bash

basedir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

# Assumes ~/.bash_profile is set correctly, e.g. 
#  CODE_SIGN_SIGNATURE="Developer ID Application: John Doe"
#  APPLE_ID_USER=doe@gmail.com
#  DCM2NIIX_SPECIFIC_PASSWORD=bbzj-zowb-bzji-ynkl
#  export APPLE_ID_USER DCM2NIIX_SPECIFIC_PASSWORD CODE_SIGN_SIGNATURE

CODE_SIGN_SIGNATURE=$CODE_SIGN_SIGNATURE
APPLE_ID_USER=$APPLE_ID_USER
APP_SPECIFIC_PASSWORD=$DCM2NIIX_SPECIFIC_PASSWORD

cd ${basedir}

mkdir build && cd build
cmake -DZLIB_IMPLEMENTATION=Cloudflare -DUSE_JPEGLS=ON -DUSE_OPENJPEG=ON ..
make

cd bin

# Clean up temporary files
rm -f dcm2niix_macOS.dmg
rm -f upload_log_file.txt
rm -f request_log_file.txt
rm -f log_file.txt

# https://stackoverflow.com/questions/2870992/automatic-exit-from-bash-shell-script-on-error
# terminate on error
set -e

echo "Verifying Info.plist"
launchctl plist dcm2niix

echo "Code signing dcm2niix..."
codesign -vvv --force --strict --options=runtime --timestamp -s "$CODE_SIGN_SIGNATURE" dcm2niix
codesign --verify --verbose --strict dcm2niix

echo "Creating disk image..."

hdiutil create -volname dcm2niix -srcfolder `pwd` -ov -format UDZO -layout SPUD -fs HFS+J  dcm2niix_macOS.dmg

# Notarizing with Apple...
echo "Uploading..."
xcrun altool --notarize-app -t osx --file dcm2niix_macOS.dmg --primary-bundle-id com.mricro.dcm2niix -u $APPLE_ID_USER -p $APP_SPECIFIC_PASSWORD --output-format xml > upload_log_file.txt

# WARNING: if there is a 'product-errors' key in upload_log_file.txt something went wrong
# we could parse it here and bail but not sure how to check for keys existing with PListBuddy
# /usr/libexec/PlistBuddy -c "Print :product-errors:0:message" upload_log_file.txt

# now we need to query apple's server to the status of notarization
# when the "xcrun altool --notarize-app" command is finished the output plist
# will contain a notarization-upload->RequestUUID key which we can use to check status
echo "Checking status..."
sleep 20
REQUEST_UUID=`/usr/libexec/PlistBuddy -c "Print :notarization-upload:RequestUUID" upload_log_file.txt`
while true; do
  xcrun altool --notarization-info $REQUEST_UUID -u $APPLE_ID_USER -p $APP_SPECIFIC_PASSWORD --output-format xml > request_log_file.txt
  # parse the request plist for the notarization-info->Status Code key which will
  # be set to "success" if the package was notarized
  STATUS=`/usr/libexec/PlistBuddy -c "Print :notarization-info:Status" request_log_file.txt`
  if [ "$STATUS" != "in progress" ]; then
    break
  fi
  # echo $STATUS
  echo "$STATUS"
  sleep 10
done

# download the log file to view any issues
/usr/bin/curl -o log_file.txt `/usr/libexec/PlistBuddy -c "Print :notarization-info:LogFileURL" request_log_file.txt`

# staple
echo "Stapling..."
xcrun stapler staple dcm2niix_macOS.dmg
xcrun stapler validate dcm2niix_macOS.dmg

open log_file.txt
