#include <iostream>
#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <cstring>
#include <sstream>
#include <map>
#include <vector>
#include <thread>
#include <arpa/inet.h>
#include <poll.h>
#include <signal.h>

#define BUFFERSIZE 1024

#define STDIN 0
#define NETOUT 1
#define NETIN 2
#define STDOUT 3

#define DEBUG 1

bool going = false;

struct IO
{
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
};

void successMsg(std::string msg)
{
    std::cout << "[+] " << msg << std::endl;
}

void errorMsg(std::string msg)
{
    std::cout << "[-] " << msg << std::endl;
}

void infoMsg(std::string msg)
{
    std::cout << "[*] " << msg << std::endl;
}

void debugMsg(std::string msg)
{
    std::cout << "DEBUG: " << msg << std::endl;
}

void prompt()
{
    std::cout << "BEACHC2> ";
}

void split(std::vector<std::string> *args, std::string str)
{
    std::stringstream ss(str);

    while(getline(ss, str, ' '))
    {
        args->push_back(str);
    }
}

void StartServer(std::map<int, std::string> *sessions, int *serverfd)
{
    int clientfd;
    sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);
    std::string ip;

    while(true)
    {
        clientfd = accept(*serverfd, (sockaddr*)&clientAddr, &clientAddrSize);
        
        ip = inet_ntoa(clientAddr.sin_addr);
        infoMsg("Connection from " + ip);

        sessions->insert(std::pair<int, std::string>(clientfd, ip));
    }
}

void help()
{
    
}

void signal_shell_handler(int code)
{
    if (DEBUG)
    {
        debugMsg("signal code " + std::to_string(code));
    }
    if (code == 2)
    {
        going = false;
    }
    else
    {
        exit(code);
    }
}

ssize_t fillBuffer(int fd, unsigned char* buffer, size_t *bufferPos)
{
    ssize_t bufferSize = BUFFERSIZE - *bufferPos;
    ssize_t bytes_read = read(fd, buffer + *bufferPos, bufferSize);

    if (bytes_read <= 0)
    {
        return -1;
    }

    *bufferPos += bytes_read;
    return bytes_read;
}

ssize_t drainBuffer(int fd, unsigned char* buffer, size_t *bufferPos)
{
    ssize_t bytes_sent = write(fd, buffer, *bufferPos);

    if(bytes_sent <= 0)
    {
        return -1;
    }

    *bufferPos -= bytes_sent;
    if(*bufferPos > 0)
    {
        memmove(buffer, buffer + bytes_sent, *bufferPos);
    }

    return bytes_sent;
}

int shell(int clientfd, IO io)
{
    going = true;

    unsigned char stdinBuff[BUFFERSIZE];
    size_t stdinPos = 0;

    unsigned char netinBuff[BUFFERSIZE];
    size_t netinPos = 0;

    struct pollfd fds[4];

    fds[STDIN].fd = io.stdin_fd;
    fds[NETOUT].fd = clientfd;
    fds[NETIN].fd = clientfd;
    fds[STDOUT].fd = io.stdout_fd;

    fds[STDIN].events = POLLIN;
    fds[NETOUT].events = 0;
    fds[NETIN].events = POLLIN;
    fds[STDOUT].events = 0;

    signal(SIGINT, signal_shell_handler);

    int pollCode, code;
    while(going)
    {
        pollCode == poll(fds, 4, 10);
        if(pollCode == -1)
        {
            errorMsg("Poll error");
            return 1;
        }
        if(fds[STDIN].fd == -1 || fds[NETOUT].fd == -1 || fds[NETIN].fd == -1 || fds[STDOUT].fd == -1)
        {
            errorMsg("Connection error");
            return 2;
        }

        if(fds[STDIN].revents & POLLIN && stdinPos < BUFFERSIZE)
        {
            int code = fillBuffer(fds[STDIN].fd, stdinBuff, &stdinPos);
            if (DEBUG)
            {
                debugMsg("STDIN code " + std::to_string(code));
            }
            if (code == -1)
            {
                fds[STDIN].fd = -1;
                continue;
            }

            if(stdinPos == BUFFERSIZE)
            {
                fds[STDIN].events = 0;
            }
            if(stdinPos > 0)
            {
                fds[NETOUT].events = POLLOUT;
            }
        }
        if(fds[NETOUT].revents & POLLOUT && stdinPos > 0)
        {
            int code = drainBuffer(fds[NETOUT].fd, stdinBuff, &stdinPos);
            if (DEBUG)
            {
                debugMsg("NETOUT code " + std::to_string(code));
            }
            if(code == -1)
            {
                fds[NETOUT].fd == -1;
                continue;
            }

            if(stdinPos == 0)
            {
                fds[NETOUT].revents = 0;
            }
            if(stdinPos < BUFFERSIZE)
            {
                fds[STDIN].events = POLLIN;
            }
        }
        if(fds[NETIN].revents & POLLIN && netinPos < BUFFERSIZE)
        {
            int code = fillBuffer(fds[NETIN].fd, netinBuff, &netinPos);
            if (DEBUG)
            {
                debugMsg("NETIN code " + std::to_string(code));
            }
            if (code == -1)
            {
                fds[NETIN].fd = -1;
                continue;
            }

            if(netinPos == BUFFERSIZE)
            {
                fds[NETIN].events = 0;
            }
            if(netinPos > 0)
            {
                fds[STDOUT].events = POLLOUT;
            }
        }
        if(fds[STDOUT].revents & POLLOUT && netinPos > 0)
        {
            int code = drainBuffer(fds[STDOUT].fd, netinBuff, &netinPos);
            if (DEBUG)
            {
                debugMsg("STDOUT code " + std::to_string(code));
            }
            if (code == -1)
            {
                fds[STDOUT].fd = -1;
                continue;
            }

            if(netinPos == 0)
            {
                fds[STDOUT].events = 0;
            }
            if(netinPos < BUFFERSIZE)
            {
                fds[NETIN].events = POLLIN;
            }
        }
    }
    return 0;
}

