import os

from ament_index_python.packages import get_package_share_directory

from launch import LaunchDescription
from launch.actions import IncludeLaunchDescription, DeclareLaunchArgument
from launch.launch_description_sources import PythonLaunchDescriptionSource
from launch.substitutions import LaunchConfiguration

from launch_ros.actions import Node


def generate_launch_description():

    package_name = 'just_another_autonomous_mobile_robot'

    # Include the robot_state_publisher launch file, force use_sim_time to true
    rsp = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([os.path.join(
            get_package_share_directory(package_name), 'launch', 'rsp.launch.py'
        )]),
        launch_arguments={'use_sim_time': 'true'}.items()
    )

    # Path to our obstacles world
    world_path = os.path.join(
        get_package_share_directory(package_name),
        'worlds',
        'obstacles.sdf'
    )

    # Include the Gazebo Sim launch file from ros_gz_sim with obstacles.sdf world
    gazebo = IncludeLaunchDescription(
        PythonLaunchDescriptionSource([os.path.join(
            get_package_share_directory('ros_gz_sim'), 'launch', 'gz_sim.launch.py'
        )]),
        launch_arguments={'gz_args': ['-r ', world_path]}.items()
    )

    # Run the spawner node from the ros_gz_sim package to spawn from /robot_description
    spawn_entity = Node(
        package='ros_gz_sim',
        executable='create',
        arguments=['-topic', 'robot_description',
                   '-name', 'my_bot',
                   '-z', '0.1'],
        output='screen'
    )

    # Bridge ROS 2 and Gazebo topics (including 2D LaserScan /scan)
    bridge = Node(
        package='ros_gz_bridge',
        executable='parameter_bridge',
        arguments=[
            '/cmd_vel@geometry_msgs/msg/Twist@ignition.msgs.Twist',
            '/odom@nav_msgs/msg/Odometry@ignition.msgs.Odometry',
            '/joint_states@sensor_msgs/msg/JointState@ignition.msgs.Model',
            '/clock@rosgraph_msgs/msg/Clock[ignition.msgs.Clock',
            '/scan@sensor_msgs/msg/LaserScan[ignition.msgs.LaserScan',
            '/tf@tf2_msgs/msg/TFMessage[ignition.msgs.Pose_V',
        ],
        output='screen'
    )

    return LaunchDescription([
        rsp,
        gazebo,
        spawn_entity,
        bridge,
    ])
