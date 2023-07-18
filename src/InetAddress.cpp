#include "InetAddress.h"

#include <arpa/inet.h> 
#include <cstring>

InetAddress::InetAddress(uint16_t port, std::string ip)
{
    memset(&sockaddr_, 0, sizeof(sockaddr_));
    sockaddr_.sin_family = AF_INET;
    sockaddr_.sin_port = htons(port);
    sockaddr_.sin_addr.s_addr = inet_addr(ip.c_str());
}

std::string InetAddress::toIp() const
{ 
    char buf[64] = {0};
	// 网络字节序转本地字节序：大端->小端
    ::inet_ntop(AF_INET, &sockaddr_.sin_addr, buf, sizeof(buf));
    return buf;  // 字符数组char[]隐式的转换为string
}

std::string InetAddress::toIpPort() const
{
    // ip : port  
    return (this->toIp() + " : " + std::to_string(this->toPort()));
}

uint16_t InetAddress::toPort() const
{
    return ntohs(sockaddr_.sin_port);
}

/*
#include <iostream>
int main()
{
    InetAddress inetaddress(8000);
    std::cout << inetaddress.toIpPort() << std::endl;
    std::cout << inetaddress.toIp() << std::endl;
    std::cout << inetaddress.toPort() << std::endl;

    return 0;
}
*/