import socket
import struct


class RawRedisClient:
    """原始 RESP 协议客户端，精确控制每个字节"""
    
    def __init__(self, host='localhost', port=6379):
        self.host = host
        self.port = port
        self.sock = None
    
    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        return self
    
    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None
    
    def encode_bulk_string(self, s):
        """编码 Bulk String: $长度\r\n内容\r\n"""
        # 确保是字符串，计算字节长度（UTF-8编码）
        if isinstance(s, bytes):
            data = s
        else:
            data = s.encode('utf-8')
        
        length = len(data)
        # 构造: $长度\r\n数据\r\n
        return f"${length}\r\n".encode('utf-8') + data + b"\r\n"
    
    def encode_array(self, *args):
        """编码 RESP 数组: *数量\r\n$长度\r\n内容\r\n..."""
        # *数量\r\n
        result = f"*{len(args)}\r\n".encode('utf-8')
        
        # 每个参数作为 Bulk String
        for arg in args:
            result += self.encode_bulk_string(arg)
        
        return result
    
    def send_raw(self, data_bytes):
        """发送原始字节，确保完全发送"""
        total_sent = 0
        while total_sent < len(data_bytes):
            sent = self.sock.send(data_bytes[total_sent:])
            if sent == 0:
                raise ConnectionError("Socket connection broken")
            total_sent += sent
        return total_sent
    
    def recv_response(self):
        """接收并解析 RESP 响应（简单实现）"""
        # 先读取第一个字节判断类型
        type_byte = self.sock.recv(1)
        if not type_byte:
            return None
        
        # 读取到 \r\n 结束的第一行
        line = type_byte
        while not line.endswith(b'\r\n'):
            chunk = self.sock.recv(1)
            if not chunk:
                break
            line += chunk
        
        resp_type = chr(line[0])
        content = line[1:-2].decode('utf-8')  # 去掉类型和\r\n
        
        if resp_type == '+':  # Simple String
            return ("simple_string", content)
        elif resp_type == '-':  # Error
            return ("error", content)
        elif resp_type == ':':  # Integer
            return ("integer", int(content))
        elif resp_type == '$':  # Bulk String
            length = int(content)
            if length == -1:
                return ("bulk_string", None)  # null
            # 读取 length + 2 (\r\n)
            data = b''
            while len(data) < length + 2:
                data += self.sock.recv(length + 2 - len(data))
            return ("bulk_string", data[:-2].decode('utf-8'))
        elif resp_type == '*':  # Array
            count = int(content)
            if count == -1:
                return ("array", None)
            elements = []
            for _ in range(count):
                elements.append(self.recv_response())
            return ("array", elements)
        
        return ("unknown", line)
    
    def execute(self, *args):
        """执行命令，args 为命令和参数"""
        # 构造 RESP 数组
        data = self.encode_array(*args)
        
        # 打印即将发送的原始字节（调试用）
        print("Sending raw bytes:")
        print(repr(data))
        print("Hex dump:")
        print(data.hex(' '))
        
        # 发送
        self.send_raw(data)
        
        # 接收响应
        return self.recv_response()


# ============ 使用示例 ============

if __name__ == "__main__":
    client = RawRedisClient('localhost', 9999)
    client.connect()
    
    try:
      
        print("=== HSET key value ===")
        result = client.execute("HGET", "ke" * 2048)
        print(f"Response: {result}\n")
       

               
    finally:
        client.close()
