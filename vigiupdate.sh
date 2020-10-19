#!/bin/bash

set -e
set -u

BASEURL=https://www.vigibot.com/vigiclient
BASEDIR=/usr/local/vigiclient

updated=no

trace() {
 echo "$(date "+%d/%m/%Y %H:%M:%S") $1"
}

abnormal() {
 trace "Abnormal script termination"
}

check() {
 before=$(date -r $1/$2 +%s 2> /dev/null || echo 0)
 wget $BASEURL/$2 -P $1 -N -T 20 -t 3 > /dev/null 2>&1 || trace "$1/$2 wget error"
 after=$(date -r $1/$2 +%s)

 if [ $after -gt $before ]
 then
  trace "$1/$2 is updated"
  updated=yes
 else
  trace "$1/$2 is not changed"
 fi
}

trap abnormal EXIT

check $BASEDIR vigiupdate.sh
check /etc/cron.d vigicron

if [ $updated == "yes" ]
then
 trace "Purging updater log"
 rm -f /var/log/vigiupdate.log
 trace "Exiting"
 trap - EXIT
 exit 0
fi

check /etc/systemd/system vigiclient.service
check /etc/systemd/system socat.service

if [ $updated == "yes" ]
then
 trace "Rebooting"
 trap - EXIT
 sudo reboot
fi

if pidof -x $0 -o $$ > /dev/null
then
 trace "Only one instance is allowed from here"
 trap - EXIT
 exit 1
fi

timedatectl status | fgrep "synchronized: yes" > /dev/null || {
 trace "System clock must be synchronized from here"
 trap - EXIT
 exit 1
}

check $BASEDIR node_modules.tar.gz
check $BASEDIR package.json

if [ $updated == "yes" ]
then
 trace "Purging node_modules directory"
 cd $BASEDIR
 rm -rf node_modules
 trace "Extracting node_modules.tar.gz"
 tar xf node_modules.tar.gz
fi

check $BASEDIR clientrobotpi.js
check $BASEDIR sys.json
check $BASEDIR trame.js

if [ $updated == "yes" ]
then
 trace "Purging vigiclient.log"
 rm -f /var/log/vigiclient.log
 trace "Restarting vigiclient service"
 systemctl restart vigiclient
 trap - EXIT
 exit 0
fi

check $BASEDIR opencv.tar.gz
check $BASEDIR frame.hpp

if [ $updated == "yes" ]
then
 cd $BASEDIR
 trace "Extracting opencv.tar.gz"
 tar -x --keep-newer-files -f opencv.tar.gz
 trace "Purging opencv binaries"
 cd opencv
 find -name bin -delete
 trace "Compiling opencv binaries"
 ./make.sh
fi

trap - EXIT
