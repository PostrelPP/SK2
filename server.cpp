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

void zapisz_liste_do_pliku() {
    ofstream lista("listaplikow");
    for (const auto& plik : listaplikow) {
        lista << plik << endl;
    }
    lista.close();
    cout << "Zapisano liste do pliku" << endl;
}

void * odbierz_dane(void *arg) {
    cout << "Nowy sluchacz do uploadu" << endl;
    int sock = *((int *)arg);
    int n;
    
    while(1) {
        n = recv(sock, sock_buff, SOCKBUFSIZE, MSG_DONTWAIT);
        if(n == 0) {
            close(sock);
            break;
        }
        if(n > 0) {
            string nazwa_pliku(sock_buff);
            cout << "Odbior pliku: " << nazwa_pliku << endl;
            
            if (nazwa_pliku.size() < 4 || nazwa_pliku.substr(nazwa_pliku.size() - 4) != ".wav") {
                nazwa_pliku += ".wav";
            }
            
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
            }
            
            ofstream file(nazwa_pliku, ios::binary);
            memset(&sock_buff, 0, sizeof(sock_buff));
            
            bool koniec = false;
            while(!koniec) {
                n = recv(sock, sock_buff, SOCKBUFSIZE, 0);
                if (n > 0) {
                    if (n >= 6 && strncmp(sock_buff, "koniec", 6) == 0) {
                        koniec = true;
                        if (n > 6) {
                            file.write(sock_buff, n - 6);
                        }
                    } else {
                        file.write(sock_buff, n);
                    }
                    memset(&sock_buff, 0, sizeof(sock_buff));
                }
                if (n == 0) {
                    koniec = true;
                }
                this_thread::sleep_for(wait);
            }
            
            file.close();
            cout << "Plik odebrany i zapisany: " << nazwa_pliku << endl;
        }
    }
    cout << "Klient uploadu sie rozlaczyl" << endl;
    pthread_exit(NULL);
}

void * zczytaj(void *) {
    int n;
    int pauza = 0;
    int ile_zmiana = 0;
    int co_usun = 0;
    bool czy_zmiana = false;
    cout << "RADIO START" << endl;
    
    while (1) {
        int licznik_pliki = 0;
        while(licznik_pliki < ile_plikow) {
            ifstream file(listaplikow[licznik_pliki], ios::binary);
            if (!file.is_open()) {
                licznik_pliki++;
                continue;
            }
            
            memset(&file_buff, 0, sizeof(file_buff));
            while(file) {
                file.read(file_buff, FILEBUFSIZE);
                size_t count = file.gcount();
                
                int licznik_klienci = 0;
                while(licznik_klienci < ile_klientow) {
                    send(clientFds[licznik_klienci].out, file_buff, FILEBUFSIZE, 0);
                    
                    memset(&com_buff, 0, sizeof(com_buff));
                    n = recv(clientFds[licznik_klienci].kom, com_buff, COMBUFSIZE, MSG_DONTWAIT);
                    
                    if(n > 0) {
                        if (strcmp(com_buff, "close") == 0) {
                            close(clientFds[licznik_klienci].out);
                            close(clientFds[licznik_klienci].kom);
                            clientFds.erase(clientFds.begin() + licznik_klienci);
                            ile_klientow--;
                            break;
                        }
                        
                        if (strcmp(com_buff, "lista") == 0) {
                            for(int k = 0; k < ile_plikow; k++) {
                                const char* nazwa_pliku = listaplikow[k].c_str();
                                send(clientFds[licznik_klienci].kom, nazwa_pliku, strlen(nazwa_pliku), 0);
                                send(clientFds[licznik_klienci].kom, "|", 1, 0);
                                this_thread::sleep_for(wait);
                            }
                        }
                        
                        if (strcmp(com_buff, "zmiana") == 0) {
                            memset(&com_buff, 0, sizeof(com_buff));
                            recv(clientFds[licznik_klienci].kom, com_buff, COMBUFSIZE, 0);
                            ile_zmiana = atoi(com_buff);
                            czy_zmiana = true;
                            if (ile_zmiana >= 0 && ile_zmiana < ile_plikow) {
                                licznik_pliki = ile_zmiana;
                            }
                            break;
                        }
                        
                        if (strcmp(com_buff, "usun") == 0) {
                            memset(&com_buff, 0, sizeof(com_buff));
                            recv(clientFds[licznik_klienci].kom, com_buff, COMBUFSIZE, 0);
                            co_usun = atoi(com_buff);
                            
                            if(co_usun >= 0 && co_usun < ile_plikow) {
                                cout << "Usuwanie pliku: " << listaplikow[co_usun] << " z indeksu: " << co_usun << endl;
                                
                                remove(listaplikow[co_usun].c_str());
                                
                                listaplikow.erase(listaplikow.begin() + co_usun);
                                ile_plikow--;
                                
                                zapisz_liste_do_pliku();
                                
                                if (co_usun < licznik_pliki) {
                                    licznik_pliki--;
                                }
                            }
                        }
                    }
                    
                    if(n == 0) {
                        close(clientFds[licznik_klienci].out);
                        close(clientFds[licznik_klienci].kom);
                        clientFds.erase(clientFds.begin() + licznik_klienci);
                        ile_klientow--;
                        cout << "Sluchacz sie rozlaczyl" << endl;
                        break;
                    }
                    licznik_klienci++;
                }
                
                if(!count) break;
                if(czy_zmiana) {
                    czy_zmiana = false;
                    break;
                }
                
                pauza++;
                if(pauza == 28) {
                    sleep(1);
                    pauza = 0;
                }
                this_thread::sleep_for(wait);
            }
            file.close();
            
            if(!czy_zmiana) {
                licznik_pliki++;
            }
        }
    }
    pthread_exit(NULL);
}

