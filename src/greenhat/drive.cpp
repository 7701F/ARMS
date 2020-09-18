#include "greenhat/drive.h"
#include "api.h"
#include "greenhat/config.h"
using namespace pros;

namespace greenhat {
//imu
Imu imu(9);

// drive motors
std::shared_ptr<okapi::MotorGroup> leftMotors;
std::shared_ptr<okapi::MotorGroup> rightMotors;

// distance constants
int distance_constant;  // ticks per foot
double degree_constant; // ticks per degree

// slew control (autonomous only)
int accel_step;  // smaller number = more slew
int deccel_step; // 200 = no slew
int arc_step;    // acceleration for arcs

// pid constants
double driveKP;
double driveKD;
double turnKP;
double turnKD;
double arcKP;

/**************************************************/
// edit below with caution!!!
static int driveMode = 0;
static int driveTarget = 0;
static int turnTarget = 0;
static int maxSpeed = 100;

/**************************************************/
// basic control
void left_drive(int vel) {
	vel *= 120;
	leftMotors->moveVoltage(vel);
}

void right_drive(int vel) {
	vel *= 120;
	rightMotors->moveVoltage(vel);
}

void left_drive_vel(int vel) {
	vel *= (double)leftMotors->getGearing() / 100;
	leftMotors->moveVelocity(vel);
}

void right_drive_vel(int vel) {
	vel *= (double)leftMotors->getGearing() / 100;
	rightMotors->moveVelocity(vel);
}

void setBrakeMode(okapi::AbstractMotor::brakeMode b) {
	leftMotors->setBrakeMode(b);
	rightMotors->setBrakeMode(b);
	left_drive_vel(0);
	right_drive_vel(0);
}

void reset() {
	leftMotors->tarePosition();
	rightMotors->tarePosition();
}

int drivePos() {
	return (rightMotors->getPosition() + leftMotors->getPosition()) / 2;
}

/**************************************************/
// slew control
static int lastSpeed = 0;
int slew(int speed) {
	int step;

	if (abs(lastSpeed) < abs(speed))
		if (driveMode == 0)
			step = arc_step;
		else
			step = accel_step;
	else
		step = deccel_step;

	if (speed > lastSpeed + step)
		lastSpeed += step;
	else if (speed < lastSpeed - step)
		lastSpeed -= step;
	else {
		lastSpeed = speed;
	}

	return lastSpeed;
}

/**************************************************/
// drive settling
bool isDriving() {
	static int count = 0;
	static int last = 0;
	static int lastTarget = 0;

	int curr = drivePos();

	int target = turnTarget;
	if (driveMode == 1)
		target = driveTarget;

	if (abs(last - curr) < 3)
		count++;
	else
		count = 0;

	if (target != lastTarget)
		count = 0;

	lastTarget = target;
	last = curr;

	// not driving if we haven't moved
	if (count > 4)
		return false;
	else
		return true;
}

void waitUntilSettled() {
	while (isDriving())
		delay(10);
}

/**************************************************/
// autonomous functions
void driveAsync(double sp, int max) {
	sp *= distance_constant;
	reset();
	maxSpeed = max;
	driveTarget = sp;
	driveMode = 1;
}

void turnAsync(double sp, int max) {
	sp *= degree_constant;
	reset();
	maxSpeed = max;
	turnTarget = sp;
	driveMode = -1;
}

void drive(double sp, int max) {
	driveAsync(sp, max);
	delay(450);
	waitUntilSettled();
}

void turn(double sp, int max) {
	turnAsync(sp, max);
	delay(450);
	waitUntilSettled();
}

void fastDrive(double sp, int max) {
	if (sp < 0)
		max = -max;
	reset();
	lastSpeed = max;
	driveMode = 0;
	left_drive(max);
	right_drive(max);

	if (sp > 0)
		while (drivePos() < sp * distance_constant)
			delay(20);
	else
		while (drivePos() > sp * distance_constant)
			delay(20);
}

void timeDrive(int t, int left, int right) {
	left_drive(left);
	right_drive(right == 0 ? left : right);
	delay(t);
}

void velocityDrive(int t, int max) {
	left_drive_vel(max);
	right_drive_vel(max);
	delay(t);
}

void arc(bool mirror, int arc_length, double rad, int max, int type) {
	reset();
	int time_step = 0;
	driveMode = 0;
	bool reversed = false;

	// reverse the movement if the length is negative
	if (arc_length < 0) {
		reversed = true;
		arc_length = -arc_length;
	}

	// fix jerk bug between velocity movements
	if (type < 2) {
		left_drive_vel(0);
		right_drive_vel(0);
		delay(10);
	}

	while (time_step < arc_length) {

		// speed
		int error = arc_length - time_step;
		int speed = error * arcKP;

		if (type == 1 || type == 2)
			speed = max;

		// speed limiting
		if (speed > max)
			speed = max;
		if (speed < -max)
			speed = -max;

		// prevent backtracking
		if (speed < 0)
			speed = 0;

		speed = slew(speed); // slew

		if (reversed)
			speed = -speed;

		double scaled_speed = speed * rad;

		if (type == 1)
			scaled_speed *= (double)time_step / arc_length;
		else if (type == 2)
			scaled_speed *= std::abs(2*(.5-(double)time_step/arc_length));
		else if(type == 3)
			scaled_speed *= (1 - (double)time_step / arc_length);


		// assign drive motor speeds
		left_drive_vel(mirror ? speed : scaled_speed);
		right_drive_vel(mirror ? scaled_speed : speed);

		// increment time step
		time_step += 10;
		delay(10);
	}

	if (type != 1 && type != 2) {
		left_drive_vel(0);
		right_drive_vel(0);
	}
}

void arcLeft(int arc_length, double rad, int max, int type) {
	arc(false, arc_length, rad, max, type);
}

void arcRight(int arc_length, double rad, int max, int type) {
	arc(true, arc_length, rad, max, type);
}

void scurve(bool mirror, int arc1, int mid, int arc2, int max) {

	// first arc
	arc(mirror, arc1, 1, max, 1);

	// middle movement
	velocityDrive(mid, max);

	// final arc
	arc(!mirror, arc2, 1, max, 2);
}

void sLeft(int arc1, int mid, int arc2, int max) {
	scurve(false, arc1, mid, arc2, max);
}

void sRight(int arc1, int mid, int arc2, int max) {
	scurve(true, arc1, mid, arc2, max);
}

void _sLeft(int arc1, int mid, int arc2, int max) {
	scurve(true, -arc1, mid, -arc2, -max);
}

void _sRight(int arc1, int mid, int arc2, int max) {
	scurve(false, -arc1, -mid, -arc2, max);
}

/**************************************************/
// task control
int odomTask() {
	double global_x = 0;
	double global_y = 0;
	double global_orientation = M_PI/2;
	double orientation_degrees;

	double prev_left_pos = 0;
	double prev_right_pos = 0;

	double right_arc = 0;
	double left_arc = 0;
	double center_arc = 0;
	double delta_angle = 0;
	double current_radius = 0;
	double center_displacement = 0;
	double delta_x = 0;
	double delta_y = 0;

	while(true){
		right_arc = rightMotors->getPosition() - prev_right_pos;
		left_arc = leftMotors->getPosition() - prev_left_pos;
		center_arc = (right_arc + left_arc) / 2.0;

		delta_angle = ((imu.get_rotation() * -1.0 * (M_PI/180.0)) + M_PI/2) - global_orientation;
		global_orientation += delta_angle;

		delta_x = cos(global_orientation) * center_arc;
		delta_y = sin(global_orientation) * center_arc;
		

		prev_right_pos += right_arc;
		prev_left_pos += left_arc;

		global_x += delta_x;
		global_y += delta_y;
		
		orientation_degrees = (global_orientation * 180) / M_PI;

		printf( "%f, %f, %f \n" , global_x, global_y, global_orientation);
		
		delay(10);
	}
}
int driveTask() {
	int prevError = 0;
	double kp;
	double kd;
	int sp;

	while (1) {
		delay(20);

		if (driveMode == 1) {
			sp = driveTarget;
			kp = driveKP;
			kd = driveKD;
		} else if (driveMode == -1) {
			sp = turnTarget;
			kp = turnKP;
			kd = turnKD;
		} else {
			continue;
		}

		// read sensors
		int sv =
		    (rightMotors->getPosition() + leftMotors->getPosition() * driveMode) /
		    2;

		// speed
		int error = sp - sv;
		int derivative = error - prevError;
		prevError = error;
		int speed = error * kp + derivative * kd;

		// speed limiting
		if (speed > maxSpeed)
			speed = maxSpeed;
		if (speed < -maxSpeed)
			speed = -maxSpeed;

		speed = slew(speed); // slew

		// set motors
		left_drive(speed * driveMode);
		right_drive(speed);
	}
}

void startTasks() {
	Task drive_task(driveTask);
	Task odom_task(odomTask);
}

void initDrive(std::initializer_list<okapi::Motor> leftMotors,
               std::initializer_list<okapi::Motor> rightMotors, int gearset,
               int distance_constant, double degree_constant, int accel_step,
               int deccel_step, int arc_step, double driveKP, double driveKD,
               double turnKP, double turnKD, double arcKP) {

	// assign constants
	greenhat::distance_constant = distance_constant;
	greenhat::degree_constant = degree_constant;
	greenhat::accel_step = accel_step;
	greenhat::deccel_step = deccel_step;
	greenhat::arc_step = arc_step;
	greenhat::driveKP = driveKP;
	greenhat::driveKD = driveKD;
	greenhat::turnKP = turnKP;
	greenhat::turnKD = turnKD;
	greenhat::arcKP = arcKP;

	// configure drive motors
	greenhat::leftMotors = std::make_shared<okapi::MotorGroup>(leftMotors);
	greenhat::rightMotors = std::make_shared<okapi::MotorGroup>(rightMotors);
	greenhat::leftMotors->setGearing((okapi::AbstractMotor::gearset)gearset);
	greenhat::rightMotors->setGearing((okapi::AbstractMotor::gearset)gearset);
	//calibrate imu
	imu.reset();

	// start task
	startTasks();
}

/**************************************************/
// operator control
void tank(int left, int right) {
	driveMode = 0; // turns off autonomous tasks
	left_drive(left);
	right_drive(right);
}

void arcade(int vertical, int horizontal) {
	driveMode = 0; // turns off autonomous task
	left_drive(vertical + horizontal);
	right_drive(vertical - horizontal);
}

} // namespace greenhat
