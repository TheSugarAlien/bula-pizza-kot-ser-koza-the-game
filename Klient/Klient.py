import socket
import threading
import tkinter as tk

SERVER_IP = "127.0.0.1"
SERVER_PORT = 1100

 # Blokada do synchronizacji dostępu do zmiennych
can_click_stack = False  # Flaga, czy gracz może dotknąć stosu
game_in_progress = False  # Flaga, czy gracz jest w grze
client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

def handle_server_messages():
    global can_click_stack
    buffer = ""
    try:
        while True:
            data = client_socket.recv(1024).decode()
            if not data:
                break
            buffer += data

            #Przetwarzanie wiadomości
            while "|" in buffer:
                message, buffer = buffer.split("|", 1)
                print(f"[DEBUG] Otrzymana wiadomość: {message}")

                if message.startswith("LISTA_GRACZY"):
                    gracze = message.split(":")[1].split(",")
                    lista_graczy.delete(0, tk.END)
                    for gracz in gracze:
                        lista_graczy.insert(tk.END, gracz.strip())
                elif message.startswith("CARDS_COUNT"):
                    liczba_kart = message.split(":")[1]
                    number_of_cards.config(text=f"{liczba_kart}")
                elif message.startswith("AKTUALNA_RYMOWANKA"):
                    aktualna_rymowanka = message.split(":")[1].strip()
                    label_rymowanka.config(text=aktualna_rymowanka)
                elif message.startswith("AKTUALNA_KARTA"):
                    aktualna_karta = message.split(":")[1].strip()
                    label_karta.config(text=aktualna_karta)
                elif message.startswith("SERWER"):
                    server_text = message.split(":")[1].strip()
                    server_prompt.config(text=server_text)
                elif message.startswith("SERWER:Gratulacje"):
                    server_text = message.split(":")[1].strip()
                    server_prompt.config(text=server_text)
                    lista_graczy.delete(0, tk.END)
                    number_of_cards.config(text="...")
                    label_karta.config(text="...")
                    label_rymowanka.config(text="...")
                else:
                    print(f"\nSerwer: {message}")
    except Exception as e:
        print(f"Utracono połączenie z serwerem: {e}")
    finally:
        client_socket.close()



def otworz_okno_dolaczenia():
    global game_in_progress, dolacz_button, wyjdz_button, client_socket
    okno_dolaczenia = tk.Toplevel(root)
    okno_dolaczenia.title("Dołącz do gry")
    okno_dolaczenia.geometry("300x150")

    tk.Label(okno_dolaczenia, text="Podaj nazwę gracza:").pack(pady=10)

    entry_nazwa_gracza = tk.Entry(okno_dolaczenia, width=25)
    entry_nazwa_gracza.pack(pady=5)

    def graj():
        global client_socket
        nazwa = entry_nazwa_gracza.get().strip()
        if not nazwa:
            server_prompt.config(text="Nazwa gracza nie może być pusta!")
            return
        
        try:
            if client_socket:
                client_socket.close()
                client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        except Exception as e:
            print(f"Błąd podczas resetowania gniazda: {e}")

        try:
            client_socket.connect((SERVER_IP, SERVER_PORT))
            print(f"Połączono z serwerem jako {nazwa}.")

            client_socket.sendall(nazwa.encode())

            game_in_progress = True
            dolacz_button.config(state="disabled")
            wyjdz_button.config(state="normal")
            
            okno_dolaczenia.destroy()

            threading.Thread(target=handle_server_messages, daemon=True).start()
        except Exception as e:
            print(f"Nie udało się połączyć z serwerem: {e}")
            server_prompt.config(text=f"Nie udało się połączyć z serwerem!")

    tk.Button(okno_dolaczenia, text="Graj", command=graj).pack(pady=10)


