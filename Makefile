# -----------------------------------------
# Makefile para el cliente FTP concurrente
# -----------------------------------------

CC = gcc
CFLAGS = -Wall -O2
OBJ = ftp_client.o connectTCP.o connectsock.o errexit.o
TARGET = ftpclient

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

ftp_client.o: ftp_client.c
	$(CC) $(CFLAGS) -c ftp_client.c

connectTCP.o: connectTCP.c
	$(CC) $(CFLAGS) -c connectTCP.c

connectsock.o: connectsock.c
	$(CC) $(CFLAGS) -c connectsock.c

errexit.o: errexit.c
	$(CC) $(CFLAGS) -c errexit.c

clean:
	rm -f *.o $(TARGET)

# -----------------------------------------
# Makefile para el cliente FTP concurrente
# -----------------------------------------

CC = gcc
CFLAGS = -Wall -O2
OBJ = ftp_client.o connectTCP.o connectsock.o errexit.o
TARGET = ftpclient

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJ)

ftp_client.o: ftp_client.c
	$(CC) $(CFLAGS) -c ftp_client.c

connectTCP.o: connectTCP.c
	$(CC) $(CFLAGS) -c connectTCP.c

connectsock.o: connectsock.c
	$(CC) $(CFLAGS) -c connectsock.c

errexit.o: errexit.c
	$(CC) $(CFLAGS) -c errexit.c

clean:
	rm -f *.o $(TARGET)

