#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
import time
import sys
import glob

# Robust Serial communication support (pyserial or built-in POSIX termios)
try:
    import serial
    HAVE_PYSERIAL = True
except ImportError:
    HAVE_PYSERIAL = False
    import os
    import termios
    import tty
    import select

class SimplePosixSerial:
    """Zero-dependency fallback serial interface using standard Linux termios."""
    def __init__(self, port, baudrate=115200, timeout=0.1):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        
        # Configure baud rate and raw mode
        baud_dict = {
            115200: termios.B115200,
            57600: termios.B57600,
            9600: termios.B9600
        }
        baud = baud_dict.get(baudrate, termios.B115200)
        
        attr = termios.tcgetattr(self.fd)
        attr[4] = baud # ispeed
        attr[5] = baud # ospeed
        attr[0] = 0    # iflag
        attr[1] = 0    # oflag
        attr[2] = termios.CS8 | termios.CREAD | termios.CLOCAL # cflag
        attr[3] = 0    # lflag (raw mode)
        termios.tcsetattr(self.fd, termios.TCSANOW, attr)
        self.buffer = b""

    def write(self, data: bytes):
        os.write(self.fd, data)

    def readline(self) -> bytes:
        while b'\n' not in self.buffer:
            r, _, _ = select.select([self.fd], [], [], self.timeout)
            if not r:
                return b""
            chunk = os.read(self.fd, 256)
            if not chunk:
                break
            self.buffer += chunk
        
        if b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            return line + b'\n'
        return b""

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


class ESP32BridgeNode(Node):
    def __init__(self):
        super().__init__('esp32_bridge')
        
        self.declare_parameter('port', '/dev/ttyUSB0')
        self.declare_parameter('baudrate', 115200)
        
        port = self.get_parameter('port').get_parameter_value().string_value
        baudrate = self.get_parameter('baudrate').get_parameter_value().integer_value
        
        # Auto-detect port if default not found
        if not glob.glob(port):
            available = glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*')
            if available:
                port = available[0]
                self.get_logger().info(f"Auto-detected ESP32 at: {port}")
            else:
                self.get_logger().warn(f"Port {port} not found. Waiting for ESP32 to be connected...")

        self.port = port
        self.baudrate = baudrate
        self.ser = None
        self.connect_serial()

        # Publisher to /cmd_vel (Touch input -> Gazebo)
        self.cmd_vel_pub = self.create_publisher(Twist, '/cmd_vel', 10)
        
        # Subscriber to /cmd_vel (Gazebo / Teleop -> ESP32 LEDs)
        self.cmd_vel_sub = self.create_subscription(
            Twist,
            '/cmd_vel',
            self.cmd_vel_callback,
            10
        )

        # Timer for polling Serial (50 Hz)
        self.timer = self.create_timer(0.02, self.timer_callback)
        self.last_pub_time = time.time()
        self.last_linear = 0.0
        self.last_angular = 0.0

    def connect_serial(self):
        try:
            if HAVE_PYSERIAL:
                self.ser = serial.Serial(self.port, self.baudrate, timeout=0.05)
            else:
                self.ser = SimplePosixSerial(self.port, self.baudrate, timeout=0.05)
            self.get_logger().info(f"Successfully connected to ESP32 on {self.port}")
        except Exception as e:
            self.get_logger().warn(f"Could not open {self.port}: {e}. Retrying periodically...")
            self.ser = None

    def cmd_vel_callback(self, msg: Twist):
        # Forward simulation motion telemetry to ESP32 to update Dashboard LEDs
        if self.ser:
            try:
                status_msg = f"STATUS,{msg.linear.x:.2f},{msg.angular.z:.2f}\n"
                self.ser.write(status_msg.encode('utf-8'))
            except Exception as e:
                self.get_logger().warn(f"Serial write error: {e}")
                self.ser = None

    def timer_callback(self):
        if not self.ser:
            # Try reconnecting if port became available
            available = glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*')
            if available:
                self.port = available[0]
                self.connect_serial()
            return

        try:
            line = self.ser.readline().decode('utf-8', errors='ignore').strip()
            if line.startswith("CMD,"):
                parts = line.split(',')
                if len(parts) >= 3:
                    linear_x = float(parts[1])
                    angular_z = float(parts[2])
                    
                    # Create Twist msg and publish
                    twist = Twist()
                    twist.linear.x = linear_x
                    twist.angular.z = angular_z
                    self.cmd_vel_pub.publish(twist)
        except Exception as e:
            self.get_logger().warn(f"Serial read error: {e}")
            self.ser = None


def main(args=None):
    rclpy.init(args=args)
    node = ESP32BridgeNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        if node.ser:
            node.ser.close()
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
