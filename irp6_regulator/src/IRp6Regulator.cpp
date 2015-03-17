#include <rtt/TaskContext.hpp>
#include <rtt/Port.hpp>
#include <std_msgs/Float64.h>
#include <std_msgs/Int64.h>
#include <rtt/Component.hpp>
#include <std_srvs/Empty.h>
#include <ros/ros.h>

// to musi byc dodane w includach w devel
// #include "../../hardware_interface/src/string_colors.h"

#include "IRp6Regulator.h"

const int MAX_PWM = 190;

IRp6Regulator::IRp6Regulator(const std::string& name)
    : TaskContext(name),
      desired_position_("DesiredPosition"),
      deltaInc_in("deltaInc_in"),
      computedPwm_out("computedPwm_out"),
      synchro_state_in_("SynchroStateIn"),
      emergency_stop_out_("EmergencyStopOut"),
      a_(0.0),
      b0_(0.0),
      b1_(0.0),
      delta_eint_old(0.0),
      delta_eint(0.0),
      deltaIncData(0.0),
      output_value(0.0),
      desired_position_increment_(0.0),
      position_increment_new(0.0),
      position_increment_old(0.0),
      set_value_new(0.0),
      set_value_old(0.0),
      set_value_very_old(0.0),
      step_new(0.0),
      step_old(0.0),
      step_old_pulse(0.0),
      update_hook_iteration_number_(0),
      new_position_iteration_number_(0),
      synchro_state_old_(false),
      max_desired_increment_(0.0) {

  this->addEventPort(desired_position_).doc(
      "Receiving a value of position step");
  this->addPort(deltaInc_in).doc("Receiving a value of measured increment.");
  this->addPort(computedPwm_out).doc(
      "Sending value of calculated pwm or current.");
  this->addPort(synchro_state_in_).doc("Synchro State from HardwareInterface");
  this->addPort(emergency_stop_out_).doc("Emergency Stop Out");

  this->addProperty("A", A_).doc("");
  this->addProperty("BB0", BB0_).doc("");
  this->addProperty("BB1", BB1_).doc("");
  this->addProperty("current_mode", current_mode_).doc("");
  this->addProperty("max_output_current", max_output_current_).doc("");
  this->addProperty("current_reg_kp", current_reg_kp_).doc("");
  this->addProperty("reg_number", reg_number_).doc("");
  this->addProperty("debug", debug_).doc("");
  this->addProperty("eint_dif", eint_dif_).doc("");
  this->addProperty("max_desired_increment", max_desired_increment_).doc("");
  this->addProperty("enc_res", enc_res_).doc("");

}

IRp6Regulator::~IRp6Regulator() {

}

bool IRp6Regulator::configureHook() {
  reset();

  a_ = A_;
  b0_ = BB0_;
  b1_ = BB1_;

  desired_position_old_ = desired_position_new_ = 0.0;

  return true;

}

void IRp6Regulator::updateHook() {

  if (NewData == deltaInc_in.read(deltaIncData)) {

    update_hook_iteration_number_++;
    if (update_hook_iteration_number_ <= 1) {
      deltaIncData = 0.0;
    }

    if (NewData == desired_position_.read(desired_position_new_)) {
      new_position_iteration_number_++;
      if (new_position_iteration_number_ <= 1) {
        desired_position_old_ = desired_position_new_;
      }
    }

    if (NewData == synchro_state_in_.read(synchro_state_new_)) {

      if (synchro_state_new_ != synchro_state_old_) {
        desired_position_old_ = desired_position_new_;
        synchro_state_old_ = synchro_state_new_;

      }
    }

    desired_position_increment_ =
        (desired_position_new_ - desired_position_old_)
            * (enc_res_ / (2.0 * M_PI));

    if (fabs(desired_position_increment_) > max_desired_increment_) {
      std::cout << "very high pos_inc_: " << reg_number_ << " pos_inc: "
                << desired_position_increment_ << std::endl;

      emergency_stop_out_.write(true);
    }

    desired_position_old_ = desired_position_new_;

    if (!debug_) {

      int output = doServo(desired_position_increment_, deltaIncData);
      /*
       std::cout << std::dec << GREEN << "output: " << output << " pos_inc: "
       << desired_position_increment_ << " inp_inc: " << deltaIncData
       << RESET << std::endl;

       if (iteration_number_ > 2000) {
       output = 0;
       }
       */
      computedPwm_out.write(output);
    } else {
      computedPwm_out.write(0.0);
    }
  }

}

