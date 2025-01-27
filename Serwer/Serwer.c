#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#define MAX_PLAYERS 3
#define MAX_GAMES 10
#define DECK_SIZE 64
int max_players = MAX_PLAYERS;

// Rymowanka
const char *rhyme[] = {"Buła", "Pizza", "Kot", "Ser", "Koza"};
#define RHYME_LENGTH 5

typedef struct
{
    int player_sockets[MAX_PLAYERS];
    char player_names[MAX_PLAYERS][50];
    int player_count;
    char deck[DECK_SIZE][20];
    int deck_index;
    char hands[MAX_PLAYERS][DECK_SIZE][20];
    int hand_sizes[MAX_PLAYERS];
    char stack[DECK_SIZE][20];
    int stack_index;
    int current_turn;
    int rhyme_index;
    pthread_mutex_t lock;
    pthread_mutex_t rhyme_mutex;
    pthread_cond_t rhyme_cond;
    int rhyme_match;
    int zaklepywanie;
    int game_started;
} GameRoom;

typedef struct
{
    GameRoom *room;
    int room_id;
} ThreadArgs;

GameRoom game_rooms[MAX_GAMES];

pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t rhyme_cond = PTHREAD_COND_INITIALIZER;
int rhyme_match = 0;

// Aktualizowanie tury (potrzebne, gdy jednego z graczy brakuje)
void update_turn_after_disconnect(GameRoom *room)
{
    while (room->player_sockets[room->current_turn] <= 0)
    {
        room->current_turn = (room->current_turn + 1) % MAX_PLAYERS;
    }
    printf("[DEBUG] Tura zaktualizowana: current_turn=%d\n", room->current_turn);
}

// Debug tool
void debug_dump_room_state(GameRoom *room, int room_id)
{
    printf("[DEBUG] Stan pokoju %d:\n", room_id);
    printf("  Liczba graczy: %d\n", room->player_count);
    printf("  Tura gracza: %d\n", room->current_turn);
    printf("  Gra rozpoczęta: %d\n", room->game_started);
    printf("  Kolejka graczy i stan socketów:\n");

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        printf("    Gracz %d: %s (socket: %d, karty: %d)\n",
               i, room->player_names[i][0] != '\0' ? room->player_names[i] : "(pusty)",
               room->player_sockets[i],
               room->hand_sizes[i]);
    }
    printf("  Karty na stosie: %d\n", room->stack_index);
    printf("------------------------------------------\n");
}

// Wysyłanie listy graczy do klientów
void broadcast_player_list(GameRoom *room)
{
    char lista[2048] = "LISTA_GRACZY:";
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (room->player_sockets[i] > 0)
        {
            char player_info[100];
            snprintf(player_info, sizeof(player_info), "%s (%d kart)%s",
                     room->player_names[i], room->hand_sizes[i], (i == room->current_turn) ? " *" : "");
            strcat(lista, player_info);
            if (i < MAX_PLAYERS - 1)
                strcat(lista, ", ");
        }
    }
    strcat(lista, "|");

    printf("[DEBUG] Lista graczy w pokoju: %s\n", lista);

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (room->player_sockets[i] > 0)
        {
            send(room->player_sockets[i], lista, strlen(lista), 0);
        }
    }
}

// Wysyłanie ilości kart na ręce dla klienta
void send_cards_count(GameRoom *room, int player_index)
{
    char message[1024];
    snprintf(message, sizeof(message), "CARDS_COUNT:%d|", room->hand_sizes[player_index]);
    if (send(room->player_sockets[player_index], message, strlen(message), 0) < 0)
    {
        perror("[ERROR] Wysyłanie liczby kart nie powiodło się\n");
    }
}

// Wysyłanie wiadomości do klientów
void broadcast_message_to_room(GameRoom *room, const char *message)
{
    printf("[DEBUG] Wysyłanie wiadomości do pokoju: %s\n", message);

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (room->player_sockets[i] >= 0)
        {
            if (send(room->player_sockets[i], message, strlen(message), 0) < 0)
            {
                perror("[ERROR] Nie udało się wysłać wiadomości do gracza");
            }
            else
            {
                printf("[DEBUG] Wiadomość wysłana do gracza %d: %s\n", i, room->player_names[i]);
            }
        }
    }
}

