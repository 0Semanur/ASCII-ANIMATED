#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 10
#define MAX_CHANNELS 3
#define FRAME_LINES 100
#define FRAME_BUFFER_SIZE 100
#define FRAME_INTERVAL_USEC 50000

// Her kanal için video verisi ve senkronizasyon bilgilerini tutan yapı
typedef struct {
    char frames[FRAME_BUFFER_SIZE][FRAME_LINES][256];  // Ring buffer'da saklanan çerçeveler
    char temp_frame[FRAME_LINES][256];                 // Yeni okunan çerçeveler için geçici bellek
    int in_index;  // Producer'ın yazdığı index
    int out_index; // Consumer'ın okuduğu index
    int frame_count;
    char filename[128]; // Video dosyasının adı
    sem_t empty;    // Boş yer sayısı
    sem_t full;     // Dolu yer sayısı
    sem_t lock;     // Kritik bölge koruması için mutex benzeri semafor
} Channel;

Channel channels[MAX_CHANNELS]; // Tüm kanallar
int server_port = 12345;        // Varsayılan port
int stream_count = 1;           // Yayın (kanal) sayısı

// Producer thread'i için argüman
typedef struct {
    int channel_id;
} ProducerArg;

// Üretici (producer) iş parçacığı: Dosyadan çerçeve okuyup kanala ekler
void* producer_thread(void* arg) {
    ProducerArg* parg = (ProducerArg*)arg;
    int channel_id = parg->channel_id;
    free(parg);

    while (1) {
        // Video dosyasını aç
        FILE* file = fopen(channels[channel_id].filename, "r");
        if (!file) {
            perror("Video dosyası açılamadı");
            sleep(1);
            continue;
        }
        char line[256];
        int line_index = 0;

        while (fgets(line, sizeof(line), file)) {
            // Yeni bir frame başlangıcı
            if (strncmp(line, "---FRAME---", 11) == 0) {
                if (line_index > 0) {
                    sem_wait(&channels[channel_id].empty); // Boş yer var mı?
                    sem_wait(&channels[channel_id].lock);  // Kilit al

                    // temp_frame'den ana buffer'a kopyala
                    for (int i = 0; i < line_index; i++) {
                        strncpy(channels[channel_id].frames[channels[channel_id].in_index][i],
                                channels[channel_id].temp_frame[i], 255);
                    }
                    // Geriye kalan satırları boş bırak
                    for (int i = line_index; i < FRAME_LINES; i++) {
                        channels[channel_id].frames[channels[channel_id].in_index][i][0] = '\0';
                    }

                    // Index'i ilerlet
                    channels[channel_id].in_index = (channels[channel_id].in_index + 1) % FRAME_BUFFER_SIZE;

                    sem_post(&channels[channel_id].lock); // Kilidi bırak
                    sem_post(&channels[channel_id].full); // Dolu sayısını arttır

                    line_index = 0; // Yeni frame için sıfırla
                }
                continue;
            }

            // Satırları geçici belleğe kopyala
            if (line_index < FRAME_LINES) {
                strncpy(channels[channel_id].temp_frame[line_index], line, 255);
                channels[channel_id].temp_frame[line_index][255] = '\0';
                line_index++;
            }
        }

        fclose(file);
        rewind(file); // (Gereksiz: Dosya kapandıktan sonra rewind etkisiz)
    }

    return NULL;
}

// İstemci iş parçacığı: Bağlanan istemciye video akışı gönderir
void* client_handler(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    int channel_network;
    // Bağlanan istemciden kanal ID'sini al
    if (recv(client_fd, &channel_network, sizeof(int), 0) <= 0) {
        perror("Kanal ID alınamadı");
        close(client_fd);
        return NULL;
    }

    int channel_id = ntohl(channel_network); // Ağdan host byte order'a çevir
    if (channel_id < 0 || channel_id >= stream_count) {
        fprintf(stderr, "Geçersiz kanal ID: %d\n", channel_id);
        close(client_fd);
        return NULL;
    }

    char frame_buffer[FRAME_LINES * 256];

    while (1) {
        sem_wait(&channels[channel_id].full); // Okunacak frame var mı?
        sem_wait(&channels[channel_id].lock); // Kritik bölgeye gir

        int index = channels[channel_id].out_index;
        frame_buffer[0] = '\0';
        for (int i = 0; i < FRAME_LINES; i++) {
            if (channels[channel_id].frames[index][i][0] == '\0') break;
            strcat(frame_buffer, channels[channel_id].frames[index][i]);
        }

        channels[channel_id].out_index = (channels[channel_id].out_index + 1) % FRAME_BUFFER_SIZE;

        sem_post(&channels[channel_id].lock); // Kilidi bırak
        sem_post(&channels[channel_id].empty); // Boş yer arttı

        strcat(frame_buffer, "\f"); // Sayfa sonu (frame ayracı) karakteri

        // Frame'i istemciye gönder
        if (send(client_fd, frame_buffer, strlen(frame_buffer), 0) <= 0) {
            printf("Bir istemci bağlantıyı kapattı.\n");
            break;
        }

        usleep(FRAME_INTERVAL_USEC); // Frame'ler arası gecikme
    }

    close(client_fd);
    return NULL;
}

int main(int argc, char* argv[]) {
    // Gerekli argüman kontrolü
    if (argc < 5) {
        fprintf(stderr, "Kullanım: %s -p port -s stream_count -ch1 file1 [-ch2 file2] [-ch3 file3]\n", argv[0]);
        return EXIT_FAILURE;
    }

    char* filenames[MAX_CHANNELS] = {NULL};
    // Argümanları parse et
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            server_port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            stream_count = atoi(argv[++i]);
        } else if (strncmp(argv[i], "-ch", 3) == 0 && i + 1 < argc) {
            int ch = argv[i][3] - '1';
            if (ch >= 0 && ch < MAX_CHANNELS) {
                filenames[ch] = argv[++i];
            }
        }
    }

    // Her kanal için producer thread'i başlat
    for (int i = 0; i < stream_count; i++) {
        if (!filenames[i]) {
            fprintf(stderr, "Kanal %d için video dosyası belirtilmedi.\n", i + 1);
            return EXIT_FAILURE;
        }

        // Kanal yapılarını başlat
        strncpy(channels[i].filename, filenames[i], 127);
        channels[i].in_index = 0;
        channels[i].out_index = 0;
        sem_init(&channels[i].empty, 0, FRAME_BUFFER_SIZE);
        sem_init(&channels[i].full, 0, 0);
        sem_init(&channels[i].lock, 0, 1);

        // Producer thread başlat
        ProducerArg* arg = malloc(sizeof(ProducerArg));
        arg->channel_id = i;
        pthread_t tid;
        pthread_create(&tid, NULL, producer_thread, arg);
    }

    // Sunucu soketini oluştur
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in server_addr = {.sin_family = AF_INET, .sin_port = htons(server_port), .sin_addr.s_addr = INADDR_ANY};
    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, MAX_CLIENTS);
    printf("Sunucu %d portunda %d kanal ile dinleniyor...\n", server_port, stream_count);

    // İstemcileri dinle
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &len);
        if (client_fd < 0) continue;

        // Her istemci için yeni thread
        int* arg = malloc(sizeof(int));
        *arg = client_fd;
        pthread_t tid;
        pthread_create(&tid, NULL, client_handler, arg);
    }

    close(server_fd);
    return 0;
}
