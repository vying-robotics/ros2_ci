#include "waypoints_test_fixture.hpp"

// Allowed error for the end pose, tighter than the action server can reach
// (it stops within 0.05 m and 2 degrees), so both tests fail
constexpr double XY_TOLERANCE = 0.001;
constexpr double YAW_TOLERANCE = 0.001;

TEST_F(WaypointsTestFixture, EndPositionTest) {
  check_end_position(XY_TOLERANCE);
}

TEST_F(WaypointsTestFixture, EndYawTest) {
  check_end_yaw(YAW_TOLERANCE);
}
