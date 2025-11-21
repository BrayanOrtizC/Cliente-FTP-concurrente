/* ftp_client.c - Cliente FTP concurrente con soporte PASV/PORT, LIST, RETR, STOR
 *
 * Comandos especiales adicionales:
 *   mkdir <dir>  -> MKD
 *   rmdir <dir>  -> RMD
 *   dele <file>  -> DELE
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>

int connectTCP(const char *host, const char *service);
int errexit(const char *format, ...);

static int transfer_mode = 0; /* 0 = PASV, 1 = PORT */
static char g_user[128] = "";
static char g_pass[128] = "";

/* ---------------- utilidades ---------------- */

ssize_t recvline(int sock, char *buf, size_t maxlen) {
    size_t n = 0;
    char c;
    ssize_t r;

    while (n < maxlen - 1) {
        r = recv(sock, &c, 1, 0);
        if (r == 1) {
            buf[n++] = c;
            if (c == '\n') break;
        } else if (r == 0) break;
        else return -1;
    }
    buf[n] = '\0';
    return n;
}

int send_cmd(int ctrl, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    size_t len = strlen(buf);
    if (len < 2 || buf[len-2] != '\r' || buf[len-1] != '\n') {
        buf[len++] = '\r';
        buf[len++] = '\n';
        buf[len] = '\0';
    }

    return send(ctrl, buf, strlen(buf), 0);
}

int read_reply(int sock, char *out, size_t outsz) {
    char line[1024];
    int code = 0;
    int first = 1;

    if (out && outsz > 0) out[0] = '\0';

    while (1) {
        ssize_t n = recvline(sock, line, sizeof(line));
        if (n <= 0) return -1;

        if (out && outsz > strlen(out) + 1)
            strncat(out, line, outsz - strlen(out) - 1);

        if (first) {
            if (isdigit((unsigned char)line[0]) &&
                isdigit((unsigned char)line[1]) &&
                isdigit((unsigned char)line[2])) {
                code = (line[0]-'0')*100 + (line[1]-'0')*10 + (line[2]-'0');
            }
            first = 0;
        }

        if (strlen(line) >= 4 && line[3] == ' ')
            break;
    }
    return code;
}

/* -------- PARSE PASV -------- */

int parse_pasv_response(const char *resp, char *host, size_t hostsz, int *port) {
    int h1,h2,h3,h4,p1,p2;
    const char *p = strchr(resp, '(');
    if (!p) return -1;

    if (sscanf(p+1, "%d,%d,%d,%d,%d,%d",
               &h1,&h2,&h3,&h4,&p1,&p2) != 6)
        return -1;

    snprintf(host, hostsz, "%d.%d.%d.%d", h1,h2,h3,h4);
    *port = p1*256 + p2;
    return 0;
}

int open_data_pasv(int ctrl) {
    char resp[1024];
    send_cmd(ctrl, "PASV");
    int code = read_reply(ctrl, resp, sizeof(resp));
    if (code != 227) {
        fprintf(stderr, "Error PASV: %d\n", code);
        return -1;
    }

    char host[64];
    int port;
    if (parse_pasv_response(resp, host, sizeof(host), &port) < 0)
        return -1;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    return connectTCP(host, portstr);
}

/* -------- PORT -------- */

int open_data_port(int ctrl) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;

    bind(listenfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(listenfd, 1);

    socklen_t len = sizeof(addr);
    getsockname(listenfd, (struct sockaddr*)&addr, &len);

    int port = ntohs(addr.sin_port);

    struct sockaddr_in local;
    socklen_t l2 = sizeof(local);
    getsockname(ctrl, (struct sockaddr*)&local, &l2);

    unsigned char *ip = (unsigned char*)&local.sin_addr.s_addr;

    int p1 = port / 256;
    int p2 = port % 256;

    char arg[64];
    snprintf(arg, sizeof(arg),
             "%d,%d,%d,%d,%d,%d",
             ip[0], ip[1], ip[2], ip[3], p1, p2);

    char resp[1024];
    send_cmd(ctrl, "PORT %s", arg);
    int code = read_reply(ctrl, resp, sizeof(resp));
    if (code != 200) {
        close(listenfd);
        return -1;
    }

    return listenfd;
}

/* -------- LIST -------- */

