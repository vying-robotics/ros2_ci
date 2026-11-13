#!/bin/bash
# Runs inside ros2_ci Docker container (see Dockerfile and Jenkinsfile)
# Based on Jenkins Basics for Robotics, Lesson 7.3 Using Git Repositories in Jobs and Pipelines
# run_simulation.sh, same steps as ros1_ci/run_waypoints_test.sh

source /ros2_ws/install/setup.bash

# Job control: without it, commands started with & in a script ignore SIGINT (Ctrl+C)
# Each command started with & also gets its own process group, stopped below with kill -INT -- -<PID>
set -m

# Start FastBot room world without Gazebo GUI in the background
# Capture process ID of that launch in SIM_ID
ros2 launch fastbot_gazebo botbox_world_room.launch.xml headless:=1 &
SIM_ID=$!

# Spawn FastBot when Gazebo is ready (one_fastbot_room.launch.py waits 20s instead)
echo "Waiting for Gazebo..."
timeout 120s bash -c 'until ros2 service list 2> /dev/null | grep -q /spawn_entity; do sleep 2; done'
ros2 launch fastbot_gazebo spawn_fastbot.launch.xml &
SPAWN_ID=$!

# Start waypoints action server
ros2 run fastbot_waypoints fastbot_action_server &
SERVER_ID=$!

# Wait for odometry and action server before running the tests
echo "Waiting for the robot and the action server..."
timeout 120s bash -c 'until timeout 5s ros2 topic echo --once /fastbot/odom > /dev/null 2>&1; do sleep 2; done' &&
timeout 60s bash -c 'until ros2 action list 2> /dev/null | grep -q /fastbot_as; do sleep 2; done'
READY=$?

# Run waypoints tests from Checkpoint 23
if [ $READY -eq 0 ]; then
    cd /ros2_ws
    colcon test --packages-select fastbot_waypoints --event-handlers console_direct+
    colcon test-result --all --verbose  # exits with an error if any test failed
    TEST_RESULT=$?
else
    echo "Simulation or action server did not start"
    TEST_RESULT=1
fi

# Stop action server and robot nodes
kill -INT -- -$SERVER_ID -$SPAWN_ID 2> /dev/null
wait $SERVER_ID $SPAWN_ID 2> /dev/null

# Stop simulation and wait for Gazebo to shut down
# Pause physics and stop gzserver before launch (same as ros1_ci)
echo "Stopping Gazebo..."
ros2 service call /pause_physics std_srvs/srv/Empty > /dev/null
pkill -INT gzserver
timeout 30s bash -c 'while pgrep gzserver > /dev/null; do sleep 1; done'

# World launch usually ends by itself once gzserver has stopped
kill -INT -- -$SIM_ID 2> /dev/null
wait $SIM_ID
if pgrep gzserver > /dev/null; then
    echo "Gazebo is still running"
    TEST_RESULT=1
else
    echo "Gazebo shut down"
fi

exit $TEST_RESULT