// Przywracanie pokoju do stanu początkowego
void reset_game_room(GameRoom *room)
{
    room->player_count = 0;
    room->deck_index = 0;
    room->stack_index = 0;
    room->game_started = 0;
    room->rhyme_match = 0;
    room->zaklepywanie = 0;

    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        room->player_sockets[i] = -1;
        memset(room->player_names[i], 0, sizeof(room->player_names[i]));
        room->hand_sizes[i] = 0;
        memset(room->hands[i], 0, sizeof(room->hands[i]));
    }
}

// Obsługa rozłączeń
void handle_player_disconnect(GameRoom *room, int player_index)
{
    pthread_mutex_lock(&room->lock);

    char leaving_player_name[50];
    strncpy(leaving_player_name, room->player_names[player_index], sizeof(leaving_player_name) - 1);
    leaving_player_name[sizeof(leaving_player_name) - 1] = '\0';

    printf("[INFO] Gracz %s rozłączył się.\n", leaving_player_name);

    // Zamykanie gniazda
    if (room->player_sockets[player_index] > 0)
    {
        close(room->player_sockets[player_index]);
        room->player_sockets[player_index] = -1;
    }

    // Rozdawanie kart pozostałym graczom
    if (room->hand_sizes[player_index] > 0)
    {
        int target_player = (player_index + 1) % MAX_PLAYERS;
        for (int i = 0; i < room->hand_sizes[player_index]; i++)
        {
            // Znajdź kolejnego aktywnego gracza
            while (room->player_sockets[target_player] <= 0)
            {
                target_player = (target_player + 1) % MAX_PLAYERS;
            }

            // Przypisz kartę
            strcpy(room->hands[target_player][room->hand_sizes[target_player]], room->hands[player_index][i]);
            room->hand_sizes[target_player]++;

            // Przejdź do kolejnego aktywnego gracza
            target_player = (target_player + 1) % MAX_PLAYERS;
        }
    }

    // Wyczyszczenie danych gracza
    memset(room->player_names[player_index], 0, sizeof(room->player_names[player_index]));
    room->hand_sizes[player_index] = 0;
    memset(room->hands[player_index], 0, sizeof(room->hands[player_index]));

    room->player_count--;

    printf("[DEBUG] Aktualizacja pokoju po rozłączeniu. Liczba graczy: %d.\n", room->player_count);

    // Aktualizacja kolejki
    if (room->player_count > 0)
    {
        update_turn_after_disconnect(room);
    }
    else
    {
        printf("[DEBUG] Pokój jest pusty. Resetowanie stanu pokoju.\n");
        reset_game_room(room);
    }

    // Powiadomienie pozostałych graczy
    char message[1024];
    snprintf(message, sizeof(message), "SERWER:Gracz %s opuścił grę. Jego karty zostały rozdane.|", leaving_player_name);
    broadcast_message_to_room(room, message);
    broadcast_player_list(room);

    // Obsługa końca gry, jeśli został jeden gracz
    if (room->player_count == 1)
    {
        int winner_index = -1;
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (room->player_sockets[i] > 0)
            {
                winner_index = i;
                break;
            }
        }

        if (winner_index != -1)
        {
            char win_message[1024];
            snprintf(win_message, sizeof(win_message), "SERWER:Gratulacje! Wygrałeś grę!|");
            send(room->player_sockets[winner_index], win_message, strlen(win_message), 0);

            close(room->player_sockets[winner_index]);
            room->player_sockets[winner_index] = -1;
            room->player_count = 0;

            printf("[INFO] Gracz %d wygrał grę i został rozłączony.\n", winner_index + 1);
        }
    }
    if (room->player_count == 0)
    {
        printf("[DEBUG] Pokój jest pusty. Resetowanie stanu pokoju.\n");
        reset_game_room(room);
    }

    pthread_mutex_unlock(&room->lock);
}

// Tasowanie talii
void shuffle_deck(char deck[DECK_SIZE][20])
{
    srand(time(NULL));
    for (int i = 0; i < DECK_SIZE; i++)
    {
        int j = rand() % DECK_SIZE;
        char temp[20];
        strcpy(temp, deck[i]);
        strcpy(deck[i], deck[j]);
        strcpy(deck[j], temp);
    }
}

// Inicjalizacja talii
void initialize_deck(char deck[DECK_SIZE][20])
{
    const char *cards[] = {"Buła", "Pizza", "Kot", "Ser", "Koza"};
    for (int i = 0; i < DECK_SIZE; i++)
    {
        strcpy(deck[i], cards[i % 5]);
    }
    shuffle_deck(deck);
}

