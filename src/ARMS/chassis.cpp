#include "ARMS/chassis.h"
#include "ARMS/config.h"
#include "api.h"

using namespace pros;

namespace chassis {

// chassis mode enums
#define LINEAR 1
#define DISABLE 0
#define ANGULAR -1

// imu
std::shared_ptr<Imu> imu;
int imuPort;

// chassis motors
std::shared_ptr<okapi::MotorGroup> leftMotors;
std::shared_ptr<okapi::MotorGroup> rightMotors;

// individual motors
std::shared_ptr<okapi::Motor> frontLeft;
std::shared_ptr<okapi::Motor> frontRight;
std::shared_ptr<okapi::Motor> backLeft;
std::shared_ptr<okapi::Motor> backRight;

// quad encoders
std::shared_ptr<ADIEncoder> leftEncoder;
std::shared_ptr<ADIEncoder> rightEncoder;

// distance constants
int distance_constant;  // ticks per foot
double degree_constant; // ticks per degree

// slew control (autonomous only)
int accel_step;  // smaller number = more slew
int deccel_step; // 200 = no slew
int arc_step;    // acceleration for arcs
int min_speed;

// pid constants
double linearKP;
double linearKD;
double turnKP;
double turnKD;
double arcKP;
double difKP;

/**************************************************/
// edit below with caution!!!
static int mode = DISABLE;
static int linearTarget = 0;
static int turnTarget = 0;
static double vectorAngle = 0;
static int maxSpeed = 100;

/**************************************************/
// basic control

// move motor group at given velocity
void motorVoltage(std::shared_ptr<okapi::MotorGroup> motor, int vel) {
	motor->moveVoltage(vel * 120);
}

void motorVelocity(std::shared_ptr<okapi::MotorGroup> motor, int vel) {
	motor->moveVelocity(vel * (double)motor->getGearing() / 200);
}

void motorVoltage(std::shared_ptr<okapi::Motor> motor, int vel) {
	motor->moveVoltage(vel * 120);
}

void motorVelocity(std::shared_ptr<okapi::Motor> motor, int vel) {
	motor->moveVelocity(vel * (double)motor->getGearing() / 200);
}

void setBrakeMode(okapi::AbstractMotor::brakeMode b) {
	leftMotors->setBrakeMode(b);
	rightMotors->setBrakeMode(b);
	motorVelocity(leftMotors, 0);
	motorVelocity(rightMotors, 0);
}

void reset() {
	motorVelocity(leftMotors, 0);
	motorVelocity(rightMotors, 0);
	delay(10);
	leftMotors->tarePosition();
	rightMotors->tarePosition();

	frontLeft->tarePosition();
	frontRight->tarePosition();
	backLeft->tarePosition();
	backRight->tarePosition();
	if (leftEncoder) {
		leftEncoder->reset();
		rightEncoder->reset();
	}
}

int position(bool yDirection) {
	if (yDirection) {
		int top_pos, bot_pos;

		// TODO change when we add middle encoder
		if (false) {
			// top_pos = middleEncoder->get_value();
			// bot_pos = middleEncoder->get_value();
		} else {
			top_pos = frontLeft->getPosition() - frontRight->getPosition();
			bot_pos = backRight->getPosition() - backLeft->getPosition();
		}

		return ((mode == ANGULAR ? -top_pos : top_pos) + bot_pos) / 2;

	} else if (imuPort != 0 && mode == ANGULAR) {
		// read sensors using IMU if turning and one exists
		return imu->get_rotation() *
		       degree_constant; // scaling by degree constant ensures that PID
		                        // constants carry over between IMU and motor
		                        // encoder turning
	} else {
		int left_pos, right_pos;

		if (leftEncoder) {
			left_pos = leftEncoder->get_value();
			right_pos = rightEncoder->get_value();
		} else {
			left_pos = leftMotors->getPosition();
			right_pos = rightMotors->getPosition();
		}

		return ((mode == ANGULAR ? -left_pos : left_pos) + right_pos) / 2;
	}
}

int difference() {
	int left_pos, right_pos;

	if (leftEncoder) {
		left_pos = leftEncoder->get_value();
		right_pos = rightEncoder->get_value();
	} else {
		left_pos = leftMotors->getPosition();
		right_pos = rightMotors->getPosition();
	}

	return (mode == ANGULAR ? 0 : (left_pos - right_pos));
}

/**************************************************/
// slew control
static int lastSpeed = 0;
int slew(int speed) {
	int step;

	if (abs(lastSpeed) < abs(speed))
		if (mode == DISABLE)
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

	return abs(lastSpeed) < min_speed && step == accel_step ? min_speed
	                                                        : lastSpeed;
}

/**************************************************/
// chassis settling
bool isDriving() {
	static int count = 0;
	static int last = 0;
	static int lastTarget = 0;

	int curr = position();

	int target = turnTarget;
	if (mode == LINEAR)
		target = linearTarget;

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
void moveAsync(double sp, int max) {
	sp *= distance_constant;
	reset();
	maxSpeed = max;
	linearTarget = sp;
	mode = LINEAR;
	vectorAngle = 0;
}

void turnAsync(double sp, int max) {
	if (imuPort != 0)
		sp += imu->get_rotation();

	sp *= degree_constant;
	reset();
	maxSpeed = max;
	turnTarget = sp;
	mode = ANGULAR;
	vectorAngle = 0;
}

void turnAbsoluteAsync(double sp, int max) {
	double currentPos = imu->get_rotation();
	turnAsync(sp - currentPos, max);
}

void moveHoloAsync(double distance, double angle, int max) {
	distance *= distance_constant;
	reset();
	maxSpeed = max;
	linearTarget = distance;
	vectorAngle = angle * M_PI / 180;
	mode = 1;
}

void move(double sp, int max) {
	moveAsync(sp, max);
	delay(450);
	waitUntilSettled();
}

void turn(double sp, int max) {
	turnAsync(sp, max);
	delay(450);
	waitUntilSettled();
}

void moveHolo(double distance, double angle, int max) {
	moveHoloAsync(distance, angle, max);
	delay(450);
	waitUntilSettled();
}

void fast(double sp, int max) {
	if (sp < 0)
		max = -max;
	reset();
	lastSpeed = max;
	mode = DISABLE;
	motorVoltage(leftMotors, max);
	motorVoltage(rightMotors, max);

	if (sp > 0)
		while (position() < sp * distance_constant)
			delay(20);
	else
		while (position() > sp * distance_constant)
			delay(20);
}

void voltage(int t, int left_speed, int right_speed) {
	motorVoltage(leftMotors, left_speed);
	motorVoltage(rightMotors, right_speed == 0 ? left_speed : right_speed);
	delay(t);
}

void velocity(int t, int max) {
	motorVelocity(leftMotors, max);
	motorVelocity(rightMotors, max);
	delay(t);
}

void arc(bool mirror, int arc_length, double rad, int max, int type) {
	reset();
	int time_step = 0;
	mode = DISABLE;
	bool reversed = false;

	// reverse the movement if the length is negative
	if (arc_length < 0) {
		reversed = true;
		arc_length = -arc_length;
	}

	// fix jerk bug between velocity movements
	if (type < 2) {
		motorVelocity(leftMotors, 0);
		motorVelocity(rightMotors, 0);
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
			scaled_speed *= std::abs(2 * (.5 - (double)time_step / arc_length));
		else if (type == 3)
			scaled_speed *= (1 - (double)time_step / arc_length);

		// assign chassis motor speeds
		motorVelocity(leftMotors, mirror ? speed : scaled_speed);
		motorVelocity(rightMotors, mirror ? scaled_speed : speed);

		// increment time step
		time_step += 10;
		delay(10);
	}

	if (type != 1 && type != 2) {
		motorVelocity(leftMotors, 0);
		motorVelocity(rightMotors, 0);
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
	velocity(mid, max);

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
	double heading = M_PI / 2;
	double heading_degrees;
	double prev_heading = heading;

	double prev_left_pos = 0;
	double prev_right_pos = 0;

	double right_arc = 0;
	double left_arc = 0;
	double center_arc = 0;
	double delta_angle = 0;
	double radius = 0;
	double center_displacement = 0;
	double delta_x = 0;
	double delta_y = 0;

	while (true) {
		right_arc = rightMotors->getPosition() - prev_right_pos;
		left_arc = leftMotors->getPosition() - prev_left_pos;
		prev_right_pos = rightMotors->getPosition();
		prev_left_pos = leftMotors->getPosition();
		center_arc = (right_arc + left_arc) / 2.0;

		heading_degrees = imu->get_rotation();
		heading = heading_degrees * M_PI / 180;
		delta_angle = heading - prev_heading;
		prev_heading = heading;

		if (delta_angle != 0) {
			radius = center_arc / delta_angle;
			center_displacement = 2 * sin(delta_angle / 2) * radius;
		} else {
			center_displacement = center_arc;
		}

		delta_x = cos(heading) * center_displacement;
		delta_y = sin(heading) * center_displacement;

		global_x += delta_x;
		global_y += delta_y;

		printf("%f, %f, %f \n", global_x, global_y, heading);

		delay(10);
	}
}
int chassisTask() {
	int prevError = 0;
	double kp;
	double kd;
	int sp;

	while (1) {
		delay(20);

		if (mode == LINEAR) {
			sp = linearTarget;
			kp = linearKP;
			kd = linearKD;
		} else if (mode == ANGULAR) {
			sp = turnTarget;
			kp = turnKP;
			kd = turnKD;
		} else {
			continue;
		}

		// get position in the x direction
		int sv_x = position();

		// get position in the y direction
		int sv_y = position(true);

		// calculate total displacement using pythagorean theorem
		int sv;
		if (vectorAngle != 0)
			sv = sqrt(pow(sv_x, 2) + pow(sv_y, 2));
		else
			sv = sv_x; // just use the x value for non-holonomic movements

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
		if (vectorAngle != 0) {
			// calculate vectors for each wheel set
			double frontVector = sin(M_PI / 4 - vectorAngle);
			double backVector = sin(M_PI / 4 + vectorAngle);

			// set scaling factor based on largest vector
			double largestVector;
			if (abs(frontVector) > abs(backVector)) {
				largestVector = abs(frontVector);
			} else {
				largestVector = abs(backVector);
			}

			frontVector *= speed / largestVector;
			backVector *= speed / largestVector;

			motorVoltage(frontLeft, frontVector);
			motorVoltage(backLeft, backVector);
			motorVoltage(frontRight, backVector);
			motorVoltage(backRight, frontVector);

		} else {
			int dif = difference() * difKP;

			motorVelocity(leftMotors, (speed - dif) * mode);
			motorVelocity(rightMotors, speed + dif);
		}
	}
}

void startTasks() {
	Task chassis_task(chassisTask);
	if (imu) {
		Task odom_task(odomTask);
	}
}

void init(std::initializer_list<okapi::Motor> leftMotors,
          std::initializer_list<okapi::Motor> rightMotors, int gearset,
          int distance_constant, double degree_constant, int accel_step,
          int deccel_step, int arc_step, int min_speed, double linearKP,
          double linearKD, double turnKP, double turnKD, double arcKP,
          double difKP, int imuPort,
          std::tuple<int, int, int, int> encoderPorts) {

	// assign constants
	chassis::distance_constant = distance_constant;
	chassis::degree_constant = degree_constant;
	chassis::accel_step = accel_step;
	chassis::deccel_step = deccel_step;
	chassis::arc_step = arc_step;
	chassis::min_speed = min_speed;
	chassis::linearKP = linearKP;
	chassis::linearKD = linearKD;
	chassis::turnKP = turnKP;
	chassis::turnKD = turnKD;
	chassis::arcKP = arcKP;
	chassis::difKP = difKP;
	chassis::imuPort = imuPort;

	// configure chassis motors
	chassis::leftMotors = std::make_shared<okapi::MotorGroup>(leftMotors);
	chassis::rightMotors = std::make_shared<okapi::MotorGroup>(rightMotors);
	chassis::leftMotors->setGearing((okapi::AbstractMotor::gearset)gearset);
	chassis::rightMotors->setGearing((okapi::AbstractMotor::gearset)gearset);

	// initialize imu
	if (imuPort != 0) {
		imu = std::make_shared<Imu>(imuPort);
		imu->reset();
		while (imu->is_calibrating()) {
			delay(10);
		}
		printf("IMU calibrated!");
	}

	if (std::get<0>(encoderPorts) != 0) {
		leftEncoder = std::make_shared<ADIEncoder>(std::get<0>(encoderPorts),
		                                           std::get<1>(encoderPorts));
		rightEncoder = std::make_shared<ADIEncoder>(std::get<2>(encoderPorts),
		                                            std::get<3>(encoderPorts));
	}

	// configure individual motors for holonomic chassis
	chassis::frontLeft = std::make_shared<okapi::Motor>(*leftMotors.begin());
	chassis::backLeft = std::make_shared<okapi::Motor>(*(leftMotors.end() - 1));
	chassis::frontRight = std::make_shared<okapi::Motor>(*rightMotors.begin());
	chassis::backRight = std::make_shared<okapi::Motor>(*(rightMotors.end() - 1));

	// set gearing for individual motors
	chassis::frontLeft->setGearing((okapi::AbstractMotor::gearset)gearset);
	chassis::backLeft->setGearing((okapi::AbstractMotor::gearset)gearset);
	chassis::frontRight->setGearing((okapi::AbstractMotor::gearset)gearset);
	chassis::backRight->setGearing((okapi::AbstractMotor::gearset)gearset);

	// start task
	startTasks();
}

/**************************************************/
// operator control
void tank(int left_speed, int right_speed) {
	mode = DISABLE; // turns off autonomous tasks
	motorVoltage(leftMotors, left_speed);
	motorVoltage(rightMotors, right_speed);
}

void arcade(int vertical, int horizontal) {
	mode = DISABLE; // turns off autonomous task
	motorVoltage(leftMotors, vertical + horizontal);
	motorVoltage(rightMotors, vertical - horizontal);
}

void holonomic(int x, int y, int z) {
	mode = 0; // turns off autonomous task
	motorVoltage(frontLeft, x + y + z);
	motorVoltage(frontRight, x - y - z);
	motorVoltage(backLeft, x + y - z);
	motorVoltage(backRight, x - y + z);
}

} // namespace chassis
