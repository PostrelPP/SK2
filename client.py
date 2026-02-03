import socket
import time
import threading
import pyaudio
import pygame
import os
from pydub import AudioSegment
import io

WIDTH = 600
HEIGHT = 400
BUTTON_WIDTH = 120
BUTTON_HEIGHT = 40

PRIMARY_COLOR = (41, 128, 185)
SECONDARY_COLOR = (52, 152, 219)
ACCENT_COLOR = (46, 204, 113)
BACKGROUND_COLOR = (236, 240, 241)
TEXT_COLOR = (44, 62, 80)
ERROR_COLOR = (231, 76, 60)
HIGHLIGHT_COLOR = (241, 196, 15)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)

def get_available_songs():
    available_songs = []
    music_dir = "./music"  
    
    if not os.path.exists(music_dir):
        os.makedirs(music_dir)
        return available_songs
    
    files = os.listdir(music_dir)
    print(f"Files in {music_dir}: {files}")
    
    for file in files:
        if file.lower().endswith('.mp3'):
            song_name = file[:-4]
            available_songs.append(song_name)
            print(f"Found MP3: {song_name}")
    return available_songs

def connect_to_server(host, port):
    sock_out = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_out.settimeout(5)
    sock_out.connect((host, port))
    sock_out.settimeout(None)
    time.sleep(0.1)
    
    sock_in = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_in.settimeout(5)
    sock_in.connect((host, port))
    sock_in.settimeout(None)
    time.sleep(0.1)
    
    sock_cmd = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock_cmd.settimeout(5)
    sock_cmd.connect((host, port))
    sock_cmd.settimeout(None)
    time.sleep(0.1)

    return sock_out, sock_in, sock_cmd

def close_connection(sock):
    if sock:
        try:
            sock.close()
        except:
            pass

def receive_queue_list(sock):
    if not sock:
        return []
    
    queue_data = b""
    try:
        sock.settimeout(1)
        while True:
            try:
                data = sock.recv(128) #на текстовые команды хватит
                if data:
                    queue_data += data
                else:
                    break
            except socket.timeout:
                break
    except:
        pass
    finally:
        sock.settimeout(None)
    
    if queue_data:
        try:
            decoded = queue_data.decode('utf-8', errors='ignore')
            items = [item.strip() for item in decoded.split('|') if item.strip()]
            return items
        except:
            return []
    return []

def receive_audio_stream(sock, stream, stop_event):
    if not sock or not stream:
        return
    
    sock.settimeout(0.5)
    while not stop_event.is_set():
        try:
            data = sock.recv(4096 * 4) #16 Кб за раз
            if data:
                try:
                    stream.write(data)
                except:
                    break
        except socket.timeout:
            continue
        except:
            break

def convert_mp3_to_wav_stream(mp3_filename):
    try:
        audio = AudioSegment.from_mp3(mp3_filename)
        audio = audio.set_frame_rate(44100).set_channels(2).set_sample_width(2)
        
        wav_buffer = io.BytesIO()
        audio.export(wav_buffer, format="wav")
        wav_data = wav_buffer.getvalue()
        
        print(f"Conversion successful, size: {len(wav_data)} bytes")
        return wav_data
        
    except Exception as e:
        print(f"Conversion error: {e}")
        return None