// Inicjalizacja gracza i wątek "turowy"
void *player_thread(void *arg)
{
    int player_socket = *((int *)arg);
    int game_index = -1;
    int player_index = -1;
    char message[1024];
    char player_name[50];
    GameRoom *room;

    // Odbieranie nazwy gracza
    int n = recv(player_socket, player_name, sizeof(player_name) - 1, 0);
    if (n <= 0)
    {
        close(player_socket);
        pthread_exit(NULL);
    }
    player_name[n] = '\0';

    // Znajdowanie wolnego nierozpoczętego pokoju dla gracza
    for (int i = 0; i < MAX_GAMES; i++)
    {
        printf("[DEBUG] Sprawdzam pokój %d: Liczba graczy: %d, Gra rozpoczęta: %d\n",
               i, game_rooms[i].player_count, game_rooms[i].game_started);
        pthread_mutex_lock(&game_rooms[i].lock);

        if (game_rooms[i].player_count < MAX_PLAYERS && game_rooms[i].game_started == 0)
        {
            if (game_rooms[i].player_count == 0)
            {
                printf("[DEBUG] Resetowanie pokoju %d dla nowej gry.\n", i);
                reset_game_room(&game_rooms[i]);
            }

            game_index = i;
            player_index = game_rooms[i].player_count++;
            game_rooms[i].player_sockets[player_index] = player_socket;
            strncpy(game_rooms[i].player_names[player_index], player_name, sizeof(game_rooms[i].player_names[player_index]) - 1);
            printf("[DEBUG] Gracz %s dodany do pokoju %d, na pozycji %d.\n", player_name, i, player_index);

            room = &game_rooms[i];
            pthread_mutex_unlock(&game_rooms[i].lock);
            debug_dump_room_state(room, game_index);
            break;
        }

        pthread_mutex_unlock(&game_rooms[i].lock);
    }

    if (game_index == -1)
    {
        strcpy(message, "SERWER:Brak wolnych miejsc w pokojach lub gra już rozpoczęta.|");
        send(player_socket, message, strlen(message), 0);
        close(player_socket);
        pthread_exit(NULL);
    }

    snprintf(message, sizeof(message), "SERWER:Dołączono do gry jako %s. Oczekuj na wypełnienie pokoju.|", player_name);
    send(player_socket, message, strlen(message), 0);

    //Czekanie na pełny pokój
    while (room->player_count < MAX_PLAYERS)
    {
        sleep(1);
    }

    pthread_mutex_lock(&room->lock);
    room->game_started = 1;
    snprintf(message, sizeof(message), "SERWER:Gra rozpoczęta, zaczyna gracz 1|");

    broadcast_message_to_room(room, message);
    if (room->deck_index == 0)
    {
        initialize_deck(room->deck);
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            room->hand_sizes[i] = 0;
            for (int j = 0; j < DECK_SIZE / MAX_PLAYERS; j++)
            {
                strcpy(room->hands[i][j], room->deck[room->deck_index++]);
                room->hand_sizes[i]++;
            }
        }
        room->current_turn = 0;
        room->rhyme_index = 0;
    }
    broadcast_player_list(room);
    send_cards_count(room, player_index);
    pthread_mutex_unlock(&room->lock);

    //Główna pętla gry turowej
    while (1)
    {
        int n = recv(player_socket, message, sizeof(message), 0);
        if (n <= 0)
        {
            if (n == 0)
            {
                printf("[INFO] Gracz %s rozłączył się.\n", player_name);
            }
            else
            {
                perror("[ERROR] Błąd odbioru danych");
            }
            handle_player_disconnect(room, player_index);
            break;
        }

        message[n] = '\0';

        if (strcmp(message, "ZAGRAJ_KARTE") == 0)
        {
            pthread_mutex_lock(&room->lock);

            if (room->zaklepywanie == 1)
            {
                snprintf(message, sizeof(message), "SERWER:Nie można zagrywać kart podczas zaklepywania stosu!|");
                send(player_socket, message, strlen(message), 0);
            }
            else if (player_index != room->current_turn)
            {
                printf("[DEBUG] Gracz %d próbował zagrać kartę poza swoją turą. Aktualna tura: %d.\n", player_index, room->current_turn);
                snprintf(message, sizeof(message), "SERWER:Nie jest Twoja tura!|");
                send(player_socket, message, strlen(message), 0);
            }
            else if (room->hand_sizes[player_index] == 0)
            {
                snprintf(message, sizeof(message), "SERWER:Nie masz już kart do zagrania!|");
                send(player_socket, message, strlen(message), 0);
            }
            else
            {

                strcpy(room->stack[room->stack_index++], room->hands[player_index][0]);
                for (int i = 1; i < room->hand_sizes[player_index]; i++)
                {
                    strcpy(room->hands[player_index][i - 1], room->hands[player_index][i]);
                }
                room->hand_sizes[player_index]--;

                snprintf(message, sizeof(message), "SERWER:Gracz %d zagrał kartę|", player_index + 1);

                broadcast_message_to_room(room, message);

                send_cards_count(room, player_index);

                snprintf(message, sizeof(message), "AKTUALNA_KARTA:%s|", room->stack[room->stack_index - 1]);
                for (int i = 0; i < max_players; i++)
                {
                    send(room->player_sockets[i], message, strlen(message), 0);
                }

                if (strcmp(room->stack[room->stack_index - 1], rhyme[room->rhyme_index]) == 0)
                {
                    printf("Karta pasuje do rymowanki: %s\n", rhyme[room->rhyme_index]);
                    room->zaklepywanie = 1; // Ustaw zaklepywanie na aktywne
                    pthread_mutex_lock(&room->rhyme_mutex);
                    room->rhyme_match = 1;
                    pthread_cond_signal(&room->rhyme_cond);
                    pthread_mutex_unlock(&room->rhyme_mutex);
                }

                snprintf(message, sizeof(message), "AKTUALNA_RYMOWANKA:%s|", rhyme[room->rhyme_index]);
                for (int i = 0; i < max_players; i++)
                {
                    send(room->player_sockets[i], message, strlen(message), 0);
                }

                room->current_turn = (room->current_turn + 1) % MAX_PLAYERS;
                while (room->player_sockets[room->current_turn] <= 0)
                {
                    room->current_turn = (room->current_turn + 1) % MAX_PLAYERS;
                }
                printf("[DEBUG] Kolejka przekazana do gracza %d\n", room->current_turn);

                room->rhyme_index = (room->rhyme_index + 1) % RHYME_LENGTH;
                broadcast_player_list(room);
            }
            debug_dump_room_state(room, game_index);
            if (room->hand_sizes[player_index] == 0)
            {
                snprintf(message, sizeof(message), "SERWER:Gracz %d wygrał grę!|", player_index + 1);
                broadcast_message_to_room(room, message);

                printf("[INFO] Gracz %d wygrał grę.\n", player_index + 1);

                // Rozłącz wszystkich graczy i zresetuj pokój
                for (int i = 0; i < MAX_PLAYERS; i++)
                {
                    if (room->player_sockets[i] > 0)
                    {
                        close(room->player_sockets[i]);
                        room->player_sockets[i] = -1;
                    }
                }
                reset_game_room(room);
            }

            pthread_mutex_unlock(&room->lock);
        }

        else if (strcmp(message, "ZAKLEP") == 0)
        {
            pthread_mutex_lock(&room->rhyme_mutex);
            if (room->zaklepywanie == 0)
            {
                snprintf(message, sizeof(message), "SERWER:Nie można kliknąć stosu teraz!|");
                send(player_socket, message, strlen(message), 0);
            }
            else if (room->rhyme_match == 0)
            {
                snprintf(message, sizeof(message), "SERWER:Już ktoś zaklepał stos!|");
                send(player_socket, message, strlen(message), 0);
            }
            else
            {
                // Rejestracja kliknięcia
                snprintf(message, sizeof(message), "SERWER:Kliknięcie stosu zarejestrowane.|");
                send(player_socket, message, strlen(message), 0);

                // Wyłącz możliwość dalszego kliknięcia
                room->rhyme_match = 0;
            }
            debug_dump_room_state(room, game_index);
            pthread_mutex_unlock(&room->rhyme_mutex);
        }
        else if (strcmp(message, "WYJSCIE") == 0)
        {
            printf("[INFO] Gracz %d opuścił grę na własne życzenie.\n", player_index + 1);
            handle_player_disconnect(room, player_index);
            break;
        }
    }
    close(player_socket);
    pthread_exit(NULL);
}