int IRp6Regulator::doServo(double step_new, int pos_inc) {

// algorytm regulacji dla serwomechanizmu
// position_increment_old - przedostatnio odczytany przyrost polozenie
//                         (delta y[k-2] -- mierzone w impulsach)
// position_increment_new - ostatnio odczytany przyrost polozenie
//                         (delta y[k-1] -- mierzone w impulsach)
// step_old_pulse               - poprzednia wartosc zadana dla jednego kroku
//                         regulacji (przyrost wartosci zadanej polozenia --
//                         delta r[k-2] -- mierzone w impulsach)
// step_new               - nastepna wartosc zadana dla jednego kroku
//                         regulacji (przyrost wartosci zadanej polozenia --
//                         delta r[k-1] -- mierzone w radianach)
// set_value_new          - wielkosc kroku do realizacji przez HIP
//                         (wypelnienie PWM -- u[k]): czas trwania jedynki
// set_value_old          - wielkosc kroku do realizacji przez HIP
//                         (wypelnienie PWM -- u[k-1]): czas trwania jedynki
// set_value_very_old     - wielkosc kroku do realizacji przez HIP
//                         (wypelnienie PWM -- u[k-2]): czas trwania jedynki

  double step_new_pulse;  // nastepna wartosc zadana dla jednego kroku regulacji

  step_new_pulse = step_new;
  position_increment_new = pos_inc;

// Przyrost calki uchybu
  delta_eint = delta_eint_old
      + (1.0 + eint_dif_) * (step_new_pulse - position_increment_new)
      - (1.0 - eint_dif_) * (step_old_pulse - position_increment_old);

//std::cout << "POS INCREMENT NEW: " << position_increment_new <<  std::endl;

// obliczenie nowej wartosci wypelnienia PWM algorytm PD + I
  set_value_new = (1 + a_) * set_value_old - a_ * set_value_very_old
      + b0_ * delta_eint - b1_ * delta_eint_old;

//std::cout << "PWM VALUE (" << pos_inc << " to " << pos_inc_new << ") IS " << (int)set_value_new << std::endl;

// ograniczenie na sterowanie
  if (set_value_new > MAX_PWM)
    set_value_new = MAX_PWM;
  if (set_value_new < -MAX_PWM)
    set_value_new = -MAX_PWM;

  if (current_mode_) {
    output_value = set_value_new * current_reg_kp_;
    if (output_value > max_output_current_) {
      output_value = max_output_current_;
    } else if (output_value < -max_output_current_) {
      output_value = -max_output_current_;
    }
  } else {
    output_value = set_value_new;
  }

  if (debug_) {
    //   std::cout << "output_value: " << output_value << std::endl;
  }

// przepisanie nowych wartosci zmiennych do zmiennych przechowujacych wartosci poprzednie
  position_increment_old = position_increment_new;
  delta_eint_old = delta_eint;
  step_old_pulse = step_new_pulse;
  set_value_very_old = set_value_old;
  set_value_old = set_value_new;

  return ((int) output_value);
}

void IRp6Regulator::reset() {
  position_increment_old = 0.0;
  position_increment_new = 0.0;
  step_old_pulse = 0.0;
  step_new = 0.0;
  set_value_new = 0.0;
  set_value_old = 0.0;
  set_value_very_old = 0.0;
  delta_eint = 0.0;
  delta_eint_old = 0.0;
}

ORO_CREATE_COMPONENT(IRp6Regulator)
