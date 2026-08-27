#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
import socket
import select

class WirelessBridgeNode(Node):
    def __init__(self):
        super().__init__('wireless_bridge')

        self.declare_parameter('remote_port', 8888)
        self.declare_parameter('robot_port', 8889)
        self.declare_parameter('broadcast_ip', '192.168.29.255')

        remote_port = self.get_parameter('remote_port').get_parameter_value().integer_value
        self.robot_port = self.get_parameter('robot_port').get_parameter_value().integer_value
        self.broadcast_ip = self.get_parameter('broadcast_ip').get_parameter_value().string_value

        # 1. Setup UDP socket to receive from ESP32 Handheld Remote (Port 8888)
        self.rx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.rx_sock.bind(('0.0.0.0', remote_port))
        self.rx_sock.setblocking(False)

        # 2. Setup UDP socket to broadcast /cmd_vel to ESP32 Robot Controller (Port 8889)
        self.tx_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.tx_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

        # ROS 2 Pub/Sub
        self.cmd_vel_pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.cmd_vel_sub = self.create_subscription(Twist, '/cmd_vel', self.cmd_vel_callback, 10)

        self.timer = self.create_timer(0.02, self.timer_callback) # 50 Hz

        self.get_logger().info(f"Wireless Bridge Active:")
        self.get_logger().info(f" -> Listening for Handheld Remote on UDP {remote_port}")
        self.get_logger().info(f" -> Broadcasting /cmd_vel to Robot Controller on {self.broadcast_ip}:{self.robot_port}")

    def timer_callback(self):
        # Poll UDP socket for commands from Remote
        readable, _, _ = select.select([self.rx_sock], [], [], 0.0)
        if readable:
            try:
                data, addr = self.rx_sock.recvfrom(256)
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
                self.get_logger().warn(f"UDP RX error: {e}")

    def cmd_vel_callback(self, msg: Twist):
        # Forward any /cmd_vel (from Remote OR autonomous Nav2 stack) to the physical Robot ESP32
        try:
            packet = f"CMD,{msg.linear.x:.2f},{msg.angular.z:.2f}\n".encode('utf-8')
            self.tx_sock.sendto(packet, (self.broadcast_ip, self.robot_port))
        except Exception as e:
            self.get_logger().warn(f"UDP TX error: {e}")

    def destroy_node(self):
        self.rx_sock.close()
        self.tx_sock.close()
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