//Wątek czasu rzeczywistego
void *real_time_thread(void *arg)
{
    ThreadArgs *args = (ThreadArgs *)arg;
    GameRoom *room = args->room;
    int room_id = args->room_id;
    free(args);

    while (1)
    {
        pthread_mutex_lock(&room->rhyme_mutex);

        while (!room->rhyme_match)
        {
            printf("[DEBUG] Wątek %d czeka na rymowankę...\n", room_id);
            pthread_cond_wait(&room->rhyme_cond, &room->rhyme_mutex);
        }
        room->rhyme_match = 0;
        pthread_mutex_unlock(&room->rhyme_mutex);

        printf("[DEBUG] Wątek %d: Rymowanka pasuje, zaklepywanie stosu.\n", room_id);

        pthread_mutex_lock(&room->lock);
        int player_count = room->player_count;
        pthread_mutex_unlock(&room->lock);

        for (int i = 0; i < player_count; i++)
        {
            pthread_mutex_lock(&room->lock);
            int socket = room->player_sockets[i];
            pthread_mutex_unlock(&room->lock);
        }

        int clicks[MAX_PLAYERS] = {0};
        int last_player = -1;
        int total_clicks = 0;

        while (total_clicks < player_count)
        {
            for (int i = 0; i < max_players; i++)
            {
                pthread_mutex_lock(&room->lock);
                int socket = room->player_sockets[i];
                pthread_mutex_unlock(&room->lock);

                if (socket < 0 || clicks[i])
                    continue;

                char response[1024];
                int n = recv(socket, response, sizeof(response), MSG_DONTWAIT);

                if (n > 0)
                {
                    response[n] = '\0';
                    printf("[DEBUG] Gracz %d wysłał: %s\n", i + 1, response);
                    if (strcmp(response, "ZAKLEP") == 0)
                    {
                        clicks[i] = 1;
                        last_player = i;
                        total_clicks++;
                        printf("[DEBUG] Gracz %d zaklepał stos. Total_clicks = %d\n", i + 1, total_clicks);
                    }
                }
                else if (n == 0)
                {
                    printf("[DEBUG] Gracz %d rozłączył się.\n", i + 1);

                    clicks[i] = 1;
                    total_clicks++;
                }
                else if (errno != EAGAIN && errno != EWOULDBLOCK)
                {
                    perror("[ERROR] recv\n");
                }
            }
        }

        // Przyznanie stosu kart ostatniemu graczowi
        pthread_mutex_lock(&room->lock);
        if (last_player != -1)
        {
            for (int i = 0; i < room->stack_index; i++)
            {
                strcpy(room->hands[last_player][room->hand_sizes[last_player]], room->stack[i]);
                room->hand_sizes[last_player]++;
            }
            room->stack_index = 0;
        }
        room->zaklepywanie = 0; // Wyłącz zaklepywanie stosu
        pthread_mutex_unlock(&room->lock);

        broadcast_player_list(room);

        // Aktualizacja stanu dla wszystkich graczy
        for (int i = 0; i < max_players; i++)
        {
            pthread_mutex_lock(&room->lock);
            int socket = room->player_sockets[i];
            int hand_size = room->hand_sizes[i];
            pthread_mutex_unlock(&room->lock);

            if (socket >= 0)
            {
                char message[1024];
                snprintf(message, sizeof(message), "SERWER:Gracz %d przegrywa i zdobywa stos. Teraz kolej Gracza %d|", last_player + 1, room->current_turn + 1); // TO DO WSZYSTKICH
                send(socket, message, strlen(message), 0);
            }
        }

        printf("[DEBUG] Zaklepywanie stosu zakończone. Oczekiwanie na nową rymowankę.\n");
    }
}

