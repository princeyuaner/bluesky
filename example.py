import socket
 
# 创建一个TCP/IP socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
 
# 连接到目标主机和端口
server_address = ('192.168.115.130', 7878)

sock.connect(server_address)
 
# 发送数据
message = 'Hello, server!'
sock.sendall(message.encode())

while True:
    pass