#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h> // Добавлено для fstat
#include <errno.h>



#define PORT 8080
#define BUFFER_SIZE 4096
#define MAX_CONNECTIONS 10
#define MAX_FILE_SIZE (128 * 1024 * 1024) // 128 MB

void handle_request(int client_socket);
void send_response(int client_socket, const char *status, const char *content_type, const char *body);
void log_event(const char *message);
void sigchld_handler(int s);





int main() {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;

    // Создание серверного сокета
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Ошибка открытия сокета");
        exit(1);
    }

    // Настройка адреса сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // Привязка сокета к адресу
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Ошибка привязки сокета");
        exit(1);
    }

    // Прослушивание входящих соединений
    listen(server_socket, BACKLOG);

    // Установка обработчика сигнала для завершившихся процессов
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // Обработчик сигнала
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);

    // Создание дочерних процессов
    for (int i = 0; i < NUM_WORKERS; i++) {
        if (fork() == 0) { // Дочерний процесс
            while (1) {
            
            
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_socket, &read_fds);

        // Используем pselect для ожидания новых соединений
            int activity = pselect(server_socket + 1, &read_fds, NULL, NULL, NULL, NULL);
            if (activity < 0) {
                if (errno == EINTR) {
                    continue; // Прерывание вызова pselect()
                } else {
                    perror("pselect error");
                    continue;
                }
            }

        if (FD_ISSET(server_socket, &read_fds)) {
            int client_socket;
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            // Прием входящего соединения
            if ((client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len)) < 0) {
                perror("Accept failed");
                continue;
            }

            // Создание дочернего процесса для обработки запроса
            if (fork() == 0) {
                close(server_socket); // Дочерний процесс не нуждается в серверном сокете
                handle_request(client_socket);
                close(client_socket);
                exit(0);
            }
            
            close(client_socket); // Родительский процесс закрывает сокет клиента
        }
            exit(0); // Завершение дочернего процесса
        }
    }

    // Родительский процесс закрывает серверный сокет и ждет завершения дочерних процессов
    close(server_socket);
    while (1) {
        pause(); // Ожидание сигналов
    }

    return 0;
}

void handle_request(int client_socket) {
    char buffer[BUFFER_SIZE];
    read(client_socket, buffer, sizeof(buffer));

    char method[8], path[256];
    sscanf(buffer, "%s %s", method, path);

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        send_response(client_socket, "405 Method Not Allowed", "text/plain", "Method Not Allowed");
        return;
    }

    // Удаление начального слэша
    char *file_path = path + 1; // Пропускаем начальный слэш
    if (strlen(file_path) == 0) {
        file_path = "index.html"; // Предположим, что мы хотим вернуть index.html
    }

    // Открытие файла
    int file_fd = open(file_path, O_RDONLY);
    if (file_fd < 0) {
        perror("File open error"); // Логгирование ошибки
        send_response(client_socket, "404 Not Found", "text/plain", "File Not Found");
        return;
    }

    // Получение информации о файле
    struct stat file_stat;
    if (fstat(file_fd, &file_stat) < 0) {
        send_response(client_socket, "403 Forbidden", "text/plain", "Forbidden");
        close(file_fd);
        return;
    }
    if (file_stat.st_size > MAX_FILE_SIZE) {
        send_response(client_socket, "403 Forbidden", "text/plain", "File Too Large");
        close(file_fd);
        return;
    }

    // Отправка заголовков
    send_response(client_socket, "200 OK", "text/html", "");

    // Если метод GET, отправляем содержимое файла
    if (strcmp(method, "GET") == 0) {
        char file_buffer[BUFFER_SIZE];
        ssize_t bytes_read;

        while ((bytes_read = read(file_fd, file_buffer, sizeof(file_buffer))) > 0) {
            write(client_socket, file_buffer, bytes_read);
        }
    }

    close(file_fd);
}


void send_response(int client_socket, const char *status, const char *content_type, const char *body) {
    // Формируем заголовок ответа
    char response[BUFFER_SIZE];
    int response_length;

    // Заголовки HTTP
    response_length = snprintf(response, sizeof(response),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status, content_type, strlen(body));

    // Отправляем заголовки клиенту
    write(client_socket, response, response_length);

    // Открываем файл для записи
    FILE *log_file = fopen("server.log", "a");
    if (log_file != NULL) {
        // Записываем статус ответа в файл
        fprintf(log_file, "Response sent: %s\n", status);
        fclose(log_file); // Закрываем файл
    } else {
        perror("Failed to open log file");
    }

    // Выводим статус ответа на консоль
    printf("Response sent: %s\n", status);
    fflush(stdout); // Сбрасываем буфер вывода

    // Отправляем тело ответа (если оно есть)
    if (strlen(body) > 0) {
        write(client_socket, body, strlen(body));
    }
}


void log_event(const char *message) {
    FILE *log_file = fopen("server.log", "a");
    if (log_file) {
        fprintf(log_file, "%s\n", message);
        fclose(log_file);
    }
}

void sigchld_handler(int s) {
    while(waitpid(-1, NULL, WNOHANG) > 0);
}
