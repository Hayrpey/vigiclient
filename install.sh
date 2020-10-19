#!/bin/bash

set -e
set -u

BASEURL=https://www.vigibot.com/vigiclient
BASEDIR=/usr/local/vigiclient

if [ $EUID -ne 0 ]
then
 echo "This script must be run as root" 1>&2
 exit 1
fi

echo "Enable I2C"
sed -i "s/#dtparam=i2c_arm=on/dtparam=i2c_arm=on,i2c_arm_baudrate=400000/" /boot/config.txt
fgrep i2c-dev /etc/modules || echo i2c-dev >> /etc/modules

echo "Enable camera"
fgrep start_x=1 /boot/config.txt || echo start_x=1 >> /boot/config.txt
fgrep gpu_mem=128 /boot/config.txt || echo gpu_mem=128 >> /boot/config.txt

echo "Enable Video4Linux"
fgrep bcm2835-v4l2 /etc/modules || echo bcm2835-v4l2 >> /etc/modules

echo "Enable /dev/fb0"
sed -i "s/#hdmi_force_hotplug=1/hdmi_force_hotplug=1/" /boot/config.txt

echo "Disable Bluetooth"
fgrep dtoverlay=pi3-disable-bt /boot/config.txt || echo dtoverlay=pi3-disable-bt >> /boot/config.txt
systemctl disable hciuart || true

echo "Disable serial console"
sed -i "s/console=serial0,115200 //" /boot/cmdline.txt

apt update

echo "Node.js, pigpio, FFmpeg and eSpeak installation"
apt install -y npm pigpio ffmpeg espeak

echo "OpenCV, WiringPI, Socat and RTIMULib installation"
apt install -y libopencv-dev wiringpi socat librtimulib-dev

echo "Cleaning"
rm -rf $BASEDIR
mkdir -p $BASEDIR
cd $BASEDIR

echo "Adding symbolic links for video and audio processes"
ln -s /bin/cat processdiffusion
ln -s $(which ffmpeg || echo ffmpegnotfound) processdiffvideo
ln -s $(which ffmpeg || echo ffmpegnotfound) processdiffaudio

echo "Updater installation"
wget $BASEURL/vigiupdate.sh
chmod +x vigiupdate.sh
wget $BASEURL/vigicron -P /etc/cron.d -N

echo "Adding the default config file"
wget $BASEURL/robot.json -P /boot -N

echo "Systemd unit files installation"
wget $BASEURL/vigiclient.service -P /etc/systemd/system -N
wget $BASEURL/socat.service -P /etc/systemd/system -N
systemctl enable vigiclient
systemctl enable socat
