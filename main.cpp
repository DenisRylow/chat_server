#include <winsock2.h>

#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>


#define MAX_MESSAGE_SIZE 80
#define MAX_CONNECTIONS 5
#define RESERVE_VECTOR_SIZE 5
#define LOG_FILE "log.txt"

std::mutex fileChatHistoryAcess, clientsInfoAcess;

struct clientInfo
{
	SOCKET clientSocket;
	sockaddr_in clientAddr;
	char nickName[MAX_MESSAGE_SIZE];
	unsigned char nickNameSize;
};

bool KillCommandNotIssued(char *message, unsigned char messageLength)
{
	if (7 > messageLength) // "!kill" is five letters long.
	{
		return true;
	};

	const char KILL_COMMAND[5] = {'!','k','i','l','l'};
	bool equal = true;
	int k = messageLength - 2 - 1;

	while ((equal) && (k >= (messageLength - 2 - 5) ))
	{
		equal = (message[k] == KILL_COMMAND[k]);

		--k;
	};

	return !equal;
};



void ChatMessageRetranslator(std::vector<clientInfo> *clients_, std::fstream *file_, bool *killServer_)
{
	SOCKET clientSocket_;
	char nickName_[MAX_MESSAGE_SIZE];
	unsigned char nickNameSize_;

	clientsInfoAcess.lock();
	clientSocket_ = clients_->back().clientSocket;
	nickNameSize_ = clients_->back().nickNameSize;
	clientsInfoAcess.unlock();

	char recievedMessage[MAX_MESSAGE_SIZE];
	char messageBuffer[MAX_MESSAGE_SIZE];
	char sentMessage[2*MAX_MESSAGE_SIZE];

	bool connectionAlive = true;
	bool endOfTransmission;
	bool loop;

	int transmissionResult, transmissionLength;
	int i, j, k;

	//	for(i = 0; i < nickNameSize_; ++i )
	//	{
	//		nickName_[i] = clients_->back().nickName[i];
	//	};

	send(clientSocket_, "Your connection to the telnet-chat has been established!\r\n", 58, 0);


	// Sending chat history.
	fileChatHistoryAcess.lock();

	file_->clear();
	file_->seekg(0, file_->end);
	std::fstream::pos_type zero = 0;

	if ((file_->tellg() != zero) && (loop = file_->is_open()))
	{
		file_->seekg(0, file_->beg);

		i = 0;
		while (!file_->eof())
		{
			++i;

			if (i < MAX_MESSAGE_SIZE)
			{
				file_->get(messageBuffer[i - 1]); // Get() sets end of file flag if reading a letter is impossible.
			}
			else
			{
				send(clientSocket_, messageBuffer, i, 0);
				i = 0;
			};

		};

		if (i > 1)
		{
			send(clientSocket_, messageBuffer, i - 1, 0); // Minus unit is needed for end of file flag correction.
		};

	};
	
	fileChatHistoryAcess.unlock();

	while (connectionAlive)
			{
		 	 	endOfTransmission = false;
		 	 	transmissionLength = 0;

		 	 	// Buffering the message.
		 	 	while (!endOfTransmission)
		 	 	{
		 	 		 transmissionResult = recv(clientSocket_, messageBuffer, sizeof(messageBuffer), 0 );

		 	 		 if (transmissionResult < 1)
		 	 		 {
		 	 			endOfTransmission = true;
		 	 			transmissionLength = 0;
		 	 		 }
		 	 		 else
		 	 		 {
		 	 			if (MAX_MESSAGE_SIZE < (transmissionLength + transmissionResult))
		 	 			{
		 	 				// Buffer overflow.
			 	 			for( k = 0; k < (MAX_MESSAGE_SIZE - transmissionLength - 2); ++k)
				 	 				 recievedMessage[k + transmissionLength] = messageBuffer[k];

		 	 				transmissionLength = MAX_MESSAGE_SIZE;
		 	 				endOfTransmission = true;

	 	 					recievedMessage[MAX_MESSAGE_SIZE - 1] = '\n';
	 	 					recievedMessage[MAX_MESSAGE_SIZE - 2] = '\r';

		 	 			}
		 	 			else
		 	 			{
			 	 			for( i = 0; i < transmissionResult; ++i)
			 	 				 recievedMessage[i + transmissionLength] = messageBuffer[i];

			 	 			transmissionLength += transmissionResult;

			 	 			if (messageBuffer[transmissionResult - 1] == '\n')
			 	 			{
			 	 				endOfTransmission = true;
			 	 			};
		 	 			};

		 	 		 };
		 	 	};


		 	 	if (transmissionLength < 1)
				{
					connectionAlive = false;
					std::cout<<"Transmission ended, socket - "<<clientSocket_<<", result - "<<transmissionResult;

					// Searching the client, deleting the information about client, shutting down the connection.
					loop = true;
					i = 0;

					clientsInfoAcess.lock();

					while (loop)
					{
						if (i >= clients_->size())
						{
							loop = false;
						}
						else
						{
							if (clients_->at(i).clientSocket == clientSocket_)
							{
								clients_->erase(clients_->begin() + i);
								shutdown(clientSocket_, SD_BOTH);
								loop = false;
								connectionAlive = false;
								closesocket(clientSocket_);
							}
						};

						++i;
					};

					clientsInfoAcess.unlock();
				}
				else
				{
					// We got a message! Time to process it.
					if (nickNameSize_ == 0)
					{
						// Nickname has been sent.
						for(i = 0; i < transmissionLength; ++i)
							nickName_[i] = recievedMessage[i];

						nickNameSize_ = transmissionLength - 2; // Subtracting 2 to remove \n\r symbols from the nickname.

						// Searching the client and inserting the nickname.
						loop = true;
						i = 0;

						clientsInfoAcess.lock();

						while (loop)
						{
							if (i >= clients_->size())
							{
								loop = false;
							}
							else
							{
								if (clients_->at(i).clientSocket == clientSocket_)
								{
									for(j = 0; j < nickNameSize_; ++j )
									{
										clients_->at(i).nickName[j] = nickName_[j];
									};
									//strcpy(clients_->at(i).nickName, recievedMessage);

									clients_->at(i).nickNameSize = nickNameSize_;
									loop = false;
								}
							};

							++i;
						};

						clientsInfoAcess.unlock();

					}
					else
					{
						if (KillCommandNotIssued( (char *) &recievedMessage, transmissionLength))
						{
							clientsInfoAcess.lock();

							// Forming the message to be sent.
							for (j = 0; j < nickNameSize_; ++j)
								sentMessage[j] = nickName_[j];

							sentMessage[j] = '>';
							++j;

							for (k = 0; k < transmissionLength; ++k)
								sentMessage[j + k] = recievedMessage[k];

							// Sending the message to other clients.
							loop = true;
							i = 0;

							while (loop)
							{
								if (i >= clients_->size())
								{
									loop = false;
								}
								else
								{
									if (clients_->at(i).clientSocket != clientSocket_) // The client does not send messages to himself.
									{
										send(clients_->at(i).clientSocket, (char *) &sentMessage, k + j, 0);

									};
								};

								++i;
							};

							clientsInfoAcess.unlock();

							// Writing into chat history.
							fileChatHistoryAcess.lock();

							if (file_->is_open())
							{
								file_->clear();
								file_->write(sentMessage, k + j);
							};

							fileChatHistoryAcess.unlock();
						}
						else
						{
							// Server shutdown command has been issued.
							*killServer_ = true;

						};

					};
				};
			};
};

