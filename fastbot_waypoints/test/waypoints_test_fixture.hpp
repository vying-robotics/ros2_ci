// Shared test fixture for waypoints_passing_test.cpp (passing tolerances) and
// waypoints_failing_test.cpp (tight tolerances)
#pragma once

#include <chrono>
#include <cmath>
#include <memory>

#include "fastbot_waypoints_msgs/action/waypoint_action.hpp"
#include "geometry_msgs/msg/point.hpp"
#include "gtest/gtest.h"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "std_srvs/srv/empty.hpp"
#include "tf2/LinearMath/Matrix3x3.h"
#include "tf2/LinearMath/Quaternion.h"

using std::placeholders::_1;
using namespace std::chrono_literals;

// Robot spawns at [0.5, -0.75] (fastbot_gazebo spawn_fastbot.launch.xml)
// Goal sent to action server (odom frame)
constexpr double GOAL_X = 0.5;
constexpr double GOAL_Y = 0.5;

// Give up on goal after timeout
constexpr auto GOAL_TIMEOUT = 120s;

// FastBot drives along -y axis of fastbot_base_link (its camera faces -y),
// Heading is the odom yaw minus 90 degrees
constexpr double HEADING_OFFSET = -M_PI / 2;

class RclCppFixture {
public:
  RclCppFixture() { rclcpp::init(0, nullptr); }
  ~RclCppFixture() { rclcpp::shutdown(); }
};
RclCppFixture g_rclcppfixture;

class WaypointsTestFixture : public ::testing::Test {
public:
  using WaypointAction = fastbot_waypoints_msgs::action::WaypointAction;
  using GoalHandleWaypointAction = rclcpp_action::ClientGoalHandle<WaypointAction>;

  WaypointsTestFixture() {
    testNode = rclcpp::Node::make_shared("waypoints_test_node");

    odom_subscriber_ = testNode->create_subscription<nav_msgs::msg::Odometry>(
            "/fastbot/odom", 10,
            std::bind(&WaypointsTestFixture::odom_callback, this, _1));

    reset_client_ = testNode->create_client<std_srvs::srv::Empty>(
            "/reset_world");

    action_client_ =
        rclcpp_action::create_client<WaypointAction>(testNode, "fastbot_as");
  }

protected:
  // Before each test:
  // 1. Reset world
  // 2. Send goal
  // 3. Record end pose
  void SetUp() override {
    service_call_reset();
    if (HasFatalFailure()) {
      return;
    }

    get_init_position();
    if (HasFatalFailure()) {
      return;
    }
    
    action_call();
    if (HasFatalFailure()) {
      return;
    }

    // Let robot settle before reading the end pose
    spin_for(1s);
    final_position_ = current_position_;
    final_yaw_ = current_yaw_;
    RCLCPP_INFO(testNode->get_logger(), "Final position: [%.3f, %.3f], yaw: %.3f",
                final_position_.x, final_position_.y, final_yaw_);
  }

  std::shared_ptr<rclcpp::Node> testNode;

  geometry_msgs::msg::Point init_position_;
  geometry_msgs::msg::Point final_position_;
  double init_yaw_ = 0.0;
  double final_yaw_ = 0.0;

  void check_end_position(double xy_tolerance) {
    double error = std::sqrt(std::pow(GOAL_X - final_position_.x, 2) +
                             std::pow(GOAL_Y - final_position_.y, 2));

    RCLCPP_INFO(testNode->get_logger(), "Position error: %.3f m (tolerance %.3f m)",
                error, xy_tolerance);

    EXPECT_LE(error, xy_tolerance)
        << "End position [" << final_position_.x << ", " << final_position_.y
        << "] is not within " << xy_tolerance << " m of the goal [" << GOAL_X
        << ", " << GOAL_Y << "]";
  }