int main(int argc, char* argv[])
{
    int serverfd = -1, port = 0;
    std::string inputStr = "";
    std::vector<std::string> inputArgs;
    std::map<int, std::string> sessions;
    struct IO io;
    io.stdin_fd = STDIN_FILENO;
    io.stdout_fd = STDOUT_FILENO;
    io.stderr_fd = STDERR_FILENO;

    if (argc < 2)
    {
        help();
        std::cout << "Usage ./BeachC2 <PORT>" << std::endl;
        return 0;
    }
    try
    {
        port = std::stoi(argv[1]);
        if(port < 1 || port > 65535)
        {
            throw std::invalid_argument("Invalid port number");
        }
    } catch(...)
    {
        help();
        errorMsg("Please enter a valid port number (1-65535)");
        return 0;
    }

    serverfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr("0.0.0.0");
    int enable = 1;
    if(setsockopt(serverfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0)
    {
        errorMsg("Setsockopt error " + std::to_string(errno));
        return 0;
    }
    if(bind(serverfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) != 0)
    {
        errorMsg("Bind error " + std::to_string(errno));
        return 0;
    }
    if(listen(serverfd, 5) != 0)
    {
        errorMsg("Listen error " + std::to_string(errno));
        close(serverfd);
        return 0;
    }
    infoMsg("Server started on 0.0.0.0:" + std::to_string(port));

    std::thread serverThread(StartServer, &sessions, &serverfd);

    int choice = 0, count = 0, code = 0;
    while(true)
    {
        inputArgs.clear();
        prompt();
        std::getline(std::cin, inputStr);
        split(&inputArgs, inputStr);

        if(DEBUG)
        {
            for(std::string i : inputArgs)
            {
                debugMsg(i);
            }
        }

        if(inputStr == "exit")
        {
            serverThread.detach();
            break;
        }
        if(inputStr == "sessions")
        {
            count = 0;
            for(auto &i : sessions)
            {
                std::cout << count << ") " << i.second << std::endl;
                count++;
            }
        }
        if(inputArgs[0] == "use")
        {
            try
            {
                choice = std::stoi(inputArgs[1]);
                if(choice < 0 || choice > sessions.size() - 1)
                {
                    throw std::invalid_argument("Invalid session number");
                }
            } catch(...)
            {
                errorMsg("Please enter a valid number");
                continue;
            }

            count = 0;
            for(auto &i : sessions)
            {
                if(count == choice)
                {
                    code = shell(i.first, io);
                    if(code == 2)
                    {
                        sessions.erase(i.first);
                        break;
                    }
                }
                count++;
            }
        }

    }

    for(auto &i : sessions)
    {
        close(i.first);
    }
    close(serverfd);
    std::cout << "Thank you for using BeachC2!" << std::endl;
    return 0;
}
