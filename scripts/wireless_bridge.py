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

        self.remote_port = self.get_parameter('remote_port').get_parameter_value().integer_value
        self.robot_port = self.get_parameter('robot_port').get_parameter_value().integer_value

        # UDP socket on port 8888 (Remote Receiver)
        self.rx_remote_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.rx_remote_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.rx_remote_sock.bind(('0.0.0.0', self.remote_port))
        self.rx_remote_sock.setblocking(False)

        # UDP socket on port 8889 (Robot Receiver & Broadcaster)
        self.robot_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.robot_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.robot_sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.robot_sock.bind(('0.0.0.0', self.robot_port))
        self.robot_sock.setblocking(False)

        self.discovered_robot_ip = None

        # ROS 2 Pub/Sub
        self.cmd_vel_pub = self.create_publisher(Twist, '/cmd_vel', 10)
        self.cmd_vel_sub = self.create_subscription(Twist, '/cmd_vel', self.cmd_vel_callback, 10)

        self.timer = self.create_timer(0.02, self.timer_callback) # 50 Hz

        self.get_logger().info(f"Wireless Bridge Active:")
        self.get_logger().info(f" -> Listening for Remote on UDP port {self.remote_port}")
        self.get_logger().info(f" -> Listening for Robot Heartbeat on UDP port {self.robot_port}")

    def timer_callback(self):
        # 1. Check for incoming Remote packets (Port 8888)
        readable_remote, _, _ = select.select([self.rx_remote_sock], [], [], 0.0)
        if readable_remote:
            try:
                data, addr = self.rx_remote_sock.recvfrom(256)
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
                self.get_logger().warn(f"Remote RX error: {e}")

        # 2. Check for Robot Heartbeat / Registration (Port 8889)
        readable_robot, _, _ = select.select([self.robot_sock], [], [], 0.0)
        if readable_robot:
            try:
                data, addr = self.robot_sock.recvfrom(256)
                msg_str = data.decode('utf-8', errors='ignore').strip()
                if "HELLO_ROBOT" in msg_str:
                    if self.discovered_robot_ip != addr[0]:
                        self.discovered_robot_ip = addr[0]
                        self.get_logger().info(f"🟢 Robot ESP32 Connected! Direct IP: {self.discovered_robot_ip}")
            except Exception as e:
                self.get_logger().warn(f"Robot Discovery error: {e}")

    def cmd_vel_callback(self, msg: Twist):
        packet = f"CMD,{msg.linear.x:.2f},{msg.angular.z:.2f}\n".encode('utf-8')

        # Send via direct unicast if discovered
        if self.discovered_robot_ip:
            try:
                self.robot_sock.sendto(packet, (self.discovered_robot_ip, self.robot_port))
            except Exception as e:
                self.get_logger().warn(f"Unicast send error: {e}")
        else:
            # Fallback to broadcast
            try:
                self.robot_sock.sendto(packet, ('192.168.29.255', self.robot_port))
                self.robot_sock.sendto(packet, ('255.255.255.255', self.robot_port))
            except Exception as e:
                pass

    def destroy_node(self):
        self.rx_remote_sock.close()
        self.robot_sock.close()
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
