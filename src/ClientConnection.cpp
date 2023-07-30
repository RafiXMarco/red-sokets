//****************************************************************************
//                         REDES Y SISTEMAS DISTRIBUIDOS
//                      
//                     2º de grado de Ingeniería Informática
//                       
//              This class processes an FTP transactions.
// 
//****************************************************************************


#include <array>
#include <cstring>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cerrno>
#include <filesystem>
#include <experimental/filesystem>
#include <fstream>
#include <iostream>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netinet/in.h>

#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <langinfo.h>
#include <locale.h>
#include <netdb.h>
#include <pwd.h>
#include <system_error>
#include <time.h>
#include <unistd.h>

#include "ClientConnection.h"




ClientConnection::ClientConnection(int s) {
    int sock = (int)(s);
  
    char buffer[MAX_BUFF];

    control_socket = s;
    // Check the Linux man pages to know what fdopen does.
    fd = fdopen(s, "a+");
    if (fd == NULL){
	std::cout << "Connection closed" << std::endl;

	fclose(fd);
	close(control_socket);
	ok = false;
	return ;
    }
    
    ok = true;
    data_socket = -1;
    parar = false;
   
  
  
};

ClientConnection::~ClientConnection()
{
 	fclose(fd);
	close(control_socket);
}


int connect_TCP(uint32_t ip_address, uint16_t port)
{
    struct sockaddr_in address{};

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = ip_address;
    address.sin_port = port;

    int socketFd = socket(AF_INET, SOCK_STREAM, 0);

    if(socketFd < 0)
    {
        throw std::system_error(errno, std::system_category(), "Cannot create socket");
    }

    if(connect(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0)
    {
        throw std::system_error(errno, std::system_category(), "Cannot connect to server");
    }

    return socketFd;
}

void ClientConnection::stop()
{
    close(data_socket);
    close(control_socket);
    parar = true;
}



    
#define COMMAND(cmd) strcmp(command, cmd)==0

// This method processes the requests.
// Here you should implement the actions related to the FTP commands.
// See the example for the USER command.
// If you think that you have to add other commands feel free to do so. You 
// are allowed to add auxiliary methods if necessary.

void ClientConnection::WaitForRequests() {
    if (!ok)
    {
        return;
    }

    namespace fs = std::experimental::filesystem;
    fs::path current_dir(fs::current_path());
    
    fprintf(fd, "220 Service ready\n");
  
    while(!parar) {

      fscanf(fd, "%s", command);
      if (COMMAND("USER")) {
	    fscanf(fd, "%s", arg);
	    fprintf(fd, "331 User name ok, need password\n");
      }
      else if (COMMAND("PWD")) {
	   fprintf(fd, "257 \"%s\" is current directory.\n", current_dir.c_str());
      }
      else if (COMMAND("PASS")) {
        fscanf(fd, "%s", arg);
        if(strcmp(arg,"1234") == 0){
            fprintf(fd, "230 User logged in\n");
        }
        else{
            fprintf(fd, "530 Not logged in.\n");
            parar = true;
        }
	   
      }
      else if (COMMAND("PORT")) {
	  unsigned int h0, h1, h2, h3;
            unsigned int p0, p1;

            fscanf(fd, "%u, %u, %u, %u, %u, %u", &h0, &h1, &h2, &h3, &p0, &p1);

            uint32_t address = h3 << 24u | h2 << 16u | h1 << 8u | h0;
            uint16_t port = p1 << 8u | p0;
            data_socket = connect_TCP(address, port);

            if(data_socket < 0)
            {
                fprintf(fd, "421 Service not available, closing control connection.\n");
            }
            else
            {
                fprintf(fd, "200 OK\n");
            }
      }
      else if (COMMAND("PASV")) {
	  int socketFd = socket(AF_INET,SOCK_STREAM, 0);

            if (socketFd < 0)
            {
                throw std::system_error(errno, std::system_category(), "Cannot create socket");
            }

            struct sockaddr_in input_addr{};
            memset(&input_addr, 0, sizeof(input_addr));
            input_addr.sin_family = AF_INET;
            inet_pton(AF_INET, "127.0.0.1", &input_addr.sin_addr.s_addr);
            input_addr.sin_port = 0;

            struct sockaddr_in output_addr{};
            socklen_t od_len = sizeof(output_addr);

            if(bind(socketFd, reinterpret_cast<sockaddr*>(&input_addr), sizeof(input_addr)) < 0)
            {
                throw std::system_error(errno, std::system_category(), "Cannot bind socket");
            }
            if (listen(socketFd, 128) < 0)
            {
                throw std::system_error(errno, std::system_category(), "Cannot listen to socket");
            }

            getsockname(socketFd, reinterpret_cast<sockaddr*>(&output_addr), &od_len);

            unsigned short p0 = (output_addr.sin_port & 0xff), p1 = (output_addr.sin_port >> 8);

            fprintf(fd, "227 Entering Passive Mode (%u,%u,%u,%u,%u,%u).\n", 127u, 0u, 0u, 1u, p0, p1);
            fflush(fd);
            
            data_socket = accept(socketFd, reinterpret_cast<sockaddr*>(&output_addr), &od_len);
      }
      else if (COMMAND("STOR") ) {
	    fscanf(fd, "%s", arg);

            std::array<char, 1024> buffer;
            std::ofstream file(current_dir.string() + '/' + arg);
            int bytes;

            if(file.good())
            {
                fprintf(fd, "150 File status okay; about to open data connection.\n");
                fflush(fd);
                do
                {
                    bytes = recv(data_socket, buffer.data(), 1024, 0);
                    file.write(buffer.data(), bytes);
                }
                while(bytes == 1024);

                fprintf(fd, "226 Closing data connection.\n");
                file.close();
            }
            else
            {
                fprintf(fd, "552 Requested file action aborted.\n");
            }
            close(data_socket);
      }
      else if (COMMAND("RETR")) {
	   fscanf(fd, "%s", arg);

            std::array<char, 1024> buffer;
            std::ifstream file(current_dir.string() + '/' + arg);

            if(file.good())
            {
                fprintf(fd, "150 File status okay; about to open data connection.\n");
                do
                {
                    file.read(buffer.data(), 1024);
                    send(data_socket, buffer.data(), file.gcount(), 0);
                }
                while(file.gcount() == 1024);

                fprintf(fd, "226 Closing data connection.\n");
                file.close();
            }
            else
            {
                fprintf(fd, "550 Requested action not taken.\n");
            }
            close(data_socket);
      }
      else if (COMMAND("LIST")) {
	   fprintf(fd, "125 List started OK.\n");
            for(auto& entry: fs::directory_iterator(current_dir))
            {
                const auto filename = entry.path().filename().string() + '\n';
                send(data_socket, filename.c_str(), filename.size(), 0);
            }
            fprintf(fd, "250 List completed successfully.\n");
            close(data_socket);
      }
      else if (COMMAND("SYST")) {
           fprintf(fd, "215 UNIX Type: L8.\n");   
      }

      else if (COMMAND("TYPE")) {
	  fscanf(fd, "%s", arg);
	  fprintf(fd, "200 OK\n");   
      }
     
      else if (COMMAND("QUIT")) {
        fprintf(fd, "221 Service closing control connection. Logged out if appropriate.\n");
        close(data_socket);	
        parar=true;
        break;
      }
  
      else  {
	    fprintf(fd, "502 Command not implemented.\n"); fflush(fd);
	    printf("Comando : %s %s\n", command, arg);
	    printf("Error interno del servidor\n");
	
      }
      
    }
    
    fclose(fd);

    
    return;
  
};
