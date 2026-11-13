#include "waypoints_test_fixture.hpp"

// Allowed error for the end pose
constexpr double XY_TOLERANCE = 0.1;
constexpr double YAW_TOLERANCE = 0.3;

TEST_F(WaypointsTestFixture, EndPositionTest) {
  check_end_position(XY_TOLERANCE);
}

TEST_F(WaypointsTestFixture, EndYawTest) {
  check_end_yaw(YAW_TOLERANCE);
}
