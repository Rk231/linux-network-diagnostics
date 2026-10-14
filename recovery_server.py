
import socket

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", 5999))
    server.listen()

    print("Recovery test server listening on port 5999")

    while True:
        client, address = server.accept()
        with client:
            client.recv(1024)
            client.sendall(b"OK: diagnostic server reachable\n")

