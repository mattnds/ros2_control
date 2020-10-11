// Copyright 2013, PAL Robotics S.L.
// Copyright 2008, Willow Garage, Inc.
// All rights reserved.
//
// Software License Agreement (BSD License 2.0)
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above
//    copyright notice, this list of conditions and the following
//    disclaimer in the documentation and/or other materials provided
//    with the distribution.
//  * Neither the name of the copyright holders nor the names of its
//    contributors may be used to endorse or promote products derived
//    from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
// FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
// COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
// INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
// BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
// LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
// ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/// \author Adolfo Rodriguez Tsouroukdissian

#ifndef JOINT_LIMITS_INTERFACE__JOINT_LIMITS_INTERFACE_HPP_
#define JOINT_LIMITS_INTERFACE__JOINT_LIMITS_INTERFACE_HPP_

#include <hardware_interface/joint_handle.hpp>

#include <rclcpp/duration.hpp>
#include <rclcpp/rclcpp.hpp>

#include <rcppmath/clamp.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#include <memory>

#include "joint_limits_interface/joint_limits.hpp"
#include "joint_limits_interface/joint_limits_interface_exception.hpp"


namespace joint_limits_interface
{

/** \brief The base class of limit handles for enforcing position, velocity, and effort limits of
 * an effort-controlled joint.
 */
class JointSaturationLimitHandle
{
public:
  JointSaturationLimitHandle()
  : prev_pos_(std::numeric_limits<double>::quiet_NaN()),
    prev_vel_(0.0)
  {}

  JointSaturationLimitHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const JointLimits & limits)
  : jposh_(jposh),
    jcmdh_(jcmdh),
    limits_(limits),
    prev_pos_(std::numeric_limits<double>::quiet_NaN()),
    prev_vel_(0.0)
  {}

  JointSaturationLimitHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jvelh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const JointLimits & limits)
  : jposh_(jposh),
    jvelh_(jvelh),
    jcmdh_(jcmdh),
    limits_(limits),
    prev_pos_(std::numeric_limits<double>::quiet_NaN()),
    prev_vel_(0.0)
  {}

  /** \return Joint name. */
  std::string get_name() const
  {
    return jposh_ ? jposh_->get_name() :
           jvelh_ ? jvelh_->get_name() :
           jcmdh_ ? jcmdh_->get_name() :
           std::string();
  }

  /** \brief Sub-class implementation of limit enforcing policy.
   */
  virtual void enforceLimits(const rclcpp::Duration & period) = 0;

  /** \brief  clear stored state, causing it to reset next iteration
   */
  virtual void reset()
  {
    prev_pos_ = std::numeric_limits<double>::quiet_NaN();
    prev_vel_ = 0.0;
  }

protected:
  std::shared_ptr<hardware_interface::JointHandle> jposh_;
  std::shared_ptr<hardware_interface::JointHandle> jvelh_;
  std::shared_ptr<hardware_interface::JointHandle> jcmdh_;
  joint_limits_interface::JointLimits limits_;

  // stored state - track position and velocity of last update
  double prev_pos_;
  double prev_vel_;

  /** \brief Return velocity for limit calculations.
   *
   * @param period Time since last measurement
   * @return the velocity, from state if available, otherwise from previous position history.
   */
  double get_velocity(const rclcpp::Duration & period) const
  {
    // if we have a handle to a velocity state we can directly return state velocity
    // otherwise we will estimate velocity from previous position (command or state)
    return jvelh_ ?
           jvelh_->get_value() :
           (jposh_->get_value() - prev_pos_) / period.seconds();
  }
};


/** \brief The base class of limit handles for enforcing position, velocity, and effort limits of
 * an effort-controlled joint that has soft-limits.
 */
class JointSoftLimitsHandle : public JointSaturationLimitHandle
{
public:
  JointSoftLimitsHandle() {}

  JointSoftLimitsHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const JointLimits & limits,
    const SoftJointLimits & soft_limits)
  : JointSaturationLimitHandle(jposh, jcmdh, limits),
    soft_limits_(soft_limits)
  {}

  JointSoftLimitsHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jvelh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const JointLimits & limits,
    const SoftJointLimits & soft_limits)
  : JointSaturationLimitHandle(jposh, jvelh, jcmdh, limits),
    soft_limits_(soft_limits)
  {}

protected:
  joint_limits_interface::SoftJointLimits soft_limits_;
};


/** \brief A handle used to enforce position and velocity limits of a position-controlled joint that does not have
    soft limits. */
