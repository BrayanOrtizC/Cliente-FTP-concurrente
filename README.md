(Se agregó al repositorio también los archivos facilitados por el profesor solo como respaldo).


Cliente FTP Concurrente – RFC 959

Este proyecto implementa un cliente FTP concurrente en C capaz de conectarse a un servidor FTP (como vsftpd), enviar comandos del RFC 959, manejar conexiones de datos en modos PASV y PORT, y realizar transferencias múltiples mediante procesos hijo (fork).

Incluye los siguientes comandos:

USER, PASS
CWD, PWD
LIST
RETR, STOR
PASV, PORT
mget, mput (transferencia concurrente)
MKD, RMD, DELE

Compatible con vsftpd en Linux / WSL2.

1. Requisitos
Editar /etc/vsftpd.conf

local_enable=YES
write_enable=YES
chroot_local_user=YES
allow_writeable_chroot=YES
local_umask=022

2. Descripción de comandos disponibles

user <nombre>
pass <contraseña>
login

cd <directorio>    # CWD
pwd                # PWD

ls
list

get <archivo>      # RETR
put <archivo>      # STOR

mget y mput (Para transferencia de datos concurrentes)
Ejemplo: mget a.txt b.txt c.txt
Cada proceso hijo crea una nueva sesión FTP y usa el directorio inicial del usuario,
no el directorio de la sesión principal.

mkdir <dir>    # MKD
rmdir <dir>    # RMD
dele <archivo> # DELE

quit
exit
bye

3. Modos de transferencia

mode pasv
El cliente se conecta al puerto de datos del servidor.

mode port
El cliente abre un puerto y el servidor se conecta a él.


4. Estructura del cliente
El proyecto utiliza:

connectTCP.c
connectsock.c
errexit.c
ftp_client.c (archivo principal)
Makefile

5. Ejemplo de sesión completa

./ftpclient 127.0.0.1 21
220 (vsFTPd 3.0.5)
ftp> user brayan
331 Please specify the password.
ftp> pass 1234
230 Login successful.
ftp> mode pasv
Modo: PASV
ftp> cd ftp-test
250 Directory successfully changed.
ftp> put subir.txt
Archivo 'subir.txt' subido correctamente.
ftp> ls
-rw-r--r-- ... subir.txt
ftp> mkdir nuevo
257 "/home/brayan/nuevo" created
ftp> dele subir.txt
250 Delete operation successful.
ftp> quit
221 Goodbye.

6. Consideraciones finales

El cliente implementa dos modos de transferencia: PASV y PORT.
El modo PORT fue probado y es funcional.
La concurrencia está implementada con procesos hijo (fork()).
Los comandos básicos del RFC 959 y algunos extendidos están implementados.
mget/mput operan en el directorio inicial remoto debido a la naturaleza independiente de las sesiones FTP.
