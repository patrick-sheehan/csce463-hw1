/*
* Patrick Sheehan
* CSCE463 HW1
* 27 January 2015
* 
* References: 463 Sample Code by Dmitri Loguinov
*/

#include "WebSocket.h"

WebSocket::WebSocket()
{
	buf = new char[INITIAL_BUF_SIZE];
}
void WebSocket::Setup(char* hostname, int port, LPVOID pParam)
{
	WSADATA wsaData;

	bool uniqueHost = false;
	bool successfulDNS = false;
	bool uniqueIP = false;

	Parameters *p = ((Parameters*)pParam);

	// Initialize WinSock; once per program run
	WORD wVersionRequested = MAKEWORD(2, 2);
	if (WSAStartup(wVersionRequested, &wsaData) != 0) {
		// WSA failure
		WSACleanup();
		return;
	}

	// open a TCP socket
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock == INVALID_SOCKET) {
		WSACleanup();
		return;
	}

	struct in_addr IP;
	struct sockaddr_in server;

	WaitForSingleObject(p->mutex, INFINITE);	// lock mutex

	size_t prevHostSetSize = p->visitedHostSet.size();
	size_t prevIPSetSize = p->visitedIPSet.size();

	p->visitedHostSet.insert(hostname);
	if (p->visitedHostSet.size() > prevHostSetSize) {
		// unique host
		(p->numURLsWithUniqueHost) += 1;

		// DNS lookup
		struct hostent *remote = gethostbyname(hostname);
		if (remote != NULL) {
			memcpy(&IP, remote->h_addr_list[0], sizeof(struct in_addr));
			(p->numSuccessfulDNSLookups) += 1;

			std::string ipString = inet_ntoa(IP);
			p->visitedIPSet.insert(ipString);

			if (p->visitedIPSet.size() > prevIPSetSize) {
				// unique IP
				(p->numURLsWithUniqueIP) += 1;

				// structure for connecting to server
				server.sin_addr = IP;
				server.sin_family = AF_INET;
				server.sin_port = htons(port);

				// Connect socket to server on correct port
				if (connect(sock, (struct sockaddr*) &server, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
					return;
				}
			}
		}
	}

	ReleaseMutex(p->mutex);						// unlock mutex
}

bool WebSocket::checkRobots(const char* hostname)
{
	int status;
	const char* robotRequest = buildRequest("HEAD", hostname, "/robots.txt");
	char* buffer = new char[INITIAL_BUF_SIZE];

	Send(robotRequest);

	if (ReadToBuffer(status, buffer) > -1) {
		memset(&buffer[0], 0, sizeof(buffer));

		if (status >= 400) {
			// Only 4xx status (robots file not found) allows unrestricted crawling
			return true;
		}
	}

	return false;
}

int WebSocket::downloadPageAndCountLinks(const char* hostname, const char* request, const char* baseUrl, LPVOID pParam)
{
	Parameters *p = ((Parameters*)pParam);

	char* buffer = new char[INITIAL_BUF_SIZE];
	const char* pageRequest = buildRequest("GET", hostname, request);
	int status;

	if (Send(pageRequest) > -1) {

		if (ReadToBuffer(status, buffer) > -1) {

			WaitForSingleObject(p->mutex);
			switch ((int)(status / 100))
			{
			case 2:
				(p->code2xxCount) += 1;
				break;
			case 3:
				(p->code3xxCount) += 1;
				break;
			case 4:
				(p->code4xxCount) += 1;
				break;
			case 5:
				(p->code5xxCount) += 1;
				break;
			default:
				(p->codeOtherCount) += 1;
				break;
			}
			ReleaseMutex(p->mutex);

			if (200 <= status && status < 300) {

				HTMLParserBase *parser = new HTMLParserBase;
				int nLinks;
				char *linkBuffer = parser->Parse((char*)buffer, (int)strlen(buffer), (char*)baseUrl, (int)strlen(baseUrl), &nLinks);

				if (nLinks < 0)
					nLinks = 0;

				return nLinks;
			}
		}
	}
	
	// did not successfully parse http page
	return -1;
}

int WebSocket::Send(const char* request)
{
	// send HTTP request
	if (send(sock, request, strlen(request), 0) == SOCKET_ERROR) {
		return -1;
		//printf("Send error: %d\n", WSAGetLastError());
	}

	return 0;
}

int WebSocket::ReadToBuffer(int& status, char* buffer)
{
	// Receive data from socket and write it to the buffer 

	size_t bytesRead = 0;
	int num = 0;
	char *responseBuf = new char[INITIAL_BUF_SIZE];
	char *temp;

	while ((num = recv(sock, responseBuf, INITIAL_BUF_SIZE, 0)) > 0)
	{
		bytesRead += num;

		// if need to resize buffer
		if ((bytesRead + INITIAL_BUF_SIZE) > strlen(responseBuf))
		{
			// Move old array to temp storage
			temp = new char[bytesRead];
			memcpy(temp, responseBuf, bytesRead);

			// Double size of buffer
			responseBuf = new char[2 * bytesRead];

			// Copy data over from temp
			memcpy(responseBuf, temp, bytesRead);
			
			// clear temp's memory
			memset(&temp[0], 0, sizeof(temp));
		}

		if (status < 0)
		{
			int s, h;
			sscanf(responseBuf, "HTTP/1.%d %d", &h, &s);
			status = s;
		}
	}

	if (bytesRead <= 0) {
		return -1;
	}

	strncpy(buffer, responseBuf, bytesRead);
	memset(&responseBuf[0], 0, sizeof(responseBuf));

	return 0;

	// on error 
	// return -1;
}

int WebSocket::msTime(clock_t start, clock_t end)
{
	double seconds = ((double)(end - start)) / CLOCKS_PER_SEC;
	return (int)(1000 * seconds);
}

const char* WebSocket::buildRequest(const char* type, const char* host, const char* subrequest)
{
	// Hostname is crucial
	if (host == NULL) {
		//printf("Failed to create a GET request. Expected char* for hostname, received NULL");
		return NULL;
	}

	// Assign default value if no subrequest provided
	if (subrequest == NULL || subrequest == " ") {
		subrequest = "/";
	}

	// Build formatted request string
	int size = strlen(host) + strlen(subrequest) + strlen(useragent) + 50;
	char* FULLRequest = new char[size];
	sprintf(FULLRequest,
		"%s %s HTTP/1.0\r\n"
		"Host: %s\r\n"
		"User-agent: %s\r\n"
		"Connection: close\r\n\r\n",
		type, subrequest, host, useragent);

	return FULLRequest;
}