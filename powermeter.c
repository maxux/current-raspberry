#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#define DEFAULT_BAUDRATE    B9600
#define DEFAULT_DEVICE      "/dev/ttyACM0"
#define AVERAGE_SIZE        3
#define PHASE_MAXID         32
#define SERVER_TARGET       "10.241.0.254"
#define SERVER_PORT         30502

typedef struct phase_t {
    float average;
    float values[AVERAGE_SIZE];
    size_t index;

} phase_t;

void diep(char *str) {
    fprintf(stderr, "[-] %s: [%d] %s\n", str, errno, strerror(errno));
    exit(EXIT_FAILURE);
}

int set_interface_attribs(int fd, int speed) {
    struct termios tty;

    memset(&tty, 0, sizeof(tty));

    if(tcgetattr(fd, &tty) != 0)
        diep("tcgetattr");

    tty.c_cflag = speed | CRTSCTS | CS8 | CLOCAL | CREAD;
    tty.c_iflag = IGNPAR | ICRNL;
    tty.c_lflag = ICANON;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    if(tcsetattr(fd, TCSANOW, &tty) != 0)
        diep("tcgetattr");

    return 0;
}

char *readline(int fd, char *buffer, size_t blen) {
    char reader;
    size_t current = 0;

    memset(buffer, 0, blen);

    while(1) {
        if(read(fd, &reader, 1) < 1) {
            perror("[-] read error");
            usleep(100000);
            continue;
        }

        if(current == 0 && reader == '\n')
            continue;

        buffer[current++] = reader;

        if(strchr(buffer, '\n')) {
            buffer[current - 2] = '\0';
            return buffer;
        }

        // buffer is full
        if(current == blen - 1) {
            buffer[current] = '\0';
            return buffer;
        }
    }
}

int http(int phase, float value) {
    int sockfd;
    struct sockaddr_in addr_remote;
    struct hostent *hent;
    char payload[512];

    addr_remote.sin_family      = AF_INET;
    addr_remote.sin_port        = htons(SERVER_PORT);

    if((hent = gethostbyname(SERVER_TARGET)) == NULL)
        diep("[-] gethostbyname");

    memcpy(&addr_remote.sin_addr, hent->h_addr_list[0], hent->h_length);

    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        diep("[-] socket");

    if(connect(sockfd, (const struct sockaddr *) &addr_remote, sizeof(addr_remote)) < 0)
        diep("[-] connect");

    sprintf(payload, "GET /power/%ld/%d/%.2f HTTP/1.0\r\n\r\n", time(NULL), phase, value);

    if(send(sockfd, payload, strlen(payload), 0) < 0)
        diep("[-] send");

    close(sockfd);

    return 0;
}

float average(size_t size, float *values) {
    float value = 0;

    for(size_t i = 0; i < size; i++)
        value += values[i];

    value /= size;

    return value;
}

int main(void) {
    int fd;
    char buffer[256], *pointer;
    phase_t **phases = NULL;

    // open arduino serial
    if((fd = open(DEFAULT_DEVICE, O_RDWR | O_NOCTTY)) < 0)
        diep(DEFAULT_DEVICE);

    set_interface_attribs(fd, DEFAULT_BAUDRATE);

    // initialize phases
    if(!(phases = (phase_t **) malloc(sizeof(phase_t *) * PHASE_MAXID)))
        diep("phases malloc");

    for(size_t i = 0; i < PHASE_MAXID; i++) {
        if(!(phases[i] = malloc(sizeof(phase_t))))
            diep("phase malloc");
    }

    // initial read
    readline(fd, buffer, sizeof(buffer));
    printf("[+] initial skip: %s\n", buffer);

    // main loop
    while(1) {
        readline(fd, buffer, sizeof(buffer));
        printf("[+] raw input: %s\n", buffer);

        if(buffer[0] != 'P') {
            fprintf(stderr, "[-] malformed line: missing phase id\n");
            continue;
        }

        if(!(pointer = strchr(buffer, ':'))) {
            fprintf(stderr, "[-] malformed line: missing value\n");
            continue;
        }

        int phaseid = atoi(buffer + 1);
        float value = atof(pointer + 1);

        if(phaseid > PHASE_MAXID) {
            fprintf(stderr, "[-] malformed line: phase out of range\n");
            continue;
        }

        phase_t *phase = phases[phaseid];
        phase->values[phase->index] = value;
        phase->index += 1;

        if(phase->index == AVERAGE_SIZE) {
            phase->average = average(AVERAGE_SIZE, phase->values);
            printf("[+] average: %.2f\n", phase->average);

            http(phaseid, phase->average);
            phase->index = 0;
        }
    }

    return 0;
}

