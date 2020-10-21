#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <opencv2/opencv.hpp>
#include <opencv2/videoio.hpp>
#include <wiringSerial.h>
#include <thread>
#include <RTIMULib.h>
#include "../frame.hpp"
#include "main.hpp"

using namespace std;
using namespace cv;

void signal_callback_handler(int signum) {
 fprintf(stderr, "Caught signal %d\n", signum);
 run = false;
}

int mapInteger(int n, int inMin, int inMax, int outMin, int outMax) {
 return (n - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

float mapFloat(float n, float inMin, float inMax, float outMin, float outMax) {
 return (n - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

int constrain(int n, int min, int max) {
 if(n < min)
  n = min;
 else if(n > max)
  n = max;

 return n;
}

void ui(Mat &image, bool &updated) {
 static bool buttonLess = false;
 static bool oldButtonLess = false;
 static bool buttonMore = false;
 static bool oldButtonMore = false;
 static bool buttonOk = false;
 static bool oldButtonOk = false;

 if(updated) {
  buttonLess = remoteFrame.switchs & 0b00010000;
  buttonMore = remoteFrame.switchs & 0b00100000;
  buttonOk = remoteFrame.switchs & 0b10000000;
 }

 //

 oldButtonLess = buttonLess;
 oldButtonMore = buttonMore;
 oldButtonOk = buttonOk;
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

void watch(Mat &image, double angle, Point center, int diam, Scalar color) {
 double deg = angle * 180.0 / M_PI;
 ellipse(image, center, Point(diam, diam), deg, 0.0, -180.0, Scalar::all(255), FILLED, LINE_AA);
 ellipse(image, center, Point(diam, diam), deg, 0.0, 180.0, color, FILLED, LINE_AA);
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

  //ui(image, updated);

  double x = imuData.fusionPose.x() * DIRX + OFFSETX;
  double y = imuData.fusionPose.y() * DIRY + OFFSETY;
  double z = imuData.fusionPose.z() * DIRZ + OFFSETZ;

  int x1 = MARGIN + DIAM1;
  int x2 = x1 + MARGIN + DIAM1 * 2;
  int x3 = width - MARGIN - DIAM1;
  int y1 = MARGIN + DIAM1;

  watch(image, x, Point(x1, y1), DIAM1, Scalar(0, 0, 255));
  watch(image, y, Point(x2, y1), DIAM1, Scalar(0, 255, 0));
  watch(image, z, Point(x3, y1), DIAM1, Scalar(255, 0, 0));

  watch(image, x * COEF2, Point(x1, y1), DIAM2, Scalar(0, 0, 255));
  watch(image, y * COEF2, Point(x2, y1), DIAM2, Scalar(0, 255, 0));
  watch(image, z * COEF2, Point(x3, y1), DIAM2, Scalar(255, 0, 0));

  if(updated) {
   for(int i = 0; i < NBCOMMANDS; i++) {
    telemetryFrame.xy[i][0] = remoteFrame.xy[i][0];
    telemetryFrame.xy[i][1] = remoteFrame.xy[i][1];
   }
   telemetryFrame.z = remoteFrame.z;
   telemetryFrame.vx = remoteFrame.vx;
   telemetryFrame.vy = remoteFrame.vy;
   telemetryFrame.vz = remoteFrame.vz;
   telemetryFrame.switchs = remoteFrame.switchs;

   writeModem(fd, telemetryFrame);
  }

  fwrite(image.data, size, 1, stdout);
 }

 return 0;
}