int ftp_list(int ctrl) {
    int data=-1, listenfd=-1;
    char resp[1024];
    int code;

    if (transfer_mode == 0)
        data = open_data_pasv(ctrl);
    else
        listenfd = open_data_port(ctrl);

    if (data < 0 && listenfd < 0) return -1;

    send_cmd(ctrl, "LIST");
    code = read_reply(ctrl, resp, sizeof(resp));
    if (code != 150 && code != 125) return -1;

    if (transfer_mode == 1) {
        struct sockaddr_in cli;
        socklen_t l = sizeof(cli);
        data = accept(listenfd, (struct sockaddr*)&cli, &l);
        close(listenfd);
    }

    char buf[4096];
    ssize_t n;
    while ((n = recv(data, buf, sizeof(buf), 0)) > 0)
        write(STDOUT_FILENO, buf, n);

    close(data);
    read_reply(ctrl, resp, sizeof(resp));
    return 0;
}

/* -------- RETR -------- */

int ftp_retr(int ctrl, const char *file) {
    int data=-1, listenfd=-1;
    char resp[1024];

    if (transfer_mode == 0)
        data = open_data_pasv(ctrl);
    else
        listenfd = open_data_port(ctrl);

    send_cmd(ctrl, "RETR %s", file);
    int code = read_reply(ctrl, resp, sizeof(resp));
    if (code != 150 && code != 125) return -1;

    if (transfer_mode == 1) {
        struct sockaddr_in cli;
        socklen_t l=sizeof(cli);
        data = accept(listenfd, (struct sockaddr*)&cli, &l);
        close(listenfd);
    }

    int fd = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0666);
    char buf[4096];
    ssize_t n;
    while ((n = recv(data, buf, sizeof(buf), 0)) > 0)
        write(fd, buf, n);

    close(fd);
    close(data);
    read_reply(ctrl, resp, sizeof(resp));

    printf("Archivo '%s' descargado correctamente.\n", file);
    return 0;
}

/* -------- STOR -------- */

int ftp_stor(int ctrl, const char *file) {
    int data=-1, listenfd=-1;
    char resp[1024];

    if (transfer_mode == 0)
        data = open_data_pasv(ctrl);
    else
        listenfd = open_data_port(ctrl);

    int fd = open(file, O_RDONLY);
    if (fd < 0) return -1;

    send_cmd(ctrl, "STOR %s", file);
    int code = read_reply(ctrl, resp, sizeof(resp));
    if (code != 150 && code != 125) return -1;

    if (transfer_mode == 1) {
        struct sockaddr_in cli;
        socklen_t l=sizeof(cli);
        data = accept(listenfd, (struct sockaddr*)&cli, &l);
        close(listenfd);
    }

    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        send(data, buf, n, 0);

    close(fd);
    close(data);
    read_reply(ctrl, resp, sizeof(resp));

    printf("Archivo '%s' subido correctamente.\n", file);
    return 0;
}

/* -------- LOGIN -------- */

int ftp_login(int ctrl, const char *u, const char *p) {
    char resp[1024];
    send_cmd(ctrl, "USER %s", u);
    int code = read_reply(ctrl, resp, sizeof(resp));

    if (code == 331) {
        send_cmd(ctrl, "PASS %s", p);
        code = read_reply(ctrl, resp, sizeof(resp));
    }

    printf("%s", resp);
    return (code == 230 ? 0 : -1);
}

/* -------- CONCURRENCIA -------- */

void child_transfer(const char *host, const char *port,
                    const char *fname, int is_get) {

    if (g_user[0]=='\0'||g_pass[0]=='\0') _exit(1);

    int cs = connectTCP(host, port);
    if (cs < 0) _exit(1);

    char resp[1024];
    read_reply(cs, resp, sizeof(resp));

    if (ftp_login(cs, g_user, g_pass) < 0) _exit(1);

    int saved = transfer_mode;
    transfer_mode = 0;

    if (is_get) ftp_retr(cs, fname);
    else        ftp_stor(cs, fname);

    transfer_mode = saved;

    send_cmd(cs, "QUIT");
    read_reply(cs, resp, sizeof(resp));
    close(cs);
    _exit(0);
}

/* -------- AUX -------- */

void trim(char *s) {
    size_t n=strlen(s);
    while(n>0 && (s[n-1]=='\n'||s[n-1]=='\r')) s[--n]='\0';
}

/* -------------- MAIN -------------- */