class PositionJointSaturationHandle : public JointSaturationLimitHandle
{
public:
  PositionJointSaturationHandle() {}

  PositionJointSaturationHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const JointLimits & limits)
  : JointSaturationLimitHandle(jposh, jcmdh, limits)
  {
    if (limits_.has_position_limits) {
      min_pos_limit_ = limits_.min_position;
      max_pos_limit_ = limits_.max_position;
    } else {
      min_pos_limit_ = -std::numeric_limits<double>::max();
      max_pos_limit_ = std::numeric_limits<double>::max();
    }
  }

/**
 * \brief Enforce position and velocity limits for a joint that is not subject to soft limits.
 *
 * \param period Control period.
 */
  void enforceLimits(const rclcpp::Duration & period)
  {
    if (std::isnan(prev_pos_)) {
      prev_pos_ = jposh_->get_value();
    }

    double min_pos, max_pos;
    if (limits_.has_velocity_limits) {
      // enforce velocity limits
      // set constraints on where the position can be based on the
      // max velocity times seconds since last update
      const double delta_pos = limits_.max_velocity * period.seconds();
      min_pos = std::max(prev_pos_ - delta_pos, min_pos_limit_);
      max_pos = std::min(prev_pos_ + delta_pos, max_pos_limit_);
    } else {
      // no velocity limit, so position is simply limited to set extents (our imposed soft limits)
      min_pos = min_pos_limit_;
      max_pos = max_pos_limit_;
    }

    // clamp command position to our computed min/max position
    const double cmd = rcppmath::clamp(jcmdh_->get_value(), min_pos, max_pos);
    jcmdh_->set_value(cmd);

    prev_pos_ = cmd;
  }

private:
  double min_pos_limit_, max_pos_limit_;
};

/**
 * \brief A handle used to enforce position and velocity limits of a position-controlled joint.
 *
 * This class implements a very simple position and velocity limits enforcing policy, and tries to impose the least
 * amount of requisites on the underlying hardware platform.
 * This lowers considerably the entry barrier to use it, but also implies some limitations.
 *
 * <b>Requisites</b>
 * - Position (for non-continuous joints) and velocity limits specification.
 * - Soft limits specification. The \c k_velocity parameter is \e not used.
 *
 * <b>Open loop nature</b>
 *
 * Joint position and velocity limits are enforced in an open-loop fashion, that is, the command is checked for
 * validity without relying on the actual position/velocity values.
 *
 * - Actual position values are \e not used because in some platforms there might be a substantial lag
 *   between sending a command and executing it (propagate command to hardware, reach control objective,
 *   read from hardware).
 *
 * - Actual velocity values are \e not used because of the above reason, and because some platforms might not expose
 *   trustworthy velocity measurements, or none at all.
 *
 * The downside of the open loop behavior is that velocity limits will not be enforced when recovering from large
 * position tracking errors. Only the command is guaranteed to comply with the limits specification.
 *
 * \note: This handle type is \e stateful, ie. it stores the previous position command to estimate the command
 * velocity.
 */

// TODO(anyone): Leverage %Reflexxes Type II library for acceleration limits handling?
class PositionJointSoftLimitsHandle : public JointSoftLimitsHandle
{
public:
  PositionJointSoftLimitsHandle()
  {}

  PositionJointSoftLimitsHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits,
    const joint_limits_interface::SoftJointLimits & soft_limits)
  : JointSoftLimitsHandle(jposh, jcmdh, limits, soft_limits)
  {
    if (!limits.has_velocity_limits) {
      throw joint_limits_interface::JointLimitsInterfaceException(
              "Cannot enforce limits for joint '" + get_name() +
              "'. It has no velocity limits specification.");
    }
  }

  /**
   * \brief Enforce position and velocity limits for a joint subject to soft limits.
   *
   * If the joint has no position limits (eg. a continuous joint), only velocity limits will be
   * enforced.
   * \param period Control period.
   */
  void enforceLimits(const rclcpp::Duration & period) override
  {
    assert(period.seconds() > 0.0);

    // Current position
    if (std::isnan(prev_pos_)) {
      // Happens only once at initialization
      prev_pos_ = jposh_->get_value();
    }
    const double pos = prev_pos_;

    // Velocity bounds
    double soft_min_vel;
    double soft_max_vel;

    if (limits_.has_position_limits) {
      // Velocity bounds depend on the velocity limit and the proximity to the position limit
      soft_min_vel = rcppmath::clamp(
        -soft_limits_.k_position * (pos - soft_limits_.min_position),
        -limits_.max_velocity,
        limits_.max_velocity);

      soft_max_vel = rcppmath::clamp(
        -soft_limits_.k_position * (pos - soft_limits_.max_position),
        -limits_.max_velocity,
        limits_.max_velocity);
    } else {
      // No position limits, eg. continuous joints
      soft_min_vel = -limits_.max_velocity;
      soft_max_vel = limits_.max_velocity;
    }

    // Position bounds
    const double dt = period.seconds();
    double pos_low = pos + soft_min_vel * dt;
    double pos_high = pos + soft_max_vel * dt;

    if (limits_.has_position_limits) {
      // This extra measure safeguards against pathological cases, like when the soft limit lies
      // beyond the hard limit
      pos_low = std::max(pos_low, limits_.min_position);
      pos_high = std::min(pos_high, limits_.max_position);
    }

    // Saturate position command according to bounds
    const double pos_cmd = rcppmath::clamp(
      jcmdh_->get_value(),
      pos_low,
      pos_high);
    jcmdh_->set_value(pos_cmd);

    // Cache variables
    // todo: shouldn't this just be pos_cmd? why call into the command handle to
    //  get what we have in the above line?
    prev_pos_ = jcmdh_->get_value();
  }
};

