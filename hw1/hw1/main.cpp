/*
* Patrick Sheehan
* CSCE463 HW1
* 26 January 2015
*
* References:
*   http://www.cplusplus.com/reference/cstdio/fread/
*   http://stackoverflow.com/questions/16867372/splitting-strings-by-newline-character-in-c
*   http://stackoverflow.com/questions/7868936/read-file-line-by-line
*   http://stackoverflow.com/questions/2029103/correct-way-to-read-a-text-file-into-a-buffer-in-c
*   CSCE 463 Sample Code by Dimitri Loguinov
*/

#include "Headers.h"

UINT fileThreadFunction(LPVOID pParam)
{	
	// Producer - adds URLs to a queue shared among threads
	
	Parameters *p = ((Parameters*)pParam);

	// safely get file name from shared parameters
	WaitForSingleObject(p->mutex, INFINITE);				// lock mutex
	printf("File thread started\n");						// critical section
	HANDLE hFile = CreateFile(p->inputFile.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL, NULL);
	ReleaseMutex(p->mutex);									// unlock mutex
	
	if (hFile == INVALID_HANDLE_VALUE)
	{
		printf("CreateFile failed with %d\n", GetLastError());
		return 0;
	}

	// get file size
	LARGE_INTEGER li;
	BOOL bRet = GetFileSizeEx(hFile, &li);
	// process errors
	if (bRet == 0)
	{
		printf("GetFileSizeEx error %d\n", GetLastError());
		return 0;
	}

	// read file into a buffer
	int fileSize = (DWORD)li.QuadPart;			// assumes file size is below 2GB; otherwise, an __int64 is needed
	DWORD bytesRead;
	// allocate buffer
	char *fileBuf = new char[fileSize];
	// read into the buffer
	bRet = ReadFile(hFile, fileBuf, fileSize, &bytesRead, NULL);
	// process errors
	if (bRet == 0 || bytesRead != fileSize)
	{
		printf("ReadFile failed with %d\n", GetLastError());
		return 0;
	}

	// done with the file
	CloseHandle(hFile);

	// safely add files to shared queue
	char *url = strtok(fileBuf, "\r\n");
	while (url) {
		WaitForSingleObject(p->mutex, INFINITE);			// lock
		p->urlQueue.push(url);								// push url into queue
		ReleaseSemaphore(p->semaphoreCrawlers, 1, NULL);	// increment semaphore by 1
		ReleaseMutex(p->mutex);								// unlock

		url = strtok(0, "\r\n");
	}

	// print we're about to exit
	WaitForSingleObject(p->mutex, INFINITE);
	SetEvent(p->eventFileReadFinished);
	printf("File thread finishing execution\n");
	ReleaseMutex(p->mutex);

	return 0;
}
UINT statThreadFunction(LPVOID pParam)
{
	// Output stats every 2 seconds. 

	Parameters *p = ((Parameters*)pParam);

	clock_t currClock, lastClock = clock();

	while (WaitForSingleObject(p->eventQuit, 2000) == WAIT_TIMEOUT)
	{
		currClock = clock();
		double secondsSinceReport = ((double)(lastClock - currClock)) / CLOCKS_PER_SEC;
		
		
		WaitForSingleObject(p->mutex, INFINITE);					// lock mutex

		int totalSeconds = ((double)(lastClock - p->clock)) / CLOCKS_PER_SEC;

		printf(
			"[%3d] %6d Q %7d E %6d H %6d D %5d I %5d R %5d C %4d L\n",
			totalSeconds,
			p->urlQueue.size(),
			p->numExtractedURLs,
			p->numURLsWithUniqueHost,
			p->numSuccessfulDNSLookups,
			p->numURLsWithUniqueIP,
			p->numURLsPassedRobotCheck,
			p->numCrawledURLs,
			p->numLinks
			);

		printf("   *** crawling %.1f pps @ %.1f Mbps\n", 
			(float)(p->numCrawledURLs / secondsSinceReport),
			(float)((p->numBytesDownloaded / 1000.0) / secondsSinceReport));

		ReleaseMutex(p->mutex);										// unlock mutex

		lastClock = currClock;
	}

	WaitForSingleObject(p->mutex, INFINITE);
	// print final results
	double totalSeconds = ((double)(clock() - p->clock)) / CLOCKS_PER_SEC;

	printf("\n**************\n");
	printf("Extracted %d URLs @ %d/s\n", 
		p->numExtractedURLs, 
		(int)(p->numExtractedURLs / totalSeconds));

	printf("Looked up %d DNS names @ %d/s\n", 
		p->numSuccessfulDNSLookups, 
		(int)(p->numSuccessfulDNSLookups / totalSeconds));

	printf("Downloaded %d robots @ %d/s\n", 
		p->numURLsPassedRobotCheck, 
		(int)(p->numURLsPassedRobotCheck / totalSeconds));

	printf("Crawled %d pages @ %d/s (%.2f MB)\n", 
		p->numCrawledURLs, 
		(int)(p->numCrawledURLs / totalSeconds), 
		(float)(p->numBytesDownloaded / 1000.0));

	printf("Parsed %d links @ %d/s\n", 
		p->numLinks, 
		(int)(p->numLinks / totalSeconds));

	printf("Connected to tamu.edu domain %d times\n", p->numTAMUHostFound);

	printf("HTTP codes: 2xx = %d, 3xx = %d, 4xx = %d, 5xx = %d, other = %d\n", 
		p->code2xxCount, p->code3xxCount, p->code4xxCount, p->code5xxCount, p->codeOtherCount);

	printf("**************\n\n");

	ReleaseMutex(p->mutex);

	return 0;
}
UINT crawlerThreadFunction(LPVOID pParam)
{
	// Consumer - removes URLs from shared queue for processing
	Parameters *p = ((Parameters*)pParam);
	std::string urlString;

	HANDLE arr[] = { p->semaphoreCrawlers, p->eventFileReadFinished };

	while (WaitForMultipleObjects(2, arr, false, INFINITE) == WAIT_OBJECT_0)
	{
		WaitForSingleObject(p->mutex, INFINITE);		// lock mutex

		// extract URL from queue
		if (!p->urlQueue.empty()){
			urlString = p->urlQueue.front();
			p->urlQueue.pop();
			(p->numExtractedURLs) += 1;					// increment number of URLs extracted
		}

		ReleaseMutex(p->mutex);							// unlock mutex

		//ReleaseSemaphore(p->semaphoreCrawlers, 1, NULL);	// release crawlers' semaphore

		URLParser parser = URLParser();
		parser.parse(urlString.c_str(), pParam);
	}

	return 0;
}
void initializeParams(LPVOID pParam, int numThreads, std::string inputFile)
{
	Parameters *p = ((Parameters*)pParam);

	// assign the time this thread is starting
	p->clock = clock();

	// create a mutex for accessing critical sections (including printf); initial state = not locked
	p->mutex = CreateMutex(NULL, 0, NULL);

	// create a semaphore that counts the number of active threads; initial value = 0, max = numThreads
	p->semaphoreCrawlers = CreateSemaphore(NULL, 0, numThreads, NULL);

	// create a quit event; manual reset, initial state = not signaled
	p->eventQuit = CreateEvent(NULL, true, false, NULL);

	// set to true once file is finished being read
	p->eventFileReadFinished = CreateEvent(NULL, true, false, NULL);

	// create a shared queue of URLs to be parsed
	p->urlQueue = std::queue<std::string>();

	// assign input file that contains URLs
	p->inputFile = inputFile;
	
	// Save IP of tamu.edu host
	struct hostent *remote = gethostbyname("tamu.edu");
	if (remote != NULL) {
		struct in_addr IP;
		memcpy(&IP, remote->h_addr_list[0], sizeof(struct in_addr));
		p->tamuIPString = inet_ntoa(IP);
	}
}
int _tmain(int argc, _TCHAR* argv[])
{
	// parse command line args
	if (argc != 3) {
		printf("Invalid number of command line args provided\n");
		printf("Usage:\n\t> hw1.exe <NUM-THREADS> <URL-INPUT.TXT>");
		return 0;
	}

	int numThreads;
	std::string fileName;

	if (sscanf(argv[1], "%d", &numThreads) != 1) {
		printf("Error parsing number of threads, assuming 1\n");
		printf("Usage:\n\t> hw1.exe <NUM-THREADS> <URL-INPUT.TXT>");
		numThreads = 1;
	}

	fileName = argv[2];

	// initialize shared data structures & parameters sent to threads
	HANDLE fileThread, statThread;
	HANDLE *crawlerThreads = new HANDLE[numThreads];
	Parameters p;

	initializeParams(&p, numThreads, fileName);


	// start file-reader thread
	// sets p.eventFileReadFinished on completion
	fileThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)fileThreadFunction, &p, 0, NULL);

	// start stats thread
	statThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)statThreadFunction, &p, 0, NULL);
	
	// start N crawling threads
	for (int i = 0; i < numThreads; i++) {
		crawlerThreads[i] = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)crawlerThreadFunction, &p, 0, NULL);
	}

	// wait for file-reader thread to quit
	WaitForSingleObject(fileThread, INFINITE);
	CloseHandle(fileThread);

	// wait for N crawling threads to finish
	for (int i = 0; i < numThreads; i++) {
		WaitForSingleObject(crawlerThreads[i], INFINITE);
		CloseHandle(crawlerThreads[i]);
	}

	// signal stats thread to quit; wait for it to terminate
	SetEvent(p.eventQuit);

	WaitForSingleObject(statThread, INFINITE);
	CloseHandle(statThread);

	// cleanup
	WSACleanup();

	system("pause");
	return 0;
}
