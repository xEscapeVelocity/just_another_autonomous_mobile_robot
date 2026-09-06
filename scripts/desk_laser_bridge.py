#!/usr/bin/env python3
import socket
import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Range, LaserScan
from tf2_ros import TransformBroadcaster
from geometry_msgs.msg import TransformStamped

class DeskLaserBridge(Node):
    def __init__(self):
        super().__init__('desk_laser_bridge')
        
        # ROS 2 Publishers
        self.pub_range = self.create_publisher(Range, '/desk_laser', 10)
        self.pub_scan  = self.create_publisher(LaserScan, '/desk_scan', 10)
        self.tf_broadcaster = TransformBroadcaster(self)

        # UDP Socket listening on port 8890
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('0.0.0.0', 8890))
        self.sock.setblocking(False)

        self.last_dist = 0.5
        self.timer = self.create_timer(0.02, self.timer_callback) # 50Hz poll
        self.get_logger().info("🎯 Live Desk Laser Bridge is ONLINE! Waiting for ESP32 on UDP 8890...")

    def timer_callback(self):
        now = self.get_clock().now().to_msg()

        # 1. Broadcast Coordinate Frame: world -> laser_frame
        t = TransformStamped()
        t.header.stamp = now
        t.header.frame_id = 'world'
        t.child_frame_id = 'laser_frame'
        t.transform.translation.x = 0.0
        t.transform.translation.y = 0.0
        t.transform.translation.z = 0.1
        t.transform.rotation.w = 1.0
        self.tf_broadcaster.sendTransform(t)

        # 2. Receive UDP distance packets
        try:
            while True:
                data, _ = self.sock.recvfrom(1024)
                text = data.decode('utf-8', errors='ignore').strip()
                if text.startswith("DIST:"):
                    self.last_dist = float(text.split(":")[1])
                    mm = int(self.last_dist * 1000)
                    
                    # Create a simple visual ASCII bar in terminal
                    bar_len = min(max(int(mm / 25), 1), 40)
                    bar = "█" * bar_len
                    print(f"\r🎯 Distance: {mm:4d} mm | {bar:<40} (Visualizing in RViz)", end='', flush=True)

                    # Publish Range Message (Cone in RViz)
                    range_msg = Range()
                    range_msg.header.stamp = now
                    range_msg.header.frame_id = 'laser_frame'
                    range_msg.radiation_type = Range.INFRARED
                    range_msg.field_of_view = 0.436 # ~25 degrees
                    range_msg.min_range = 0.03
                    range_msg.max_range = 2.0
                    range_msg.range = self.last_dist
                    self.pub_range.publish(range_msg)

                    # Publish LaserScan Message (Point in RViz)
                    scan_msg = LaserScan()
                    scan_msg.header.stamp = now
                    scan_msg.header.frame_id = 'laser_frame'
                    scan_msg.angle_min = -0.05
                    scan_msg.angle_max = 0.05
                    scan_msg.angle_increment = 0.05
                    scan_msg.time_increment = 0.0
                    scan_msg.scan_time = 0.05
                    scan_msg.range_min = 0.03
                    scan_msg.range_max = 2.0
                    scan_msg.ranges = [self.last_dist, self.last_dist, self.last_dist]
                    self.pub_scan.publish(scan_msg)

        except BlockingIOError:
            pass

def main(args=None):
    rclpy.init(args=args)
    node = DeskLaserBridge()
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
