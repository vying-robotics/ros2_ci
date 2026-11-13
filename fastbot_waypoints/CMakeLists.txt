cmake_minimum_required(VERSION 3.8)
project(fastbot_waypoints)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# find dependencies
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(rclcpp_action REQUIRED)
find_package(geometry_msgs REQUIRED)
find_package(nav_msgs REQUIRED)
find_package(tf2 REQUIRED)
find_package(std_srvs REQUIRED)
find_package(fastbot_waypoints_msgs REQUIRED)

add_executable(fastbot_action_server src/fastbot_action_server.cpp)
ament_target_dependencies(fastbot_action_server
  rclcpp rclcpp_action geometry_msgs nav_msgs tf2 fastbot_waypoints_msgs)

install(TARGETS
  fastbot_action_server
  DESTINATION lib/${PROJECT_NAME}
)

if(BUILD_TESTING)
  # Only GTest node level tests
  find_package(ament_cmake_gtest REQUIRED)
  # Build with --cmake-args -DWAYPOINTS_FAIL_TEST=ON to run the failing tests
  # (tight tolerances) instead of the passing ones
  option(WAYPOINTS_FAIL_TEST "Build the failing waypoints tests" OFF)
  if(WAYPOINTS_FAIL_TEST)
    set(TESTFILES
        test/main.cpp
        test/waypoints_failing_test.cpp
        )
  else()
    set(TESTFILES
        test/main.cpp
        test/waypoints_passing_test.cpp
        )
  endif()

  # Each test drives robot to the goal, need more than 60s default
  ament_add_gtest(${PROJECT_NAME}_test ${TESTFILES} TIMEOUT 300)
  ament_target_dependencies(${PROJECT_NAME}_test
    rclcpp rclcpp_action nav_msgs tf2 std_srvs fastbot_waypoints_msgs)
endif()

ament_package()
