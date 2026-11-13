# ROS 2 Humble + Gazebo, FastBot simulation and fastbot_waypoints (Checkpoint 23)
# Based on Jenkins Basics for Robotics, Lesson 10.3 Create a Docker image
# Build from ~/ros2_ws/src: docker build -f ros2_ci/Dockerfile -t ros2_ci .
FROM osrf/ros:humble-desktop

# Tell container to use C.UTF-8 locale for its language settings
ENV LANG=C.UTF-8

# Print Python (ROS) output right away, so Jenkins console shows it in order
ENV PYTHONUNBUFFERED=1

RUN echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections

# Install required packages (Gazebo 11 ROS packages for FastBot simulation)
RUN set -x \
    && apt-get update \
    && apt-get install -y ros-humble-gazebo-ros-pkgs ros-humble-robot-state-publisher ros-humble-xacro \
    && rm -rf /var/lib/apt/lists/*

# Set up ROS 2 workspace
WORKDIR /
RUN mkdir -p /ros2_ws/src
WORKDIR /ros2_ws/src

# FastBot simulation packages and waypoints packages from Checkpoint 23
COPY ./fastbot/fastbot_description /ros2_ws/src/fastbot_description
COPY ./fastbot/fastbot_gazebo /ros2_ws/src/fastbot_gazebo
COPY ./fastbot_waypoints_msgs /ros2_ws/src/fastbot_waypoints_msgs
COPY ./fastbot_waypoints /ros2_ws/src/fastbot_waypoints

# build
WORKDIR /ros2_ws
RUN /bin/bash -c "source /opt/ros/humble/setup.bash && colcon build"

# Gazebo looks for robot meshes in ~/.gazebo/models
# (launch file sets GAZEBO_MODEL_PATH to fastbot_gazebo/models only)
RUN mkdir -p /root/.gazebo/models \
    && ln -s /ros2_ws/install/fastbot_description/share/fastbot_description /root/.gazebo/models/fastbot_description

# replace setup.bash in ros_entrypoint.sh
RUN sed -i 's|source "/opt/ros/\$ROS_DISTRO/setup.bash"|source "/ros2_ws/install/setup.bash"|g' /ros_entrypoint.sh

# Script that starts simulation and runs waypoints tests
COPY ./ros2_ci/run_waypoints_test.sh /run_waypoints_test.sh
RUN chmod +x /run_waypoints_test.sh

# Cleanup
RUN rm -rf /root/.cache

# Start a bash shell when container starts
CMD ["bash"]