int main()
{
    int server_socket, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    if ((server_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0)
    {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(1100);

    if (bind(server_socket, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_socket, MAX_PLAYERS) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Serwer uruchomiony na porcie 1100\n");

    for (int i = 0; i < MAX_GAMES; i++)
    {
        game_rooms[i].player_count = 0;
        game_rooms[i].deck_index = 0;
        game_rooms[i].stack_index = 0;
        game_rooms[i].game_started = 0;
        pthread_mutex_init(&game_rooms[i].lock, NULL);
        pthread_mutex_init(&game_rooms[i].rhyme_mutex, NULL);
        pthread_cond_init(&game_rooms[i].rhyme_cond, NULL);

        pthread_t rt_thread;
        ThreadArgs *args = malloc(sizeof(ThreadArgs));
        args->room = &game_rooms[i];
        args->room_id = i;

        pthread_create(&rt_thread, NULL, real_time_thread, (void *)args);
        pthread_detach(rt_thread);
    }
    while ((new_socket = accept(server_socket, (struct sockaddr *)&address, (socklen_t *)&addrlen)) >= 0)
    {
        printf("Nowy gracz dołączył!\n");

        pthread_t player;
        pthread_create(&player, NULL, player_thread, &new_socket);
        pthread_detach(player);
    }

    close(server_socket);
    return 0;
}
