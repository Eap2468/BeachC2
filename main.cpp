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

#define DEBUG 0

bool going = false;
bool serverListen = true;

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

bool ServerSetup(int *serverfd, int port)
{
    sockaddr_in serverAddr;
    *serverfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr("0.0.0.0");
    int enable = 1;
    if(setsockopt(*serverfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) != 0
    || setsockopt(*serverfd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) != 0)
    {
        errorMsg("Setsockopt error " + std::to_string(errno));
        return false;
    }
    if(bind(*serverfd, (sockaddr*)&serverAddr, sizeof(serverAddr)) != 0)
    {
        errorMsg("Bind error " + std::to_string(errno));
        return false;
    }
    if(listen(*serverfd, 5) != 0)
    {
        errorMsg("Listen error " + std::to_string(errno));
        close(*serverfd);
        return false;
    }

    infoMsg("Server started on 0.0.0.0:" + std::to_string(port));

    return true;
}

void StartServer(std::map<int, std::string> *sessions, int *serverfd)
{
    int clientfd = -1;
    sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);
    struct pollfd fds[1];
    std::string ip;

    fds[0].fd = *serverfd;
    fds[0].events = POLLIN;

    while(serverListen)
    {
        poll(fds, 1, 10);
        if(fds[0].revents & POLLIN)
        {
            clientfd = accept(*serverfd, (sockaddr*)&clientAddr, &clientAddrSize);
            if(clientfd == -1){ break; }
            
            ip = inet_ntoa(clientAddr.sin_addr);
            infoMsg("Connection from " + ip);

            sessions->insert(std::pair<int, std::string>(clientfd, ip));
        }
    }
    close(*serverfd);
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
        return;
    }
    else if (code == 20)
    {
        going = false;
        std::cout << std::endl;
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

    int pollCode, code;
    while(going)
    {
        pollCode = poll(fds, 4, 10);
        if(pollCode == -1 && going)
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
    std::thread serverThread;
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

    if(!ServerSetup(&serverfd, port))
    {
        errorMsg("Unable to bind to port " + std::to_string(port));
    }
    else
    {
        serverThread = std::thread(StartServer, &sessions, &serverfd);
    }

    signal(SIGINT, signal_shell_handler);
    signal(SIGTSTP, signal_shell_handler);

    int choice = 0, count = 0, code = 0;
    std::vector<int> choiceVect = {};
    bool found = false;
    
    while(true)
    {
        std::vector<int>().swap(choiceVect);

        std::vector<std::string>().swap(inputArgs);
        prompt();
        std::getline(std::cin, inputStr);

        if(inputStr.length() == 0)
        {
            continue;
        }

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
        if(inputStr == "clear")
        {
            printf("\e[1:1H\e[2J");
            continue;
        }
        if(inputStr == "sessions")
        {
            count = 0;
            if(sessions.size() == 0)
            {
                infoMsg("No sessions available, go catch some shells!");
            }
            for(auto &i : sessions)
            {
                std::cout << count << ") " << i.second << std::endl;
                count++;
            }
        }
        if(inputArgs[0] == "listen")
        {
            if (inputArgs.size() < 2)
            {
                errorMsg("Please enter a valid port number");
                continue;
            }

            infoMsg("Attempting to restart server on port " + inputArgs[1]);

            try
            {
                port = std::stoi(inputArgs[1]);
                if(port < 1 || port > 65535)
                {
                    throw std::invalid_argument("Invalid port number");
                }
            }catch(...)
            {
                errorMsg("Please enter a valid port number");
                continue;
            }

            serverListen = false;
            try
            {
                serverThread.join();
            }catch(...){}

            if(!ServerSetup(&serverfd, port))
            {
                errorMsg("Unable to bind to port " + std::to_string(port));
                continue;
            }
            serverListen = true;

            serverThread = std::thread(StartServer, &sessions, &serverfd);
        }
        if(inputArgs[0] == "use")
        {
            try
            {
                if(inputArgs.size() < 2)
                {
                    throw std::invalid_argument("No session number given");
                }
                choice = std::stoi(inputArgs[1]);
                if(choice < 0 || choice > sessions.size() - 1)
                {
                    throw std::invalid_argument("Invalid session number");
                }
            } catch(...)
            {
                errorMsg("Please enter a valid session number");
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
        if(inputArgs[0] == "kill")
        {
            try
            {
                if(inputArgs.size() < 2)
                {
                    throw std::invalid_argument("No session number given");
                }

                if(inputArgs[1] == "*")
                {
                    std::cout << "Kill all connections? (y/n) ";
                    getline(std::cin, inputStr);

                    if(DEBUG)
                    {
                        debugMsg("inputStr: " + inputStr);
                    }

                    if(inputStr == "y" || inputStr == "Y")
                    {
                        infoMsg("Killing all connections");
                        for(auto &i : sessions)
                        {
                            close(i.first);
                        }
                        sessions.clear();

                        infoMsg("All connections killed");

                        continue;
                    }
                }



                for(int i = 1; i < inputArgs.size(); i++)
                {
                    found = false;
                    choice = std::stoi(inputArgs[i]);
                    if (choice < 0 || choice > sessions.size() - 1)
                    {
                        throw std::invalid_argument("Invalid session number");
                    }

                    for(int i : choiceVect)
                    {
                        if (i == choice)
                        {
                            found = true;
                            break;
                        }
                    }

                    if(!found)
                    {
                        choiceVect.push_back(choice);
                    }
                }

                if (DEBUG)
                {
                    debugMsg("choiceVect");
                    for(int i : choiceVect)
                    {
                        debugMsg(std::to_string(i));
                    }
                }

                if (choiceVect.size() > 1)
                {
                    int temp = -1;

                    for(int i = 0; i < choiceVect.size() - 1; i++)
                    {
                        for(int a = 1; a < choiceVect.size(); a++)
                        {
                            if(choiceVect[i] < choiceVect[a])
                            {
                                temp = choiceVect[i];
                                choiceVect[i] = choiceVect[a];
                                choiceVect[a] = temp;
                            }
                        }
                    }

                    if(DEBUG)
                    {
                        debugMsg("choiceVect");
                        for(int i : choiceVect)
                        {
                            debugMsg(std::to_string(i));
                        }
                    }

                    infoMsg("Killing connections");
                    for(int i : choiceVect)
                    {
                        count = 0;
                        for(auto &a : sessions)
                        {
                            if(count == i)
                            {
                                close(a.first);
                                sessions.erase(a.first);
                                break;
                            }
                            count++;
                        }
                    }
                    continue;
                }

            } catch(...)
            {
                errorMsg("Please enter a valid session number");
                continue;
            }

            count = 0;
            for(auto &i : sessions)
            {
                if (count == choice)
                {
                    infoMsg("Killing connection");
                    close(i.first);
                    sessions.erase(i.first);
                    infoMsg("Connection killed");
                    break;
                }
                count++;
            }
        }

        if (inputArgs[0] == "rename")
        {
            try
            {
                if (inputArgs.size() < 2)
                {
                    throw std::invalid_argument("No session number given");
                }

                choice = std::stoi(inputArgs[1]);
                if (choice < 0 || choice > sessions.size() - 1)
                {
                    throw std::invalid_argument("Invalid session number");
                }
            } catch(...)
            {
                errorMsg("Please enter a valid session number");
                continue;
            }
            
            std::string nameStr = "";
            if (inputArgs.size() > 2)
            {
                for(int i = 2; i < inputArgs.size(); i++)
                {
                    if (DEBUG)
                    {
                        debugMsg("Arg " + inputArgs.at(i));
                    }

                    if (i == inputArgs.size() - 1)
                    {
                        nameStr += inputArgs.at(i);
                        continue;
                    }

                    nameStr += (inputArgs.at(i) + " ");
                }
                
                if (DEBUG)
                {
                    debugMsg("nameStr " + nameStr);
                }
            }
            else
            {
                std::cout << "New session name: ";
                std::getline(std::cin, nameStr);
            }

            count = 0;
            for(auto &i : sessions)
            {
                if (count == choice)
                {
                    sessions[i.first] = nameStr;
                    successMsg("Session name changed!");
                }
                count++;
            }
        }
    }

    for(auto &i : sessions)
    {
        close(i.first);
    }
    std::cout << "Thank you for using BeachC2!" << std::endl;
    return 0;
}