def wyjdz_z_gry():
    global game_in_progress, dolacz_button, wyjdz_button, client_socket

    if client_socket:
        try:
            client_socket.sendall("WYJSCIE".encode())
        except Exception as e:
            print(f"[DEBUG] Nie udało się wysłać komunikatu o wyjściu: {e}")
        try:
            client_socket.shutdown(socket.SHUT_RDWR)
            client_socket.close()
        except Exception as e:
            print(f"[DEBUG] Błąd podczas zamykania gniazda: {e}")
    
    game_in_progress = False
    dolacz_button.config(state="normal")
    wyjdz_button.config(state="disabled")

    lista_graczy.delete(0, tk.END)

    number_of_cards.config(text="...")
    label_karta.config(text="...")
    label_rymowanka.config(text="...")

    server_prompt.config(text="Opuściłeś rozgrywkę")



def zbieraj_karty():
    if client_socket:
        client_socket.sendall("ZAKLEP".encode())
        print("Zaklepujesz stos!")

def zagraj_karte():
    if client_socket:
        client_socket.sendall("ZAGRAJ_KARTE".encode())
        print("Zagrywanie karty...")

# Główne okno
root = tk.Tk()
root.title("Buła Pizza Kot Ser Koza")
root.geometry("450x425")
root.resizable(False, False)

# Przyciski Dołącz do gry i Wyjdź z gry
dolacz_button = tk.Button(root, text="Dołącz", command=otworz_okno_dolaczenia, height=2, width=15)
dolacz_button.grid(row=0, column=0, padx=10, pady=10, sticky="ew")
wyjdz_button = tk.Button(root, text="Wyjdź", command=wyjdz_z_gry, state="disabled", height=2, width=15)
wyjdz_button.grid(row=1, column=0, padx=10, pady=10, sticky="ew")

# Rymowanka
tk.Label(root, text="Rymowanka:", font=("Arial", 12)).grid(row=0, column=1, padx=10, pady=5)
label_rymowanka = tk.Label(root, text="...", font=("Arial", 10))
label_rymowanka.grid(row=1, column=1, padx=10, pady=10, sticky="ew")

# Zagrana karta
tk.Label(root, text="Zagrana karta:", font=("Arial", 12)).grid(row=2, column=1, padx=10, pady=5)
label_karta = tk.Label(root, text="...", font=("Arial", 10))
label_karta.grid(row=3, column=1, padx=10, pady=10, sticky="ew")

# Lista graczy
tk.Label(root, text="Lista graczy", font=("Arial", 12)).grid(row=0, column=2, padx=10, pady=5)
lista_graczy = tk.Listbox(root, height=8, font=("Arial", 10))
lista_graczy.grid(row=1, column=2, rowspan=3, padx=10, pady=10, sticky="ew")

# Przycisk ZBIERAJ
tk.Button(root, text="ZAKLEP STOS", command=zbieraj_karty, bg="red", fg="white", font=("Arial", 12)).grid(row=4, column=1, pady=5, sticky="ew")

# Przycisk zagraj kartę
tk.Button(root, text="Zagraj kartę!!!", command=zagraj_karte, font=("Arial", 12)).grid(row=5, column=1, pady=5, sticky="ew")

# Tekst promptu od serwera
server_prompt = tk.Label(root, text="Tekst promptu od serwera", justify="center", font=("Arial", 10))
server_prompt.grid(row=6, column=0, columnspan=3, padx=10, pady=10, sticky="ew")

# Liczba kart
tk.Label(root, text="Twoje karty:", font=("Arial", 12)).grid(row=7, column=1, pady=5)
number_of_cards = tk.Label(root, text="...", font=("Arial", 16))
number_of_cards.grid(row=8, column=1, pady=10)

# Konfiguracja rozmiarów kolumn i wierszy
rows, columns = 10, 3
for i in range(rows):
    root.grid_rowconfigure(i, weight=1)
for j in range(columns):
    root.grid_columnconfigure(j, weight=1, uniform="col")

root.mainloop()