int main(int argc, char** argv) {
    auto port = 8080;
    int serverSocket, sock_in;
    struct sockaddr_in serverAddr;
    struct sockaddr_storage serverStorage;
    socklen_t addr_size;
    sock socks;
    string nazwa;
    pthread_t thread_id;
    pthread_t thread_idRad;
    
    if(argc != 2) {
        cout << "Port ustawiony na domyslny: " << port << endl;
    } else {
        port = atoi(argv[1]);
    }
    
    ifstream lista("listaplikow");
    if (lista.is_open()) {
        while(getline(lista, nazwa)) {
            ifstream test(nazwa);
            if (test.good()) {
                listaplikow.push_back(nazwa);
                ile_plikow++;
                test.close();
            }
        }
        lista.close();
        cout << "Wczytano " << ile_plikow << " plikow z listaplikow" << endl;
    } else {
        cout << "Plik listaplikow nie istnieje, tworzenie nowego" << endl;
    }
    
    serverSocket = socket(PF_INET, SOCK_STREAM, 0);
    
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    bind(serverSocket, (struct sockaddr *) &serverAddr, sizeof(serverAddr));
    
    if(listen(serverSocket, 20) == 0)
        printf("Czekam na polaczenie na porcie %d\n", port);
    else
        printf("blad: listen\n");
    
    if(pthread_create(&thread_idRad, NULL, zczytaj, 0) != 0)
        printf("Nie udalo sie stworzyc watku radia\n");
    
    while(1) {
        addr_size = sizeof serverStorage;
        sock_in = accept(serverSocket, (struct sockaddr *) &serverStorage, &addr_size);
        socks.out = accept(serverSocket, (struct sockaddr *) &serverStorage, &addr_size);
        socks.kom = accept(serverSocket, (struct sockaddr *) &serverStorage, &addr_size);
        
        if (sock_in > 0 && socks.out > 0 && socks.kom > 0) {
            ile_klientow++;
            clientFds.push_back(socks);  
            pthread_detach(thread_id);
        }
    }
    
    pthread_detach(thread_idRad);
    return 0;
}
