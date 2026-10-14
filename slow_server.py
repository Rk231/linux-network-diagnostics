import socket
import threading
import time

HOST = "127.0.0.1"
PORT = 5001

def handle_client(conn, addr):
    with conn:
        print(f"{addr} connected; waiting 10 seconds...")
        try:
            conn.recv(1024)
            time.sleep(10)
            conn.sendall(b"OK: diagnostic server reachable\n")
        except OSError as exc:
            print(f"{addr}: {exc}")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, PORT))
    server.listen()

    print(f"Concurrent slow server listening on {HOST}:{PORT}")

    while True:
        conn, addr = server.accept()
        threading.Thread(
            target=handle_client,
            args=(conn, addr),
            daemon=True
        ).start()