def upload_song(song_name, sock):
    print(f"=== UPLOAD START: {song_name} ===")
    
    mp3_filename = f"./music/{song_name}.mp3"
    print(f"Looking for file: {mp3_filename}")
    
    if not os.path.exists(mp3_filename):
        print(f"ERROR: File not found: {mp3_filename}")
        print(f"Current directory: {os.getcwd()}")
        print(f"Files in ./music/: {os.listdir('./music') if os.path.exists('./music') else 'Directory not found'}")
        return False
    
    try:
        wav_data = convert_mp3_to_wav_stream(mp3_filename) # Конвертируем MP3 в WAV
        if not wav_data:
            print("ERROR: Failed to convert MP3 to WAV")
            return False
        
        print(f"Sending filename: '{song_name}' (without .wav)")
        sock.send(song_name.encode())
        time.sleep(0.5)  
        
        chunk_size = 12000
        total_chunks = (len(wav_data) + chunk_size - 1) // chunk_size
        print(f"Sending {total_chunks} chunks of {chunk_size} bytes each")
        
        for i in range(0, len(wav_data), chunk_size):
            chunk = wav_data[i:i + chunk_size]
            bytes_sent = sock.send(chunk)
            print(f"Sent chunk {i//chunk_size + 1}/{total_chunks}, size: {bytes_sent} bytes")
            time.sleep(0.001)
        
        print("Sending 'koniec' marker")
        time.sleep(0.5)
        sock.send(b"koniec")
        
        print(f"=== UPLOAD COMPLETE: {song_name} ===")
        return True
        
    except Exception as e:
        print(f"UPLOAD ERROR: {e}")
        import traceback
        traceback.print_exc()
        return False

def create_button(screen, x, y, width, height, text, font, color=PRIMARY_COLOR, hover_color=SECONDARY_COLOR, disabled=False):
    mouse_pos = pygame.mouse.get_pos()
    button_rect = pygame.Rect(x, y, width, height)
    
    is_hovered = button_rect.collidepoint(mouse_pos) and not disabled
    
    if disabled:
        button_color = (200, 200, 200)
    else:
        button_color = hover_color if is_hovered else color
    
    pygame.draw.rect(screen, button_color, button_rect, border_radius=8)
    
    border_color = (150, 150, 150) if disabled else TEXT_COLOR
    pygame.draw.rect(screen, border_color, button_rect, 2, border_radius=8)
    
    text_color = (100, 100, 100) if disabled else WHITE
    text_surface = font.render(text, True, text_color)
    text_rect = text_surface.get_rect(center=button_rect.center)
    screen.blit(text_surface, text_rect)
    
    return button_rect, is_hovered and not disabled

def draw_list(screen, items, start_x, start_y, item_height, selected_index, font):
    visible_items = 5
    if not items:
        return 0, 0
    
    start_idx = max(0, min(selected_index - 2, len(items) - visible_items))
    
    for i in range(visible_items):
        idx = start_idx + i
        if idx < len(items):
            y_pos = start_y + i * item_height
            item_rect = pygame.Rect(start_x, y_pos, 300, item_height - 5)
            
            if idx == selected_index:
                pygame.draw.rect(screen, HIGHLIGHT_COLOR, item_rect, border_radius=5)
            
            text = font.render(items[idx], True, TEXT_COLOR)
            screen.blit(text, (start_x + 10, y_pos + 5))
    
    return start_idx, visible_items

def refresh_playlist(cmd_socket):
    if not cmd_socket:
        return []
    
    try:
        cmd_socket.send(b"lista")
        time.sleep(0.3)
        return receive_queue_list(cmd_socket)
    except Exception as e:
        print(f"Error refreshing playlist: {e}")
        return []

