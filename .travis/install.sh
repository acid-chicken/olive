#!/bin/bash

if [[ "$TRAVIS_OS_NAME" == "osx" ]]; then

  #export PATH="/usr/local/opt/qt/bin:/usr/local/opt/python@2/libexec/bin:$PATH"
  echo Attempting skip of Mac path export

elif [[ "$TRAVIS_OS_NAME" == "linux" ]]; then

  sudo apt-get -y -o Dpkg::Options::="--force-overwrite" install qt510base qt510multimedia qt510svg qttools5-dev libavformat-dev libavcodec-dev libavfilter-dev libavutil-dev libswscale-dev libswresample-dev cmake libopencolorio-dev libopenimageio-dev
  source /opt/qt*/bin/qt*-env.sh

fi