/** \brief A handle used to enforce position, velocity, and effort limits of an effort-controlled
 * joint that does not have soft limits.
 */
class EffortJointSaturationHandle : public JointSaturationLimitHandle
{
public:
  EffortJointSaturationHandle() {}

  EffortJointSaturationHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jvelh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits)
  : JointSaturationLimitHandle(jposh, jvelh, jcmdh, limits)
  {
    if (!limits.has_velocity_limits) {
      throw joint_limits_interface::JointLimitsInterfaceException(
              "Cannot enforce limits for joint '" + get_name() +
              "'. It has no velocity limits specification.");
    }
    if (!limits.has_effort_limits) {
      throw joint_limits_interface::JointLimitsInterfaceException(
              "Cannot enforce limits for joint '" + get_name() +
              "'. It has no efforts limits specification.");
    }
  }

  EffortJointSaturationHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits)
  : EffortJointSaturationHandle(jh, nullptr, jcmdh, limits)
  {
  }

  /**
   * \brief Enforce position, velocity, and effort limits for a joint that is not subject
   * to soft limits.
   */
  void enforceLimits(const rclcpp::Duration & period) override
  {
    double min_eff = -limits_.max_effort;
    double max_eff = limits_.max_effort;

    if (limits_.has_position_limits) {
      const double pos = jposh_->get_value();
      if (pos < limits_.min_position) {
        min_eff = 0.0;
      } else if (pos > limits_.max_position) {
        max_eff = 0.0;
      }
    }

    const double vel = get_velocity(period);
    if (vel < -limits_.max_velocity) {
      min_eff = 0.0;
    } else if (vel > limits_.max_velocity) {
      max_eff = 0.0;
    }

    double clamped = rcppmath::clamp(jcmdh_->get_value(), min_eff, max_eff);
    jcmdh_->set_value(clamped);
  }
};

/** \brief A handle used to enforce position, velocity and effort limits of an effort-controlled
  * joint.
  */

// TODO(anyone): This class is untested!. Update unit tests accordingly.
class EffortJointSoftLimitsHandle : public JointSoftLimitsHandle
{
public:
  EffortJointSoftLimitsHandle() {}

  EffortJointSoftLimitsHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jvelh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits,
    const joint_limits_interface::SoftJointLimits & soft_limits)
  : JointSoftLimitsHandle(jposh, jvelh, jcmdh, limits, soft_limits)
  {
    if (!limits.has_velocity_limits) {
      throw joint_limits_interface::JointLimitsInterfaceException(
              "Cannot enforce limits for joint '" + get_name() +
              "'. It has no velocity limits specification.");
    }
    if (!limits.has_effort_limits) {
      throw joint_limits_interface::JointLimitsInterfaceException(
              "Cannot enforce limits for joint '" + get_name() +
              "'. It has no effort limits specification.");
    }
  }

  EffortJointSoftLimitsHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits,
    const joint_limits_interface::SoftJointLimits & soft_limits)
  : EffortJointSoftLimitsHandle(jh, nullptr, jcmdh, limits, soft_limits)
  {
  }

  /**
   * \brief Enforce position, velocity and effort limits for a joint subject to soft limits.
   *
   * If the joint has no position limits (eg. a continuous joint), only velocity and effort limits
   * will be enforced.
   */
  void enforceLimits(const rclcpp::Duration & period) override
  {
    // Current state
    const double pos = jposh_->get_value();
    const double vel = get_velocity(period);

    // Velocity bounds
    double soft_min_vel;
    double soft_max_vel;

    if (limits_.has_position_limits) {
      // Velocity bounds depend on the velocity limit and the proximity to the position limit
      soft_min_vel = rcppmath::clamp(
        -soft_limits_.k_position * (pos - soft_limits_.min_position),
        -limits_.max_velocity,
        limits_.max_velocity);

      soft_max_vel = rcppmath::clamp(
        -soft_limits_.k_position * (pos - soft_limits_.max_position),
        -limits_.max_velocity,
        limits_.max_velocity);
    } else {
      // No position limits, eg. continuous joints
      soft_min_vel = -limits_.max_velocity;
      soft_max_vel = limits_.max_velocity;
    }

    // Effort bounds depend on the velocity and effort bounds
    const double soft_min_eff = rcppmath::clamp(
      -soft_limits_.k_velocity * (vel - soft_min_vel),
      -limits_.max_effort,
      limits_.max_effort);

    const double soft_max_eff = rcppmath::clamp(
      -soft_limits_.k_velocity * (vel - soft_max_vel),
      -limits_.max_effort,
      limits_.max_effort);

    // Saturate effort command according to bounds
    const double eff_cmd = rcppmath::clamp(
      jcmdh_->get_value(),
      soft_min_eff,
      soft_max_eff);
    jcmdh_->set_value(eff_cmd);
  }
};


/** \brief A handle used to enforce velocity and acceleration limits of a velocity-controlled joint.
  */
class VelocityJointSaturationHandle : public JointSaturationLimitHandle
{
public:
  VelocityJointSaturationHandle() {}

  VelocityJointSaturationHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jvelh,  // currently unused
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits)
  : JointSaturationLimitHandle(nullptr, jvelh, jcmdh, limits)
  {
    if (!limits.has_velocity_limits) {
      throw joint_limits_interface::JointLimitsInterfaceException(
              "Cannot enforce limits for joint '" + get_name() +
              "'. It has no velocity limits specification.");
    }
  }

  VelocityJointSaturationHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits)
  : JointSaturationLimitHandle(nullptr, nullptr, jcmdh, limits)
  {
    if (!limits.has_velocity_limits) {
      throw joint_limits_interface::JointLimitsInterfaceException(
              "Cannot enforce limits for joint '" + get_name() +
              "'. It has no velocity limits specification.");
    }
  }

  /**
   * \brief Enforce joint velocity and acceleration limits.
   * \param period Control period.
   */
  void enforceLimits(const rclcpp::Duration & period) override
  {
    // Velocity bounds
    double vel_low;
    double vel_high;

    if (limits_.has_acceleration_limits) {
      assert(period.seconds() > 0.0);
      const double dt = period.seconds();

      vel_low = std::max(prev_vel_ - limits_.max_acceleration * dt, -limits_.max_velocity);
      vel_high = std::min(prev_vel_ + limits_.max_acceleration * dt, limits_.max_velocity);
    } else {
      vel_low = -limits_.max_velocity;
      vel_high = limits_.max_velocity;
    }

    // Saturate velocity command according to limits
    const double vel_cmd = rcppmath::clamp(
      jcmdh_->get_value(),
      vel_low,
      vel_high);
    jcmdh_->set_value(vel_cmd);

    // Cache variables
    prev_vel_ = jcmdh_->get_value();
  }
};

/** \brief A handle used to enforce position, velocity, and acceleration limits of a
  * velocity-controlled joint.
  */
class VelocityJointSoftLimitsHandle : public JointSoftLimitsHandle
{
public:
  VelocityJointSoftLimitsHandle() {}

  VelocityJointSoftLimitsHandle(
    const std::shared_ptr<hardware_interface::JointHandle> & jposh,
    const std::shared_ptr<hardware_interface::JointHandle> & jvelh,
    const std::shared_ptr<hardware_interface::JointHandle> & jcmdh,
    const joint_limits_interface::JointLimits & limits,
    const joint_limits_interface::SoftJointLimits & soft_limits)
  : JointSoftLimitsHandle(jposh, jvelh, jcmdh, limits, soft_limits)
  {
    if (limits.has_velocity_limits) {
      max_vel_limit_ = limits.max_velocity;
    } else {
      max_vel_limit_ = std::numeric_limits<double>::max();
    }
  }

