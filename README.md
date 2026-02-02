# SK2

Radio internetowe to aplikacja klient-serwer umożliwiająca strumieniowanie muzyki do wielu klientów jednocześnie. Serwer przechowuje kolejkę utworów, które kolejno odtwarza i przesyła do wszystkich podłączonych klientów w czasie rzeczywistym.

Główne Funkcjonalności

Serwer
- Przechowuje kolejkę plików dźwiękowych w formacie WAV
- Strumieniuje muzykę do wszystkich podłączonych klientów jednocześnie
- Obsługuje równoczesne połączenia wielu klientów
- Zarządza kolejką odtwarzania
- Zapisuje listę utworów w pliku listaplikow dla trwałości danych

Klient (GUI)
- Łączy się z serwerem radiowym
- Wyświetla aktualną kolejkę utworów
- Umożliwia przesyłanie nowych plików MP3 na serwer (automatyczna konwersja do WAV)
- Pozwala przełączać się między utworami w kolejce
- Umożliwia usuwanie utworów z kolejki
- Odbiera i odtwarza strumień audio w czasie rzeczywistym


Kompilacja Serwera
g++ -std=c++11 -pthread serwer.cpp -o radio

SERWER
./radio 8080

KLIENT
python3 main.py