int main()
{

	bool killServer = false;

	std::fstream file;
	file.open(LOG_FILE, std::fstream::in | std::fstream::out | std::fstream::app); 

	WSADATA wsa;

	int errorWSA = WSAStartup(MAKEWORD(2,2), &wsa);
    if (errorWSA != 0)
    {
        std::cout<<"WSAS not started - "<<errorWSA;
        return 1;
    }

    SOCKET s, socketTemp;

    sockaddr_in server, clientTempAddr;

    clientInfo clientTemp;
    std::vector<clientInfo> clients;
    clients.reserve(RESERVE_VECTOR_SIZE);

    std::vector<std::thread> threadPool;
    threadPool.reserve(RESERVE_VECTOR_SIZE);

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons( 7100 );

    s = socket(AF_INET , SOCK_STREAM , IPPROTO_TCP); 

    int errorBind = bind(s, (struct sockaddr *) &server, sizeof(server));
    std::cout<< errorBind;

    int errorListen = listen(s, MAX_CONNECTIONS);
    std::cout<< errorListen;

    int c = sizeof(sockaddr_in);

    while ( ((socketTemp = accept(s, (struct sockaddr *) &clientTempAddr, &c)) != INVALID_SOCKET) && (!killServer) )
    {
    	clientTemp.clientAddr = clientTempAddr;
    	clientTemp.clientSocket = socketTemp;
    	clientTemp.nickNameSize = 0;

    	clients.push_back(clientTemp);

        threadPool.push_back( std::thread (ChatMessageRetranslator, &clients, &file, &killServer) );
        threadPool.back().detach();

     };


    // Cleaning up.
    file.close();
    std::terminate();
    for( auto t : clients)
    {
    	shutdown(t.clientSocket, SD_SEND);
    	closesocket(t.clientSocket);
    };

    closesocket(s);
    WSACleanup();

	return 0;
}