def main():
    try:
        with open("conf.txt", "r") as f:
            server_host = f.readline().strip()
            server_port = int(f.readline().strip())
            print(f"Server configuration: {server_host}:{server_port}")
    except Exception as e:
        server_host = "127.0.0.1"
        server_port = 8080

    try:
        audio = pyaudio.PyAudio()
        audio_stream = audio.open(format=pyaudio.paInt16, channels=2,
                                 rate=44100, output=True, frames_per_buffer=4096*4)
        print("Audio initialized successfully")
    except Exception as e:
        print(f"Audio initialization error: {e}")
        print("Audio playback will be disabled")
        audio = None
        audio_stream = None

    current_screen = "connect"
    is_running = True
    is_connected = False

    selected_queue_index = 0
    selected_song_index = 0
    playlist_queue = []
    available_songs = []

    out_socket = None
    in_socket = None
    cmd_socket = None
    audio_thread = None
    stop_event = threading.Event()
    
    last_refresh_time = time.time()
    refresh_interval = 3

    pygame.init()
    screen = pygame.display.set_mode([WIDTH, HEIGHT])
    pygame.display.set_caption('Internet Radio Client')
    
    title_font = pygame.font.SysFont('Arial', 32, bold=True)
    heading_font = pygame.font.SysFont('Arial', 24, bold=True)
    normal_font = pygame.font.SysFont('Arial', 16)
    small_font = pygame.font.SysFont('Arial', 14)

    clock = pygame.time.Clock()
    
    while is_running:
        clock.tick(30)
       
        current_time = time.time()
        if is_connected and cmd_socket and current_time - last_refresh_time > refresh_interval:
            new_playlist = refresh_playlist(cmd_socket)
            if new_playlist != playlist_queue:
                playlist_queue = new_playlist #актуализация листа каждые 3 секунды
                print(f"Playlist updated: {playlist_queue}")
            last_refresh_time = current_time
        
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                is_running = False
            
            elif event.type == pygame.MOUSEBUTTONDOWN:
                mouse_pos = pygame.mouse.get_pos()
                
                if current_screen == "connect":
                    connect_btn = pygame.Rect(WIDTH//2 - BUTTON_WIDTH//2, 200, BUTTON_WIDTH, BUTTON_HEIGHT)
                    quit_btn = pygame.Rect(WIDTH//2 - BUTTON_WIDTH//2, 260, BUTTON_WIDTH, BUTTON_HEIGHT)
                    
                    if connect_btn.collidepoint(mouse_pos):
                        try:
                            print("Connecting to server...")
                            out_socket, in_socket, cmd_socket = connect_to_server(server_host, server_port)
                            print("Connected to server successfully")
                            
                            if audio_stream:
                                stop_event.clear()
                                audio_thread = threading.Thread(target=receive_audio_stream, 
                                                               args=(in_socket, audio_stream, stop_event))
                                audio_thread.daemon = True
                                audio_thread.start()
                                print("Audio stream thread started")
                            
                            is_connected = True
                            playlist_queue = refresh_playlist(cmd_socket)
                            print(f"Initial playlist: {playlist_queue}")
                            current_screen = "main"
                            
                        except Exception as e:
                            print(f"Connection error: {e}")
                            is_connected = False
                    
                    elif quit_btn.collidepoint(mouse_pos):
                        is_running = False
                
                elif current_screen == "main":
                    start_idx = 0
                    if playlist_queue:
                        start_idx, _ = draw_list(screen, playlist_queue, 50, 120, 30, selected_queue_index, normal_font)
                    
                    next_btn = pygame.Rect(400, 170, BUTTON_WIDTH, BUTTON_HEIGHT)
                    add_btn = pygame.Rect(400, 270, BUTTON_WIDTH, BUTTON_HEIGHT)
                    remove_btn = pygame.Rect(400, 320, BUTTON_WIDTH, BUTTON_HEIGHT)
                    
                    if playlist_queue and 50 <= mouse_pos[0] <= 350 and 120 <= mouse_pos[1] <= 270:
                        click_index = (mouse_pos[1] - 120) // 30 + start_idx
                        if click_index < len(playlist_queue):
                            selected_queue_index = click_index
                            print(f"Selected song: {selected_queue_index} - {playlist_queue[selected_queue_index]}")
                    
                    # Next Song button
                    elif next_btn.collidepoint(mouse_pos) and playlist_queue:
                        if is_connected and cmd_socket:
                            try:
                                next_index = (selected_queue_index + 1) % len(playlist_queue)
                                print(f"Requesting NEXT song: from {selected_queue_index} to {next_index}")
                                
                                cmd_socket.send(b"zmiana")
                                time.sleep(0.1)
                                cmd_socket.send(str(next_index).encode())
                                print(f"Sent 'zmiana {next_index}' to server")
                                
                                # Update selection in client
                                selected_queue_index = next_index
                                
                                # Refresh playlist
                                time.sleep(0.5)
                                playlist_queue = refresh_playlist(cmd_socket)
                                
                            except Exception as e:
                                print(f"Error sending next song command: {e}")
                    
                    # Add Songs button
                    elif add_btn.collidepoint(mouse_pos):
                        print("Loading available songs...")
                        available_songs = get_available_songs()
                        print(f"Found {len(available_songs)} MP3 files in music folder")
                        
                        # Filter songs already in playlist
                        filtered_songs = []
                        for song in available_songs:
                            if f"{song}.wav" not in playlist_queue:
                                filtered_songs.append(song)
                            else:
                                print(f"Song already in playlist: {song}.wav")
                        
                        available_songs = filtered_songs
                        selected_song_index = 0
                        current_screen = "add_songs"
                        print(f"Available songs for upload: {available_songs}")
                    
                    # Remove button
                    elif remove_btn.collidepoint(mouse_pos) and playlist_queue:
                        if is_connected and cmd_socket and playlist_queue:
                            try:
                                print(f"Removing song at index: {selected_queue_index} - {playlist_queue[selected_queue_index]}")
                                
                                # Send usun command
                                cmd_socket.send(b"usun")
                                time.sleep(0.1)
                                cmd_socket.send(str(selected_queue_index).encode())
                                
                                print(f"Sent 'usun {selected_queue_index}' to server")
                                
                                # Wait for processing
                                time.sleep(0.5)
                                
                                # Refresh playlist
                                playlist_queue = refresh_playlist(cmd_socket)
                                print(f"After removal: {playlist_queue}")
                                
                                # Adjust selection index
                                if selected_queue_index >= len(playlist_queue):
                                    selected_queue_index = max(0, len(playlist_queue) - 1)
                                    
                            except Exception as e:
                                print(f"Error removing song: {e}")
                
                elif current_screen == "add_songs":
                    start_idx = 0
                    if available_songs:
                        start_idx, _ = draw_list(screen, available_songs, 50, 120, 30, selected_song_index, normal_font)
                    
                    upload_btn = pygame.Rect(400, 120, BUTTON_WIDTH, BUTTON_HEIGHT)
                    back_btn = pygame.Rect(400, 180, BUTTON_WIDTH, BUTTON_HEIGHT)
                    
                    if available_songs and 50 <= mouse_pos[0] <= 350 and 120 <= mouse_pos[1] <= 270:
                        click_index = (mouse_pos[1] - 120) // 30 + start_idx
                        if click_index < len(available_songs):
                            selected_song_index = click_index
                            print(f"Selected for upload: {available_songs[selected_song_index]}")
                    
                    elif upload_btn.collidepoint(mouse_pos) and available_songs:
                        if is_connected and out_socket:
                            song_to_upload = available_songs[selected_song_index]
                            print(f"=== STARTING UPLOAD: {song_to_upload} ===")
                            
                            def upload_task():
                                if upload_song(song_to_upload, out_socket):
                                    print(f"Upload successful: {song_to_upload}")
                                    time.sleep(2)  # Wait for server to process
                                    if cmd_socket:
                                        nonlocal playlist_queue, available_songs
                                        # Refresh playlist
                                        playlist_queue = refresh_playlist(cmd_socket)
                                        print(f"Updated playlist: {playlist_queue}")
                                        
                                        # Refresh available songs list
                                        available_songs = get_available_songs()
                                        available_songs = [s for s in available_songs if f"{s}.wav" not in playlist_queue]
                                        print(f"Updated available songs: {available_songs}")
                                else:
                                    print(f"Upload failed: {song_to_upload}")
                            
                            upload_thread = threading.Thread(target=upload_task)
                            upload_thread.daemon = True
                            upload_thread.start()
                    
                    elif back_btn.collidepoint(mouse_pos):
                        current_screen = "main"
                        if is_connected and cmd_socket:
                            playlist_queue = refresh_playlist(cmd_socket)

        screen.fill(BACKGROUND_COLOR)
        mouse_pos = pygame.mouse.get_pos()
        
        header_rect = pygame.Rect(0, 0, WIDTH, 60)
        pygame.draw.rect(screen, PRIMARY_COLOR, header_rect)
        
        title_text = title_font.render("Internet Radio", True, WHITE)
        screen.blit(title_text, (20, 15))
        
        # Status indicator
        status_text = "Connected" if is_connected else "Disconnected"
        status_color = ACCENT_COLOR if is_connected else ERROR_COLOR
        status_surface = small_font.render(status_text, True, WHITE)
        pygame.draw.circle(screen, status_color, (WIDTH - 40, 30), 10)
        screen.blit(status_surface, (WIDTH - 120, 22))

        if current_screen == "connect":
            heading = heading_font.render("Connect to Radio Server", True, TEXT_COLOR)
            screen.blit(heading, (WIDTH//2 - heading.get_width()//2, 100))
            
            info_text = f"Server: {server_host}:{server_port}"
            info_surface = normal_font.render(info_text, True, TEXT_COLOR)
            screen.blit(info_surface, (WIDTH//2 - info_surface.get_width()//2, 150))
            
            connect_btn, _ = create_button(screen, WIDTH//2 - BUTTON_WIDTH//2, 200, 
                                          BUTTON_WIDTH, BUTTON_HEIGHT, "Connect", normal_font)
            quit_btn, _ = create_button(screen, WIDTH//2 - BUTTON_WIDTH//2, 260,
                                       BUTTON_WIDTH, BUTTON_HEIGHT, "Quit", normal_font, ERROR_COLOR)

        elif current_screen == "main":
            playlist_heading = heading_font.render("Current Playlist", True, TEXT_COLOR)
            screen.blit(playlist_heading, (50, 80))
            
            if playlist_queue:
                start_idx, _ = draw_list(screen, playlist_queue, 50, 120, 30, selected_queue_index, normal_font)
                count_text = small_font.render(f"Songs in playlist: {len(playlist_queue)}", True, TEXT_COLOR)
                screen.blit(count_text, (50, 280))
            
            # Control buttons
            next_disabled = not playlist_queue
            next_btn, _ = create_button(screen, 400, 170, BUTTON_WIDTH, BUTTON_HEIGHT, 
                                       "Next Song", normal_font, PRIMARY_COLOR, next_disabled)
            
            add_btn, _ = create_button(screen, 400, 270, BUTTON_WIDTH, BUTTON_HEIGHT, 
                                      "Add Songs", normal_font)
            
            remove_disabled = not playlist_queue
            remove_btn, _ = create_button(screen, 400, 320, BUTTON_WIDTH, BUTTON_HEIGHT, 
                                         "Remove", normal_font, ERROR_COLOR, remove_disabled)

        elif current_screen == "add_songs":
            heading = heading_font.render("Available Songs", True, TEXT_COLOR)
            screen.blit(heading, (50, 80))
            
            if available_songs:
                start_idx, _ = draw_list(screen, available_songs, 50, 120, 30, selected_song_index, normal_font)
                count_text = small_font.render(f"Available: {len(available_songs)} songs", True, TEXT_COLOR)
                screen.blit(count_text, (50, 280))
            
            upload_disabled = not available_songs
            upload_btn, _ = create_button(screen, 400, 120, BUTTON_WIDTH, BUTTON_HEIGHT,
                                         "Upload", normal_font, ACCENT_COLOR, upload_disabled)
            
            back_btn, _ = create_button(screen, 400, 180, BUTTON_WIDTH, BUTTON_HEIGHT,
                                       "Back", normal_font)

        pygame.display.update()
    
    if stop_event:
        stop_event.set()
    
    if is_connected:
        if audio_thread:
            audio_thread.join(timeout=1)
        
        close_connection(out_socket)
        close_connection(in_socket)
        close_connection(cmd_socket)
        print("Client connections closed")
    
    if audio_stream:
        audio_stream.stop_stream()
        audio_stream.close()
    
    if audio:
        audio.terminate()
    
    pygame.quit()

if __name__ == "__main__":
    main()
