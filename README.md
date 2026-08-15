# just_another_autonomous_mobile_robot

An Autonomous Mobile Robot (AMR) developed with **ROS 2 Humble** and **Modern Gazebo (Fortress / `gz sim`)**.

## Features & Implemented Milestones

- **URDF / Xacro Robot Model:**
  - Parametric chassis, differential drive wheels, and low-friction caster wheel.
  - Calculated mass properties and inertia tensors (`inertial_macros.xacro`).
  - Standard REP-120 compliant coordinate frames (`base_link`, `base_footprint`, `chassis`, `left_wheel`, `right_wheel`, `caster_wheel`).
- **Modern Gazebo Simulation:**
  - Physics simulation with `ignition-gazebo-diff-drive-system` and `ignition-gazebo-joint-state-publisher-system`.
  - Spawner node integration with `ros_gz_sim`.
- **ROS 2 Control & Bridge:**
  - Topic bridging via `ros_gz_bridge` for `/cmd_vel`, `/odom`, `/joint_states`, and `/clock`.
  - Differential drive keyboard teleoperation support.

## How to Build & Run

### 1. Build the Workspace
```bash
cd ~/dev_ws
source /opt/ros/humble/setup.bash
colcon build --symlink-install
```

### 2. Launch Simulation in Gazebo
```bash
source install/setup.bash
ros2 launch just_another_autonomous_mobile_robot launch_sim.launch.py
```

### 3. Teleoperation (Drive with Keyboard)
In a second terminal:
```bash
source /opt/ros/humble/setup.bash
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```