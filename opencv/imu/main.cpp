#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <wiringSerial.h>
#include <thread>
#include <RTIMULib.h>
#include "../common.hpp"
#include "../frame.hpp"
#include "main.hpp"

using namespace std;
using namespace cv;

void signal_callback_handler(int signum) {
 fprintf(stderr, "Caught signal %d\n", signum);
 run = false;
}

void imuThread() {
 int oldStdout = dup(fileno(stdout));
 dup2(fileno(stderr), fileno(stdout));

 RTIMUSettings *settings = new RTIMUSettings("RTIMULib");
 RTIMU *imu = RTIMU::createIMU(settings);
 if(imu == NULL || imu->IMUType() == RTIMU_TYPE_NULL) {
  fprintf(stderr, "No IMU found\n");
  return;
 }

 imu->IMUInit();
 imu->setSlerpPower(0.002);
 imu->setGyroEnable(true);
 imu->setAccelEnable(true);
 imu->setCompassEnable(false);

 dup2(oldStdout, fileno(stdout));

 while(run) {
  usleep(imu->IMUGetPollInterval() * 1000);
  while(imu->IMURead())
   imuData = imu->getIMUData();
 }
}

void watch(Mat &image, double angle, Point center, int diam, Scalar color1, Scalar color2) {
 double angleDeg = angle * 180.0 / M_PI;
 ellipse(image, center, Point(diam, diam), angleDeg, 0.0, 180.0, color1, FILLED, LINE_AA);
 ellipse(image, center, Point(diam, diam), angleDeg, 0.0, -180.0, color2, FILLED, LINE_AA);
}

void autopilot(Mat &image) {
 static int timeout = TIMEOUT;
 static int vz = 0;
 static int oldPz = 0;
 bool buttonLeft90 = remoteFrame.switchs & 0b00010000;
 bool buttonRight90 = remoteFrame.switchs & 0b00100000;
 bool buttonLeft45 = remoteFrame.switchs & 0b01000000;
 bool buttonRight45 = remoteFrame.switchs & 0b10000000;
 static bool oldButtonLeft90 = false;
 static bool oldButtonRight90 = false;
 static bool oldButtonLeft45 = false;
 static bool oldButtonRight45 = false;
 double x = imuData.fusionPose.x() * DIRX + OFFSETX;
 double y = imuData.fusionPose.y() * DIRY + OFFSETY;
 double z = imuData.fusionPose.z() * DIRZ + OFFSETZ;
 int zDeg = int(z * 180.0 / M_PI);

 if(remoteFrame.vx != 0 ||
    remoteFrame.vy != 0 ||
    remoteFrame.vz != 0 ||
    buttonLeft90 || buttonRight90 ||
    buttonLeft45 || buttonRight45) {
  timeout = TIMEOUT;
 }

 if(timeout > 0) {
  timeout--;

  if(buttonLeft90 && !oldButtonLeft90)
   vz += 90 * DIVVZ;
  if(buttonRight90 && !oldButtonRight90)
   vz -= 90 * DIVVZ;
  if(buttonLeft45 && !oldButtonLeft45)
   vz += 45 * DIVVZ;
  if(buttonRight45 && !oldButtonRight45)
   vz -= 45 * DIVVZ;
  oldButtonLeft90 = buttonLeft90;
  oldButtonRight90 = buttonRight90;
  oldButtonLeft45 = buttonLeft45;
  oldButtonRight45 = buttonRight45;

  vz += remoteFrame.vz;
  if(vz < -180 * DIVVZ)
   vz += 360 * DIVVZ;
  else if(vz >= 180 * DIVVZ)
   vz -= 360 * DIVVZ;

  int pz = vz / DIVVZ - zDeg;

  if(pz < -180)
   pz += 360;
  else if(pz >= 180)
   pz -= 360;

  int dz = pz - oldPz;
  oldPz = pz;
  int autovz = pz * KPVZ + dz * KDVZ;
  autovz = constrain(autovz, -127, 127);
  telemetryFrame.vz = autovz;

 } else {
  vz = zDeg * DIVVZ;
  oldPz = 0;
  telemetryFrame.vz = 0;
 }

 int x1 = MARGIN + DIAM1;
 int x2 = x1 + MARGIN + DIAM1 * 2;
 int x3 = width - MARGIN - DIAM1;
 int y1 = MARGIN + DIAM1;

 watch(image, x, Point(x1, y1), DIAM1, Scalar(0, 0, 200), Scalar::all(200));
 watch(image, y, Point(x2, y1), DIAM1, Scalar(0, 200, 0), Scalar::all(200));

 watch(image, x * COEF2, Point(x1, y1), DIAM2, Scalar(0, 0, 255), Scalar::all(255));
 watch(image, y * COEF2, Point(x2, y1), DIAM2, Scalar(0, 255, 0), Scalar::all(255));

 watch(image, -z, Point(x3, y1), DIAM1, Scalar(200, 0, 0), Scalar::all(200));
 watch(image, -double(vz / DIVVZ) / 180.0 * M_PI, Point(x3, y1), DIAM2, Scalar(255, 0, 0), Scalar::all(255));
}

int main(int argc, char* argv[]) {
 if(argc != 4) {
  width = WIDTH;
  height = HEIGHT;
  fps = FPS;
 } else {
  sscanf(argv[1], "%d", &width);
  sscanf(argv[2], "%d", &height);
  sscanf(argv[3], "%d", &fps);
 }

 int fd = serialOpen(DEVROBOT, DEVDEBIT);
 if(fd == -1) {
  fprintf(stderr, "Error opening serial port\n");
  return 1;
 }

 thread imuThr(imuThread);

 Mat image;
 int size = width * height * 3;

 telemetryFrame.header[0] = '$';
 telemetryFrame.header[1] = 'R';
 telemetryFrame.header[2] = ' ';
 telemetryFrame.header[3] = ' ';

 VideoCapture capture;
 capture.open(0);
 capture.set(CAP_PROP_FRAME_WIDTH, width);
 capture.set(CAP_PROP_FRAME_HEIGHT, height);
 capture.set(CAP_PROP_FPS, fps);
 capture.set(CAP_PROP_FORMAT, CV_8UC3);
 while(run) {
  capture.read(image);

  bool updated = readModem(fd, remoteFrame);

  autopilot(image);

  if(updated) {
   for(int i = 0; i < NBCOMMANDS; i++) {
    telemetryFrame.xy[i][0] = remoteFrame.xy[i][0];
    telemetryFrame.xy[i][1] = remoteFrame.xy[i][1];
   }
   telemetryFrame.z = remoteFrame.z;
   telemetryFrame.vx = remoteFrame.vx;
   telemetryFrame.vy = remoteFrame.vy;
   telemetryFrame.switchs = remoteFrame.switchs;

   writeModem(fd, telemetryFrame);
  }

  fwrite(image.data, size, 1, stdout);
 }

 return 0;
}
