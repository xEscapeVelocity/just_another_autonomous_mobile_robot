#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
import socket
import select

class WirelessBridgeNode(Node):
    def __init__(self):
        super().__init__('wireless_bridge')

        self.declare_parameter('port', 8888)
        port = self.get_parameter('port').get_parameter_value().integer_value

        # Setup UDP socket to receive from ESP32 Handheld Remote
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(('0.0.0.0', port))
        self.sock.setblocking(False)

        self.cmd_vel_pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.timer = self.create_timer(0.02, self.timer_callback) # 50 Hz

        self.get_logger().info(f"Wireless UDP Bridge listening on UDP port {port}...")

    def timer_callback(self):
        # Poll UDP socket non-blockingly
        readable, _, _ = select.select([self.sock], [], [], 0.0)
        if readable:
            try:
                data, addr = self.sock.recvfrom(256)
                msg_str = data.decode('utf-8', errors='ignore').strip()
                if "CMD," in msg_str:
                    cmd_part = msg_str[msg_str.find("CMD,"):]
                    parts = cmd_part.split(',')
                    if len(parts) >= 3:
                        linear_x = float(parts[1])
                        angular_z = float(parts[2])

                        twist = Twist()
                        twist.linear.x = linear_x
                        twist.angular.z = angular_z
                        self.cmd_vel_pub.publish(twist)
            except Exception as e:
                self.get_logger().warn(f"UDP read error: {e}")

    def destroy_node(self):
        self.sock.close()
        super().destroy_node()

def main(args=None):
    rclpy.init(args=args)
    node = WirelessBridgeNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
