#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <algorithm>
#include <dirent.h> 

#define FILEBUFSIZE 8192
#define SOCKBUFSIZE 4096
#define COMBUFSIZE 64

using namespace std;

chrono::milliseconds wait(10);

char file_buff[FILEBUFSIZE];
char sock_buff[SOCKBUFSIZE];
char com_buff[COMBUFSIZE];

typedef struct sock {
    int out;
    int kom;
} sock;

vector<string> listaplikow;
vector<sock> clientFds;

int ile_klientow = 0;
int ile_plikow = 0;
int current_song_index = 0;

void scan_music_directory() {
    DIR *dir;
    struct dirent *entry;
    listaplikow.clear();
    
    dir = opendir("."); 
    
    while ((entry = readdir(dir)) != NULL) {
        string filename(entry->d_name);
        
        if (filename == "." || filename == "..") {
            continue;
        }
        
        if (filename.length() > 4) {
            string extension = filename.substr(filename.length() - 4);
            transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
            
            if (extension == ".wav") {
                listaplikow.push_back(filename);
            }
        }
    }
    
    closedir(dir);    
    sort(listaplikow.begin(), listaplikow.end());
    ile_plikow = listaplikow.size();
    cout << "Znaleziono " << ile_plikow << " plikow WAV" << endl;
}

void zapisz_liste_do_pliku() {
    ofstream lista("listaplikow", ios::trunc); 
    for (const auto& plik : listaplikow) {
        lista << plik << endl;
    }
    lista.close();
}

void * odbierz_dane(void *arg) {
    cout << "Nowy klient do uploadu" << endl;
    int client_sock = *((int *)arg);
    delete (int*)arg; // Освобождаем память
    
    char buffer[SOCKBUFSIZE];
    memset(buffer, 0, SOCKBUFSIZE);
    
    int n = recv(client_sock, buffer, SOCKBUFSIZE - 1, 0);
    if (n <= 0) {
        close(client_sock);
        pthread_exit(NULL);
    }
    
    buffer[n] = '\0';
    string nazwa_pliku(buffer);
    
    cout << "Odbieranie pliku: " << nazwa_pliku << endl;
    
    if (nazwa_pliku.size() < 4 || nazwa_pliku.substr(nazwa_pliku.size() - 4) != ".wav") {
        nazwa_pliku += ".wav";
    }
    
    ofstream file(nazwa_pliku, ios::binary); //открываем файл для записи
    if (!file.is_open()) {
        cout << "Nie moge otworzyc pliku do zapisu: " << nazwa_pliku << endl;
        close(client_sock);
        pthread_exit(NULL);
    }
    
    long total_bytes = 0;
    bool koniec = false;
    
    while (!koniec) {
        memset(buffer, 0, SOCKBUFSIZE);
        n = recv(client_sock, buffer, SOCKBUFSIZE, 0);
        
        if (n > 0) {
            if (n >= 6 && strncmp(buffer, "koniec", 6) == 0) {
                koniec = true;
                if (n > 6) {
                    file.write(buffer + 6, n - 6);
                    total_bytes += (n - 6);
                }
            } else {
                file.write(buffer, n);
                total_bytes += n;
            }
        } else if (n == 0) {
            koniec = true;
        }
    }
    
    file.close();
    
    bool istnieje = false;
    for (const auto& plik : listaplikow) {
        if (plik == nazwa_pliku) {
            istnieje = true;
            break;
        }
    }
    
    if (!istnieje) {
        listaplikow.push_back(nazwa_pliku);
        ile_plikow++;
        zapisz_liste_do_pliku();
        cout << "Dodano nowy plik: " << nazwa_pliku << endl;
    } else {
        cout << "Plik juz istnieje: " << nazwa_pliku << endl;
    }
    
    close(client_sock);
    pthread_exit(NULL);
}

