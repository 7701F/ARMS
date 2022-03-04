#include "ARMS/lib.h"
#include "api.h"

namespace arms::pid {

int mode = DISABLE;

// pid constants
double linearKP;
double angularKP;
double linearKI;
double linearKD;
double angularKI;
double angularKD;

// kp defaults
double defaultLinearKP;
double defaultAngularKP;

double minPower;
double odomAngleScaling;

int direction;
bool thru;

// pid targets
double angularTarget = 0;
Point pointTarget{0, 0};
std::vector<Point> waypoints;

double pid(double error, double* pe, double* in, double kp, double ki,
           double kd) {

	double derivative = error - *pe;
	if ((*pe > 0 && error < 0) || (*pe < 0 && error > 0))
		*in = 0; // remove integral at zero error
	double speed = error * kp + *in * ki + derivative * kd;

	// scale back integral if over max windup
	if (fabs(speed) < 100) {
		*in += error;
	}

	*pe = error;

	return speed;
}

double pid(double target, double sv, double* pe, double* in, double kp,
           double ki, double kd) {
	double error = target - sv;
	return pid(error, pe, in, kp, ki, kd);
}

std::array<double, 2> translational() {
	// previous sensor values
	static double pe_lin = 0;
	static double pe_ang = 0;

	// integral values
	static double in_lin;
	static double in_ang;

	// find the lookahead point
	pointTarget = purepursuit::getLookaheadPoint(waypoints);

	// get current error
	double lin_error = purepursuit::getDistanceError(waypoints);
	double ang_error = odom::getAngleError(pointTarget);

	// check for default kp
	if (linearKP == -1)
		linearKP = defaultLinearKP;
	if (angularKP == -1)
		angularKP = defaultAngularKP;

	double lin_speed =
	    pid(lin_error, &pe_lin, &in_lin, linearKP, linearKI, linearKD);

	double ang_speed =
	    pid(ang_error, &pe_ang, &in_ang, angularKP * odomAngleScaling, angularKI,
	        angularKD * odomAngleScaling);

	// apply direction
	if (direction == 3 || (direction == 1 && fabs(ang_error) > M_PI_2)) {
		ang_error = ang_error - (ang_error / fabs(ang_error)) * M_PI;
		lin_speed = -lin_speed;
	}

	// disable PID for thru movement
	if (thru)
		lin_speed = chassis::maxSpeed;

	// scale down angular speed as linear scales down
	if (fabs(ang_speed) > fabs(lin_speed))
		ang_speed = fabs(lin_speed) * ang_speed / fabs(ang_speed);

	// reduce linear speed if ang_speed is really large
	double scaling_factor = fabs(lin_speed) + fabs(ang_speed);
	lin_speed = chassis::maxSpeed * lin_speed / (scaling_factor);

	// add speeds together
	double left_speed = lin_speed - ang_speed;
	double right_speed = lin_speed + ang_speed;

	return {left_speed, right_speed};
}

std::array<double, 2> angular() {
	static double pe = 0; // previous error
	static double in = 0; // integral

	if (angularKP == -1)
		angularKP = defaultAngularKP;

	double sv = odom::heading_degrees;
	double speed =
	    pid(angularTarget, sv, &pe, &in, angularKP, angularKI, angularKD);
	return {speed, -speed}; // clockwise positive
}

void init(double linearKP, double linearKI, double linearKD, double angularKP,
          double angularKI, double angularKD, double minPower,
          double odomAngleScaling) {

	pid::defaultLinearKP = linearKP;
	pid::linearKI = linearKI;
	pid::linearKD = linearKD;
	pid::defaultAngularKP = angularKP;
	pid::angularKI = angularKI;
	pid::angularKD = angularKD;
	pid::minPower = minPower;
	pid::odomAngleScaling = odomAngleScaling;
}

} // namespace arms::pid
