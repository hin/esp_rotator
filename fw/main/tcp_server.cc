#include <esp_log.h>

#include <lwip/err.h>
#include <lwip/sockets.h>
#include <lwip/sys.h>
#include <lwip/netdb.h>

#include "rotator.h"

static const char *TAG = "tcpsrv";

#define PORT 4533
#define MAX_SESSIONS 16

struct session_state {
    int sock;

    char line_buf[1024];
    int line_pos;
    Rotator *rotator;
};

struct session_state sessions[MAX_SESSIONS];

static void session_start(struct session_state *session)
{
    session->line_pos = 0;
    return;
}

static void session_close(struct session_state *session)
{
    close(session->sock);
    session->sock = -1;
}

static void session_parse_line(struct session_state *session)
{
    char buf[128];
    //ESP_LOGI(TAG, "Line: >>%s<<", session->line_buf);

    if (session->line_buf[0] == 'p')
    {
        float azi, ele;
        session->rotator->get_position(&azi, &ele);
        snprintf(buf, sizeof buf, "%1.2f\n%1.2f\n", azi, ele);
        write(session->sock, buf, strlen(buf));
        //ESP_LOGI(TAG, "response: >>%s<<", buf);
    } else if(session->line_buf[0] == 'P')
    {
        char cmd[100];
        float azi, ele;
        sscanf(session->line_buf, "%s %f %f", cmd, &azi, &ele);
//        ESP_LOGI(TAG, ">>>>> %f/%f", azi, ele);
        session->rotator->set_position(azi, ele);
        snprintf(buf, sizeof buf, "RPRT 0\n");
        write(session->sock, buf, strlen(buf));
//        ESP_LOGI(TAG, "response: >>%s<<", buf);
    } else {
        snprintf(buf, sizeof buf, "RPRT 0\n");
        write(session->sock, buf, strlen(buf));
        ESP_LOGI(TAG, "response: >>%s<<", buf);
    }
}

static void session_read(struct session_state *session)
{
    char read_buf[1024];
    ssize_t len = read(session->sock, read_buf, sizeof read_buf);
    if (len < 0)
    {
        ESP_LOGE(TAG, "read on socket %d: %d %s", session->sock, len, strerror(errno));
        session_close(session);
        return;
    }

    if (len == 0)
    {
        ESP_LOGI(TAG, "session closed, socket=%d", session->sock);
        session_close(session);
        return;
    }

    //ESP_LOGI(TAG, "received >>%.*s<< len=%d", len, read_buf, len);

    for (int i=0; i < len; i++)
    {
        if (read_buf[i] == '\n') {
            session->line_buf[session->line_pos++] = '\0';
            session_parse_line(session);
            session->line_pos = 0;
        } else {
            session->line_buf[session->line_pos++] = read_buf[i];
        }
    }

    return;
}

void tcp_server_task(void *pvParameters)
{
    ESP_LOGI(TAG, "starting");

    Rotator *rotator = (Rotator *)pvParameters;

    for(int i=0; i < MAX_SESSIONS; i++)
    {
        sessions[i].sock = -1;
        sessions[i].rotator = rotator;
    }

    struct sockaddr_in6 dest_addr;

    struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(PORT);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Listener socket created: %d", listen_sock);

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0)
    {
        ESP_LOGE(TAG, "Socket unable to bind: %s", strerror(errno));
        goto clean_up;
    }
    ESP_LOGI(TAG, "Socket bound to port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: %s", strerror(errno));
        goto clean_up;
    }

    while(1) {
        //ESP_LOGI(TAG, "Socket listening");

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_sock, &rfds);
        int max_socket = listen_sock;

        for (int i=0; i < MAX_SESSIONS; i++)
        {
            if(sessions[i].sock != -1) {
                FD_SET(sessions[i].sock, &rfds);
                if (sessions[i].sock > max_socket)
                    max_socket = sessions[i].sock;
            }
        }

        int err = select(max_socket+1, &rfds, NULL, NULL, NULL);
        //ESP_LOGI(TAG, "select(): %d", err);

        if (FD_ISSET(listen_sock, &rfds)) {
            struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
            uint addr_len = sizeof(source_addr);
            int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
            if (sock < 0) {
                ESP_LOGE(TAG, "accept(): %s", strerror(errno));
                break;
            }

            // Convert ip address to string
            char addr_str[128];
            if (source_addr.sin6_family == PF_INET) {
                inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
            } else if (source_addr.sin6_family == PF_INET6) {
                inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
            }
            ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

            bool stored = false;
            for (int i=0; i < MAX_SESSIONS; i++)
            {
                if (sessions[i].sock == -1)
                {
                    sessions[i].sock = sock;
                    //ESP_LOGI(TAG, "Session %d started", i);
                    session_start(&sessions[i]);
                    stored = true;
                    break;
                }
            }
            if (!stored)
            {
                ESP_LOGE(TAG, "Too many sessions (max %d)", MAX_SESSIONS);
                close(sock);
            }
        }
        for(int i=0; i < MAX_SESSIONS; i++)
        {
            if (FD_ISSET(sessions[i].sock, &rfds))
                session_read(&sessions[i]);
        }

    }

clean_up:
    close(listen_sock);
    vTaskDelete(NULL);
}