int main(int argc, char *argv[]) {

    if (argc != 3)
        errexit("Uso: %s <host> <port>\n", argv[0]);

    const char *host = argv[1];
    const char *port = argv[2];

    int ctrl = connectTCP(host, port);
    if (ctrl < 0)
        errexit("No se pudo conectar.\n");

    char resp[2048];
    int code = read_reply(ctrl, resp, sizeof(resp));
    printf("%s", resp);

    char line[512], cmd[64], args[256];

    while (1) {

        printf("ftp> ");
        if (!fgets(line, sizeof(line), stdin)) break;

        trim(line);
        if (strlen(line)==0) continue;

        cmd[0]=args[0]='\0';
        sscanf(line, "%63s %255[^\n]", cmd, args);

        for(char *p=cmd;*p;++p)
            *p=tolower((unsigned char)*p);

        /* ---------- USER ---------- */
        if (strcmp(cmd,"user")==0) {
            strncpy(g_user, args, sizeof(g_user)-1);
            send_cmd(ctrl,"USER %s", g_user);
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);

        /* ---------- PASS ---------- */
        } else if (strcmp(cmd,"pass")==0) {
            strncpy(g_pass, args, sizeof(g_pass)-1);
            send_cmd(ctrl,"PASS %s", g_pass);
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);

        /* ---------- LOGIN ---------- */
        } else if (strcmp(cmd,"login")==0) {
            ftp_login(ctrl,g_user,g_pass);

        /* ---------- CD ---------- */
        } else if (strcmp(cmd,"cd")==0) {
            send_cmd(ctrl,"CWD %s", args);
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);

        /* ---------- PWD ---------- */
        } else if (strcmp(cmd,"pwd")==0) {
            send_cmd(ctrl,"PWD");
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);

        /* ---------- LIST ---------- */
        } else if (strcmp(cmd,"ls")==0 || strcmp(cmd,"list")==0) {
            ftp_list(ctrl);

        /* ---------- MODE ---------- */
        } else if (strcmp(cmd,"mode")==0) {
            if (strcasecmp(args,"pasv")==0) {
                transfer_mode=0;
                printf("Modo: PASV\n");
            } else if (strcasecmp(args,"port")==0) {
                transfer_mode=1;
                printf("Modo: PORT\n");
            } else {
                printf("Uso: mode pasv|port\n");
            }

        /* ---------- GET ---------- */
        } else if (strcmp(cmd,"get")==0) {
            ftp_retr(ctrl,args);

        /* ---------- PUT ---------- */
        } else if (strcmp(cmd,"put")==0) {
            ftp_stor(ctrl,args);

        /* ---------- MGET ---------- */
        } else if (strcmp(cmd,"mget")==0) {
            char *t=strtok(args," ");
            while(t){
                if(fork()==0) child_transfer(host,port,t,1);
                t=strtok(NULL," ");
            }

        /* ---------- MPUT ---------- */
        } else if (strcmp(cmd,"mput")==0) {
            char *t=strtok(args," ");
            while(t){
                if(fork()==0) child_transfer(host,port,t,0);
                t=strtok(NULL," ");
            }

        /* ---------- MKD ---------- */
        } else if (strcmp(cmd,"mkdir")==0) {
            if (args[0]=='\0') {
                printf("Uso: mkdir <directorio>\n");
                continue;
            }
            send_cmd(ctrl,"MKD %s",args);
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);

        /* ---------- RMD ---------- */
        } else if (strcmp(cmd,"rmdir")==0) {
            if (args[0]=='\0') {
                printf("Uso: rmdir <directorio>\n");
                continue;
            }
            send_cmd(ctrl,"RMD %s",args);
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);

        /* ---------- DELE ---------- */
        } else if (strcmp(cmd,"dele")==0) {
            if (args[0]=='\0') {
                printf("Uso: dele <archivo>\n");
                continue;
            }
            send_cmd(ctrl,"DELE %s",args);
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);

        /* ---------- QUIT ---------- */
        } else if (!strcmp(cmd,"quit") ||
                   !strcmp(cmd,"exit") ||
                   !strcmp(cmd,"bye")) {

            send_cmd(ctrl,"QUIT");
            read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);
            break;

        /* ---------- COMANDO GENÉRICO ---------- */
        } else {
            if(args[0]!='\0') send_cmd(ctrl,"%s %s",cmd,args);
            else send_cmd(ctrl,"%s",cmd);
            code=read_reply(ctrl,resp,sizeof(resp));
            printf("%s",resp);
        }
    }

    close(ctrl);
    return 0;
}