  /**
   * \brief Enforce position, velocity, and acceleration limits for a velocity-controlled joint
   * subject to soft limits.
   *
   * \param period Control period.
   */
  void enforceLimits(const rclcpp::Duration & period)
  {
    double min_vel, max_vel;
    if (limits_.has_position_limits) {
      // Velocity bounds depend on the velocity limit and the proximity to the position limit.
      const double pos = jposh_->get_value();
      min_vel = rcppmath::clamp(
        -soft_limits_.k_position * (pos - soft_limits_.min_position),
        -max_vel_limit_, max_vel_limit_);
      max_vel = rcppmath::clamp(
        -soft_limits_.k_position * (pos - soft_limits_.max_position),
        -max_vel_limit_, max_vel_limit_);
    } else {
      min_vel = -max_vel_limit_;
      max_vel = max_vel_limit_;
    }

    if (limits_.has_acceleration_limits) {
      const double vel = get_velocity(period);
      const double delta_t = period.seconds();
      min_vel = std::max(vel - limits_.max_acceleration * delta_t, min_vel);
      max_vel = std::min(vel + limits_.max_acceleration * delta_t, max_vel);
    }

    jcmdh_->set_value(rcppmath::clamp(jcmdh_->get_value(), min_vel, max_vel));
  }

private:
  double max_vel_limit_;
};

// TODO(anyone): Port this to ROS 2
// //**
//  * \brief Interface for enforcing joint limits.
//  *
//  * \tparam HandleType %Handle type. Must implement the following methods:
//  *  \code
//  *   void enforceLimits();
//  *   std::string get_name() const;
//  *  \endcode
//  */
// template<class HandleType>
// class joint_limits_interface::JointLimitsInterface
//   : public hardware_interface::ResourceManager<HandleType>
// {
// public:
//   HandleType getHandle(const std::string & name)
//   {
//     // Rethrow exception with a meaningful type
//     try {
//       return this->hardware_interface::ResourceManager<HandleType>::getHandle(name);
//     } catch (const std::logic_error & e) {
//       throw joint_limits_interface::JointLimitsInterfaceException(e.what());
//     }
//   }
//
//   /** \name Real-Time Safe Functions
//    *\{*/
//   /** \brief Enforce limits for all managed handles. */
//   void enforceLimits(const rclcpp::Duration & period)
//   {
//     for (auto && resource_name_and_handle : this->resource_map_) {
//       resource_name_and_handle.second.enforceLimits(period);
//     }
//   }
//   /*\}*/
// };
//
// /** Interface for enforcing limits on a position-controlled joint through saturation. */
// class PositionJointSaturationInterface
//   : public joint_limits_interface::JointLimitsInterface<PositionJointSaturationHandle>
// {
// public:
//   /** \name Real-Time Safe Functions
//    *\{*/
//   /** \brief Reset all managed handles. */
//   void reset()
//   {
//     for (auto && resource_name_and_handle : this->resource_map_) {
//       resource_name_and_handle.second.reset();
//     }
//   }
//   /*\}*/
// };
//
// /** Interface for enforcing limits on a position-controlled joint with soft position limits. */
// class PositionJointSoftLimitsInterface
//   : public joint_limits_interface::JointLimitsInterface<PositionJointSoftLimitsHandle>
// {
// public:
//   /** \name Real-Time Safe Functions
//    *\{*/
//   /** \brief Reset all managed handles. */
//   void reset()
//   {
//     for (auto && resource_name_and_handle : this->resource_map_) {
//       resource_name_and_handle.second.reset();
//     }
//   }
//   /*\}*/
// };
//
// /** Interface for enforcing limits on an effort-controlled joint through saturation. */
// class EffortJointSaturationInterface
//   : public joint_limits_interface::JointLimitsInterface<EffortJointSaturationHandle>
// {
// };
//
// /** Interface for enforcing limits on an effort-controlled joint with soft position limits. */
// class EffortJointSoftLimitsInterface
//   : public joint_limits_interface::JointLimitsInterface<EffortJointSoftLimitsHandle>
// {
// };
//
// /** Interface for enforcing limits on a velocity-controlled joint through saturation. */
// class VelocityJointSaturationInterface
//   : public joint_limits_interface::JointLimitsInterface<VelocityJointSaturationHandle>
// {
// };
//
// /** Interface for enforcing limits on a velocity-controlled joint with soft position limits. */
// class VelocityJointSoftLimitsInterface
//   : public joint_limits_interface::JointLimitsInterface<VelocityJointSoftLimitsHandle>
// {
// };
}  // namespace joint_limits_interface

#endif  // JOINT_LIMITS_INTERFACE__JOINT_LIMITS_INTERFACE_HPP_
