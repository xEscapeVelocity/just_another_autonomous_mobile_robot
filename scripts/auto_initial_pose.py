#!/usr/bin/env python3
import time
import rclpy
from rclpy.node import Node
from rosgraph_msgs.msg import Clock
from geometry_msgs.msg import PoseWithCovarianceStamped
from std_srvs.srv import Empty

class AutoPoseInitializer(Node):
    def __init__(self):
        super().__init__('auto_pose_initializer')
        self.pub = self.create_publisher(PoseWithCovarianceStamped, '/initialpose', 10)
        self.sub_clock = self.create_subscription(Clock, '/clock', self.clock_callback, 10)
        self.nomotion_client = self.create_client(Empty, '/request_nomotion_update')
        
        self.current_clock = None
        self.published_count = 0
        self.timer = self.create_timer(1.0, self.timer_callback)
        self.get_logger().info("Auto Pose Initializer started. Waiting for /clock and AMCL...")

    def clock_callback(self, msg: Clock):
        self.current_clock = msg.clock

    def timer_callback(self):
        if self.current_clock is None:
            return

        if self.published_count < 3:
            pose_msg = PoseWithCovarianceStamped()
            pose_msg.header.stamp = self.current_clock
            pose_msg.header.frame_id = 'map'
            pose_msg.pose.pose.position.x = 0.0
            pose_msg.pose.pose.position.y = 0.0
            pose_msg.pose.pose.position.z = 0.0
            pose_msg.pose.pose.orientation.w = 1.0
            pose_msg.pose.covariance[0] = 0.25
            pose_msg.pose.covariance[7] = 0.25
            pose_msg.pose.covariance[35] = 0.068

            self.pub.publish(pose_msg)
            self.published_count += 1
            self.get_logger().info(f"Broadcasted initial pose estimate at sim time {self.current_clock.sec}s (Attempt {self.published_count}/3)")

            if self.nomotion_client.service_is_ready():
                req = Empty.Request()
                self.nomotion_client.call_async(req)
        else:
            self.get_logger().info("Initial pose established successfully.")
            self.timer.cancel()

def main(args=None):
    rclpy.init(args=args)
    node = AutoPoseInitializer()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()

if __name__ == '__main__':
    main()
