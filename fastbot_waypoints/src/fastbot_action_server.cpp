#include <cmath>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "fastbot_waypoints_msgs/action/waypoint_action.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "tf2/LinearMath/Matrix3x3.h"
#include "tf2/LinearMath/Quaternion.h"

class WaypointActionServer : public rclcpp::Node {
public:
  using WaypointAction = fastbot_waypoints_msgs::action::WaypointAction;
  using GoalHandleWaypointAction =
      rclcpp_action::ServerGoalHandle<WaypointAction>;

  explicit WaypointActionServer(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions())
      : Node("fastbot_as", options) {
    using namespace std::placeholders;

    // creates action server
    this->action_server_ = rclcpp_action::create_server<WaypointAction>(
        this, "fastbot_as",
        std::bind(&WaypointActionServer::handle_goal, this, _1, _2),
        std::bind(&WaypointActionServer::handle_cancel, this, _1),
        std::bind(&WaypointActionServer::handle_accepted, this, _1));

    // topics
    pub_cmd_vel_ = this->create_publisher<geometry_msgs::msg::Twist>(
        "/fastbot/cmd_vel", 1);
    sub_odom_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/fastbot/odom", 1,
        std::bind(&WaypointActionServer::clbk_odom, this, _1));
    RCLCPP_INFO(this->get_logger(), "Action server started");
  }

private:
  rclcpp_action::Server<WaypointAction>::SharedPtr action_server_;

  // topics
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr pub_cmd_vel_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr sub_odom_;

  // go to point vars
  // robot state variables (written by odom callback, read by execute)
  std::mutex state_mutex_;
  geometry_msgs::msg::Point position_;
  double yaw_ = 0.0;

  // machine state
  std::string state_ = "idle";
  
  // goal
  geometry_msgs::msg::Point des_pos_;
  
  // parameters
  const double yaw_precision_ = M_PI / 90; // +/- 2 degree allowed
  const double dist_precision_ = 0.05;
  
  // FastBot drives along the -y axis of fastbot_base_link (camera faces -y)
  // Heading is odom yaw minus 90 degrees
  const double heading_offset_ = -M_PI / 2;

  void clbk_odom(const nav_msgs::msg::Odometry::SharedPtr msg) {
    // yaw
    tf2::Quaternion quaternion(
        msg->pose.pose.orientation.x, msg->pose.pose.orientation.y,
        msg->pose.pose.orientation.z, msg->pose.pose.orientation.w);
    double roll, pitch, yaw;
    tf2::Matrix3x3(quaternion).getRPY(roll, pitch, yaw);

    std::lock_guard<std::mutex> lock(state_mutex_);

    // position
    position_ = msg->pose.pose.position;
    yaw_ = std::atan2(std::sin(yaw + heading_offset_),
                      std::cos(yaw + heading_offset_));
  }

  void get_state(geometry_msgs::msg::Point &position, double &yaw) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    position = position_;
    yaw = yaw_;
  }

  rclcpp_action::GoalResponse
  handle_goal(const rclcpp_action::GoalUUID &uuid,
              std::shared_ptr<const WaypointAction::Goal> goal) {
    RCLCPP_INFO(this->get_logger(), "goal [%f, %f] received",
                goal->position.x, goal->position.y);
                
    (void)uuid;
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse
  handle_cancel(const std::shared_ptr<GoalHandleWaypointAction> goal_handle) {
    RCLCPP_INFO(this->get_logger(), "Received request to cancel goal");

    (void)goal_handle;
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handle_accepted(
      const std::shared_ptr<GoalHandleWaypointAction> goal_handle) {
    using namespace std::placeholders;
    // spin up new thread to avoid blocking the executor
    std::thread{std::bind(&WaypointActionServer::execute, this, _1),
                goal_handle}
        .detach();
  }

  void execute(const std::shared_ptr<GoalHandleWaypointAction> goal_handle) {
    const auto goal = goal_handle->get_goal();
    auto feedback = std::make_shared<WaypointAction::Feedback>();
    auto result = std::make_shared<WaypointAction::Result>();

    // loop rate
    rclcpp::Rate rate(25);

    // helper variables
    bool success = true;

    // define desired position and errors
    geometry_msgs::msg::Point position;
    double yaw;
    get_state(position, yaw);

    des_pos_ = goal->position;
    double desired_yaw =
        std::atan2(des_pos_.y - position.y, des_pos_.x - position.x);
    
        double err_pos = std::sqrt(std::pow(des_pos_.y - position.y, 2) +
                               std::pow(des_pos_.x - position.x, 2));
    double err_yaw = desired_yaw - yaw;

    // perform task
    while (err_pos > dist_precision_ && success && rclcpp::ok()) {
      // update vars
      get_state(position, yaw);
      desired_yaw = std::atan2(des_pos_.y - position.y, des_pos_.x - position.x);
      
      err_yaw = desired_yaw - yaw;
      err_pos = std::sqrt(std::pow(des_pos_.y - position.y, 2) +
                          std::pow(des_pos_.x - position.x, 2));
      
      RCLCPP_INFO(this->get_logger(), "Current Yaw: %f", yaw);
      RCLCPP_INFO(this->get_logger(), "Desired Yaw: %f", desired_yaw);
      RCLCPP_INFO(this->get_logger(), "Error Yaw: %f", err_yaw);
      
      geometry_msgs::msg::Twist twist_msg;
      if (goal_handle->is_canceling()) {
        // cancelled
        RCLCPP_INFO(this->get_logger(), "The goal has been cancelled/preempted");
        result->success = false;
        goal_handle->canceled(result);
        success = false;
        break;
      } else if (std::fabs(err_yaw) > yaw_precision_) {
        // fix yaw
        RCLCPP_INFO(this->get_logger(), "fix yaw");
        state_ = "fix yaw";
        twist_msg.angular.z = err_yaw > 0 ? 0.65 : -0.65;
        pub_cmd_vel_->publish(twist_msg);
      } else {
        // go to point
        RCLCPP_INFO(this->get_logger(), "go to point");
        state_ = "go to point";

        twist_msg.linear.x = 0.2;
        twist_msg.angular.z = 0;
        pub_cmd_vel_->publish(twist_msg);
      }

      // send feedback
      feedback->position = position;
      feedback->state = state_;
      goal_handle->publish_feedback(feedback);

      rate.sleep();
    }

    // stop
    geometry_msgs::msg::Twist twist_msg;
    twist_msg.linear.x = 0;
    twist_msg.angular.z = 0;
    pub_cmd_vel_->publish(twist_msg);

    // return success
    if (success && rclcpp::ok()) {
      result->success = true;
      goal_handle->succeed(result);
      RCLCPP_INFO(this->get_logger(), "Goal succeeded");
    }
  }
}; // class WaypointActionServer

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);

  auto action_server = std::make_shared<WaypointActionServer>();

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(action_server);
  executor.spin();

  rclcpp::shutdown();
  return 0;
}