void * zczytaj(void *) {
    cout << "RADIO START - Odtwarzanie rozpoczęte" << endl;
    
    while (1) {
        if (ile_plikow == 0) {
            this_thread::sleep_for(chrono::seconds(1));
            continue;
        }
        
        if (current_song_index >= ile_plikow) {
            current_song_index = 0;
        }
        
        string current_file = listaplikow[current_song_index];
        cout << "Odtwarzanie [" << current_song_index << "]: " << current_file << endl;
        
        ifstream file(current_file, ios::binary);
        if (!file.is_open()) {
            cout << "BŁĄD: Nie mogę otworzyć pliku: " << current_file << endl;
            current_song_index++;
            continue;
        }
        
        while (file) {
            file.read(file_buff, FILEBUFSIZE);
            size_t count = file.gcount();
            
            if (count == 0) {
                break;
            }
            
            for (int i = 0; i < ile_klientow; i++) {
                if (clientFds[i].out > 0) {
                    send(clientFds[i].out, file_buff, count, 0);
                }
            }
            
            for (int i = 0; i < ile_klientow; i++) {
                if (clientFds[i].kom > 0) {
                    memset(com_buff, 0, COMBUFSIZE);
                    int n = recv(clientFds[i].kom, com_buff, COMBUFSIZE - 1, MSG_DONTWAIT);
                                                                           //неблокируемый режим
                    
                    if (n > 0) {
                        com_buff[n] = '\0';
                        
                        if (strcmp(com_buff, "lista") == 0) {
                            cout << "Wysylam liste do klienta" << endl;
                            for (int k = 0; k < ile_plikow; k++) {
                                send(clientFds[i].kom, listaplikow[k].c_str(), listaplikow[k].size(), 0);
                                send(clientFds[i].kom, "|", 1, 0);
                            }
                        }
                        else if (strcmp(com_buff, "zmiana") == 0) {
                            memset(com_buff, 0, COMBUFSIZE);
                            n = recv(clientFds[i].kom, com_buff, COMBUFSIZE - 1, 0);
                            if (n > 0) {
                                com_buff[n] = '\0';
                                int nowy_index = atoi(com_buff);
                                if (nowy_index >= 0 && nowy_index < ile_plikow) {
                                    cout << "Zmiana na piosenke: " << nowy_index << endl;
                                    current_song_index = nowy_index;
                                    file.close();
                                    goto next_song; 
                                }
                            }
                        }
                        else if (strcmp(com_buff, "usun") == 0) {
                            memset(com_buff, 0, COMBUFSIZE);
                            n = recv(clientFds[i].kom, com_buff, COMBUFSIZE - 1, 0);
                            if (n > 0) {
                                com_buff[n] = '\0';
                                int index_to_remove = atoi(com_buff);
                                
                                if (index_to_remove >= 0 && index_to_remove < ile_plikow) {
                                    cout << "Usuwanie: " << listaplikow[index_to_remove] << endl;
                                    
                                    remove(listaplikow[index_to_remove].c_str());
                                    
                                    listaplikow.erase(listaplikow.begin() + index_to_remove);
                                    ile_plikow--;
                                    zapisz_liste_do_pliku();
                                    
                                    if (index_to_remove < current_song_index) {
                                        current_song_index--;
                                    }
                                    if (index_to_remove == current_song_index) {
                                        if (current_song_index >= ile_plikow) {
                                            current_song_index = 0;
                                        }
                                        file.close();
                                        goto next_song;
                                    }
                                }
                            }
                        }
                        else if (strcmp(com_buff, "close") == 0) {
                            close(clientFds[i].out);
                            close(clientFds[i].kom);
                            clientFds[i].out = -1;
                            clientFds[i].kom = -1;
                        }
                    }
                    else if (n == 0) {
                        close(clientFds[i].out);
                        close(clientFds[i].kom);
                        clientFds[i].out = -1;
                        clientFds[i].kom = -1;
                    }
                }
            }
            
            this_thread::sleep_for(wait);
        }
        
        file.close();
       
        current_song_index++;
        if (current_song_index >= ile_plikow) {
            current_song_index = 0;
        }
        
        next_song:
        continue;
    }
    
    pthread_exit(NULL);
}

int main(int argc, char** argv) {
    auto port = 8080;
    
    if(argc != 2) {
        cout << "Port: " << port << endl;
    } else {
        port = atoi(argv[1]);
    }
    
    scan_music_directory();
    zapisz_liste_do_pliku();
    
    int serverSocket = socket(PF_INET, SOCK_STREAM, 0);// Tworzenie gniazda(TCP)
    if (serverSocket < 0) {
        cout << "Błąd tworzenia gniazda" << endl;
        return 1;
    }
    
    int opt = 1; // Ustawienie opcji gniazda
    setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); //быстрый перезапуск(после закрытия)
    
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET; //ipv4
    serverAddr.sin_port = htons(port); //port
    serverAddr.sin_addr.s_addr = INADDR_ANY; //сетевые интерфейсы
    
    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cout << "Błąd bind" << endl;
        close(serverSocket);
        return 1;
    }
    
    if (listen(serverSocket, 10) < 0) { //режим прослушивания
        close(serverSocket);
        return 1;
    }
    
    pthread_t radio_thread; // Wątek odtwarzania radia
    if (pthread_create(&radio_thread, NULL, zczytaj, NULL) != 0) {
        cout << "Nie udało się stworzyć wątku radia" << endl;
        close(serverSocket);
        return 1;
    }
    pthread_detach(radio_thread);
    
    while (1) { // Główna pętla akceptowania klientów
        sockaddr_in clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        
        // Akceptujemy 3 połączenia dla każdego klienta
        int sock_in = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen); //upload
        int sock_out = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen); //audio 
        int sock_kom = accept(serverSocket, (sockaddr*)&clientAddr, &clientLen); //cpmmand
        
        if (sock_in > 0 && sock_out > 0 && sock_kom > 0) {
            cout << "Nowy klient połączony" << endl;
            
            sock new_client; // Dodajemy do listy klientów
            new_client.out = sock_out;
            new_client.kom = sock_kom;
            clientFds.push_back(new_client);
            ile_klientow++;
            
            pthread_t upload_thread; // Wątek do odbierania plików
            int* client_sock = new int(sock_in);
            if (pthread_create(&upload_thread, NULL, odbierz_dane, client_sock) != 0) {
                cout << "Nie udało się stworzyć wątku uploadu" << endl;
                delete client_sock;
            } else {
                pthread_detach(upload_thread);
            }
        } else {
            if (sock_in > 0) close(sock_in);
            if (sock_out > 0) close(sock_out);
            if (sock_kom > 0) close(sock_kom);
        }
    }
    
    close(serverSocket);
    return 0;
}
