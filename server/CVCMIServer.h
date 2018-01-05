/*
 * CVCMIServer.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <boost/program_options.hpp>

class CMapInfo;

class CConnection;
struct CPackForSelectionScreen;
class CGameHandler;
struct SharedMemory;

namespace boost
{
	namespace asio
	{
		namespace ip
		{
			class tcp;
		}
		class io_service;

		template <typename Protocol> class stream_socket_service;
		template <typename Protocol,typename StreamSocketService>
		class basic_stream_socket;

		template <typename Protocol> class socket_acceptor_service;
		template <typename Protocol,typename SocketAcceptorService>
		class basic_socket_acceptor;
	}
};

typedef boost::asio::basic_socket_acceptor<boost::asio::ip::tcp, boost::asio::socket_acceptor_service<boost::asio::ip::tcp>> TAcceptor;
typedef boost::asio::basic_stream_socket<boost::asio::ip::tcp, boost::asio::stream_socket_service<boost::asio::ip::tcp>> TSocket;

struct StartInfo;
struct ServerCapabilities;
class CVCMIServer
{
	enum
	{
		INVALID, RUNNING, ENDING_WITHOUT_START, ENDING_AND_STARTING_GAME
	} state;
	ui16 port;
	boost::asio::io_service * io;
	TAcceptor * acceptor;
	SharedMemory * shm;

	boost::program_options::variables_map cmdLineOptions;

	CConnection * host;
	int listeningThreads;
	std::set<CConnection *> connections;
	std::list<CPackForSelectionScreen *> toAnnounce;
	boost::recursive_mutex mx;

	TSocket * upcomingConnection;

	const CMapInfo * curmap;
	StartInfo * curStartInfo;
	ServerCapabilities * capabilities;

	void startAsyncAccept();
	void connectionAccepted(const boost::system::error_code & ec);
	void startListeningThread(CConnection * pc);
	void handleConnection(CConnection * cpc);

	void processPack(CPackForSelectionScreen * pack);
	void sendPack(CConnection * pc, const CPackForSelectionScreen & pack);
	void announcePack(const CPackForSelectionScreen & pack);
	void announceTxt(const std::string & txt, const std::string & playerName = "system");

public:
	static std::atomic<bool> shuttingDown;

	CVCMIServer(boost::program_options::variables_map & opts);
	~CVCMIServer();

	void start();
	void startGame();

#ifdef VCMI_ANDROID
	static void create();
#endif
};

//extern std::atomic<bool> CVCMIServer::serverShuttingDown;
