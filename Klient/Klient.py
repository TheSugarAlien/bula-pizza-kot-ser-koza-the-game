import socket

SERVER_IP = "127.0.0.1"  # Adres IP serwera
SERVER_PORT = 1100  # Port serwera


def main():
    try:
        client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_socket.connect((SERVER_IP, SERVER_PORT))
        print(f"Połączono z serwerem udane")

        id_response = client_socket.recv(1024).decode()
        print(f"Twoje ID to: {id_response}")

        while True:
            message = input("Wpisz wiadomość do serwera (lub 'exit' aby zakończyć): ")
            if message.lower() == "exit":
                print("Zakończono połączenie.")
                break

            client_socket.sendall(message.encode())

            response = client_socket.recv(1024).decode()
            print(f"Odpowiedź serwera: {response}")

    except ConnectionRefusedError:
        print("Nie udało się połączyć z serwerem.")
    except Exception as e:
        print(f"Wystąpił błąd: {e}")
    finally:
        client_socket.close()


if __name__ == "__main__":
    main()
