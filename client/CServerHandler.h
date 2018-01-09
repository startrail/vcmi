/*
 * CServerHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

struct SharedMemory;
class CConnection;

struct ServerCapabilities;

#include "../lib/CondSh.h"
#include "../lib/CStopWatch.h"

/// structure to handle running server and connecting to it
class CServerHandler
{
private:
	void callServer(); //calls server via system(), should be called as thread
public:
	CStopWatch th;
	boost::thread *serverThread; //thread that called system to run server
	SharedMemory * shm;
	std::string uuid;
	bool verbose; //whether to print log msgs

	CConnection * c;
	ServerCapabilities * cap;

	static CondSh<bool> serverAlive;  //used to prevent game start from executing if server is already running

	std::vector<std::string> myNames;
	std::vector<ui8> myPlayers;

	//functions setting up local server
	void startServer(); //creates a thread with callServer
	void waitForServer(); //waits till server is ready
	void startServerAndConnect(); //connects to server
	bool isServerLocal();

	//////////////////////////////////////////////////////////////////////////
	void justConnectToServer(const std::string &host = "", const ui16 port = 0); //connects to given host without taking any other actions (like setting up server)
	static ui16 getDefaultPort();
	static std::string getDefaultPortStr();

	CServerHandler();
	virtual ~CServerHandler();

	void welcomeServer();
	void stopConnection();
};

extern CServerHandler * CSH;