  void check_end_yaw(double yaw_tolerance) {
    // Robot turns to face the goal from start position
    // Then drive straight to it
    double expected_yaw = std::atan2(GOAL_Y - init_position_.y, 
                                      GOAL_X - init_position_.x);
    double error = std::atan2(std::sin(expected_yaw - final_yaw_),
                              std::cos(expected_yaw - final_yaw_));

    RCLCPP_INFO(testNode->get_logger(), "Yaw error: %.3f rad (tolerance %.3f rad)",
                error, yaw_tolerance);
                
    EXPECT_LE(std::fabs(error), yaw_tolerance)
        << "End yaw " << final_yaw_ << " is not within " << yaw_tolerance
        << " rad of the expected yaw " << expected_yaw;
  }

private:
  void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg) {
    current_position_ = msg->pose.pose.position;
    current_yaw_ = quaternion_to_yaw(msg->pose.pose.orientation);

    odom_received_ = true;
  }

  // Robot heading from the odom orientation
  double quaternion_to_yaw(const geometry_msgs::msg::Quaternion &msg) {
    tf2::Quaternion quaternion(msg.x, msg.y, msg.z, msg.w);

    double roll, pitch, yaw;
    tf2::Matrix3x3(quaternion).getRPY(roll, pitch, yaw);
    
    return std::atan2(std::sin(yaw + HEADING_OFFSET),
                      std::cos(yaw + HEADING_OFFSET));
  }

  void spin_for(std::chrono::milliseconds duration) {
    auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
      rclcpp::spin_some(testNode);
      std::this_thread::sleep_for(10ms);
    }
  }

  void service_call_reset() {
    // Start every test from the spawn pose
    ASSERT_TRUE(reset_client_->wait_for_service(10s))
        << "Gazebo /reset_world service is not available";

    auto request = std::make_shared<std_srvs::srv::Empty::Request>();
    auto future = reset_client_->async_send_request(request);

    ASSERT_EQ(rclcpp::spin_until_future_complete(testNode, future, 10s),
              rclcpp::FutureReturnCode::SUCCESS);
    spin_for(2s);
  }

  void get_init_position() {
    odom_received_ = false;
    auto end = std::chrono::steady_clock::now() + 10s;

    while (!odom_received_ && std::chrono::steady_clock::now() < end) {
      rclcpp::spin_some(testNode);
      std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(odom_received_) << "No odometry on /fastbot/odom";

    init_position_ = current_position_;
    init_yaw_ = current_yaw_;
    
    RCLCPP_INFO(testNode->get_logger(),
                "Initial position: [%.3f, %.3f], yaw: %.3f", init_position_.x,
                init_position_.y, init_yaw_);
  }

  void action_call() {
    // Create connection to the action server
    ASSERT_TRUE(action_client_->wait_for_action_server(10s))
        << "Action server fastbot_as is not running";

    // Create goal and send it to the action server
    auto goal_msg = WaypointAction::Goal();
    goal_msg.position.x = GOAL_X;
    goal_msg.position.y = GOAL_Y;

    RCLCPP_INFO(testNode->get_logger(), "Sending goal: [%.3f, %.3f]", 
                GOAL_X, GOAL_Y);
    auto goal_handle_future = action_client_->async_send_goal(goal_msg);
    ASSERT_EQ(rclcpp::spin_until_future_complete(testNode, goal_handle_future, 10s),
              rclcpp::FutureReturnCode::SUCCESS);

    auto goal_handle = goal_handle_future.get();
    ASSERT_TRUE(goal_handle) << "Goal was rejected by the action server";

    // Wait for result. Cancel the goal if it takes too long
    auto result_future = action_client_->async_get_result(goal_handle);
    if (rclcpp::spin_until_future_complete(testNode, result_future, GOAL_TIMEOUT) !=
        rclcpp::FutureReturnCode::SUCCESS) {
      RCLCPP_WARN(testNode->get_logger(),
                  "Goal not reached in time, cancelling it");
                  
      auto cancel_future = action_client_->async_cancel_goal(goal_handle);
      rclcpp::spin_until_future_complete(testNode, cancel_future, 5s);
    }
  }

  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_subscriber_;
  rclcpp::Client<std_srvs::srv::Empty>::SharedPtr reset_client_;
  rclcpp_action::Client<WaypointAction>::SharedPtr action_client_;

  geometry_msgs::msg::Point current_position_;
  double current_yaw_ = 0.0;
  bool odom_received_ = false;
};
