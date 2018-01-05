/*
 * CVCMIServer.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include <boost/asio.hpp>

#include "../lib/filesystem/Filesystem.h"
#include "../lib/mapping/CCampaignHandler.h"
#include "../lib/CThreadHelper.h"
#include "../lib/serializer/Connection.h"
#include "../lib/CModHandler.h"
#include "../lib/CArtHandler.h"
#include "../lib/CGeneralTextHandler.h"
#include "../lib/CHeroHandler.h"
#include "../lib/CTownHandler.h"
#include "../lib/CBuildingHandler.h"
#include "../lib/spells/CSpellHandler.h"
#include "../lib/CCreatureHandler.h"
#include "zlib.h"
#include "CVCMIServer.h"
#include "../lib/StartInfo.h"
#include "../lib/mapping/CMap.h"
#include "../lib/rmg/CMapGenOptions.h"
#ifdef VCMI_ANDROID
#include "lib/CAndroidVMHelper.h"
#else
#include "../lib/Interprocess.h"
#endif
#include "../lib/VCMI_Lib.h"
#include "../lib/VCMIDirs.h"
#include "CGameHandler.h"
#include "../lib/mapping/CMapInfo.h"
#include "../lib/GameConstants.h"
#include "../lib/logging/CBasicLogConfigurator.h"
#include "../lib/CConfigHandler.h"
#include "../lib/ScopeGuard.h"

#include "../lib/UnlockGuard.h"

#if defined(__GNUC__) && !defined(__MINGW32__) && !defined(VCMI_ANDROID)
#include <execinfo.h>
#endif

std::string NAME_AFFIX = "server";
std::string NAME = GameConstants::VCMI_VERSION + std::string(" (") + NAME_AFFIX + ')';

CVCMIServer::CVCMIServer(boost::program_options::variables_map & opts)
	: port(3030), io(new boost::asio::io_service()), shm(nullptr), listeningThreads(0), upcomingConnection(nullptr), curmap(nullptr), curStartInfo(nullptr), state(RUNNING), cmdLineOptions(opts)
{
	logNetwork->trace("CVCMIServer created!");
	if(cmdLineOptions.count("port"))
		port = cmdLineOptions["port"].as<ui16>();
	logNetwork->info("Port %d will be used", port);
	try
	{
		acceptor = new TAcceptor(*io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port));
	}
	catch(...)
	{
		logNetwork->info("Port %d is busy, trying to use random port instead", port);
		if(cmdLineOptions.count("run-by-client") && !cmdLineOptions.count("enable-shm"))
		{
			logNetwork->error("Cant pass port number to client without shared memory!", port);
			exit(0);
		}
		acceptor = new TAcceptor(*io, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
		port = acceptor->local_endpoint().port();
	}
	logNetwork->info("Listening for connections at port %d", port);
}

CVCMIServer::~CVCMIServer()
{
	delete acceptor;
	delete upcomingConnection;

	for(CPackForSelectionScreen * pack : toAnnounce)
		delete pack;

	toAnnounce.clear();

	//TODO pregameconnections
}

void CVCMIServer::start()
{
#ifndef VCMI_ANDROID
	if(cmdLineOptions.count("enable-shm"))
	{
		std::string sharedMemoryName = "vcmi_memory";
		if(cmdLineOptions.count("enable-shm-uuid") && cmdLineOptions.count("uuid"))
		{
			sharedMemoryName += "_" + cmdLineOptions["uuid"].as<std::string>();
		}
		shm = new SharedMemory(sharedMemoryName);
	}
#endif

	startAsyncAccept();
	if(shm)
	{
		shm->sr->setToReadyAndNotify(port);
	}

	while(state == RUNNING)
	{
		{
			boost::unique_lock<boost::recursive_mutex> myLock(mx);
			while(!toAnnounce.empty())
			{
				processPack(toAnnounce.front());
				toAnnounce.pop_front();
			}

//                      //we end sending thread if we ordered all our connections to stop
//                      ending = true;
//                      for(CPregameConnection *pc : connections)
//                              if(!pc->sendStop)
//                                      ending = false;

			if(state != RUNNING)
			{
				logNetwork->info("Stopping listening for connections...");
				acceptor->close();
			}

			if(acceptor)
			{
				acceptor->get_io_service().reset();
				acceptor->get_io_service().poll();
			}
		} //frees lock

		boost::this_thread::sleep(boost::posix_time::milliseconds(50));
	}

	logNetwork->info("Thread handling connections ended");

	if(state == ENDING_WITHOUT_START)
	{
		return;
	}

	if(state == ENDING_AND_STARTING_GAME)
	{
		logNetwork->info("Waiting for listening thread to finish...");
		while(listeningThreads)
			boost::this_thread::sleep(boost::posix_time::milliseconds(50));

		startGame();
	}
}

void CVCMIServer::startGame()
{
	logNetwork->info("Preparing new game");
	CGameHandler gh;

	switch(curStartInfo->mode)
	{
	case StartInfo::NEW_GAME:
		gh.init(curStartInfo);
		break;

	case StartInfo::LOAD_GAME:
		CLoadFile lf(*CResourceHandler::get("local")->getResourceName(ResourceID(curStartInfo->mapname, EResType::SERVER_SAVEGAME)), MINIMAL_SERIALIZATION_VERSION);
		gh.loadCommonState(lf);
		lf >> gh;
		break;
	}
	gh.conns = connections;
	for(CConnection * c : gh.conns) // MPTODO: should we need to do that if we sent gamestate?
		c->addStdVecItems(gh.gs);

//	gh.sendCommonState(*conn);
	gh.run(curStartInfo->mode == StartInfo::LOAD_GAME);
}

void CVCMIServer::startAsyncAccept()
{
	assert(!upcomingConnection);
	assert(acceptor);

	upcomingConnection = new TSocket(acceptor->get_io_service());
	acceptor->async_accept(*upcomingConnection, std::bind(&CVCMIServer::connectionAccepted, this, _1));
}

void CVCMIServer::connectionAccepted(const boost::system::error_code & ec)
{
	if(ec)
	{
		if(state != ENDING_WITHOUT_START)
			logNetwork->info("Something wrong during accepting: %s", ec.message());
		return;
	}

	try
	{
		logNetwork->info("We got a new connection! :)");
		CConnection * pc = new CConnection(upcomingConnection, NAME);
		upcomingConnection = nullptr;
		connections.insert(pc);
		if(pc->connectionID == 1)
			host = pc;
		startListeningThread(pc);
	}
	catch(std::exception & e)
	{
		upcomingConnection = nullptr;
		logNetwork->info("I guess it was just my imagination!");
	}

	startAsyncAccept();
}

void CVCMIServer::startListeningThread(CConnection * pc)
{
	listeningThreads++;
	pc->enterPregameConnectionMode();
	pc->handler = new boost::thread(&CVCMIServer::handleConnection, this, pc);
}

void CVCMIServer::handleConnection(CConnection * cpc)
{
	setThreadName("CPregameServer::handleConnection");
	try
	{
		while(!cpc->receivedStop)
		{
			CPackForSelectionScreen * cpfs = nullptr;
			*cpc >> cpfs;

			logNetwork->info("Got package to announce %s from %s", typeid(*cpfs).name(), cpc->toString());

			boost::unique_lock<boost::recursive_mutex> queueLock(mx);

			auto ws = dynamic_ptr_cast<WelcomeServer>(cpfs);
			if(ws)
			{
				cpc->names = ws->names;
				cpc->uuid = ws->uuid;

				auto wc = new WelcomeClient(*capabilities, cpc->connectionID);
				toAnnounce.push_back(wc);

				logNetwork->info("Connection with client %d established. UUID: %s", cpc->connectionID, cpc->uuid);
				for(auto & name : cpc->names)
					logNetwork->info("Client %d player: %s", cpc->connectionID, name);

				for(auto & name : cpc->names)
				{
					announceTxt(boost::str(boost::format("%s(%d) joins the game") % name % cpc->connectionID));

					auto pj = new PlayerJoined();
					pj->playerName = name;
					pj->connectionID = cpc->connectionID;
					toAnnounce.push_back(pj);
				}
			}

			bool quitting = dynamic_ptr_cast<QuitMenuWithoutStarting>(cpfs),
				startingGame = dynamic_ptr_cast<StartWithCurrentSettings>(cpfs);
			if(quitting || startingGame) //host leaves main menu or wants to start game -> we end
			{
				cpc->receivedStop = true;
				if(!cpc->sendStop)
					sendPack(cpc, *cpfs);

				if(cpc == host)
					toAnnounce.push_back(cpfs);
			}
			else
				toAnnounce.push_back(cpfs);

			if(startingGame)
			{
				//wait for sending thread to announce start
				auto unlock = vstd::makeUnlockGuard(mx);
				while(state == RUNNING)
					boost::this_thread::sleep(boost::posix_time::milliseconds(50));
			}
			else if(quitting) // Server must be stopped if host is leaving from lobby to avoid crash
			{
				CVCMIServer::shuttingDown = true;
			}
		}
	}
	catch(const std::exception & e)
	{
		boost::unique_lock<boost::recursive_mutex> queueLock(mx);
		logNetwork->error("%s dies... \nWhat happened: %s", cpc->toString(), e.what());
	}

	boost::unique_lock<boost::recursive_mutex> queueLock(mx);
	if(state != ENDING_AND_STARTING_GAME)
	{
		connections -= cpc;

		//notify other players about leaving
		auto pl = new PlayerLeft();
		pl->playerID = cpc->connectionID;

		for(auto & name : cpc->names)
			announceTxt(boost::str(boost::format("%s(%d) left the game") % name % cpc->connectionID));
		toAnnounce.push_back(pl);

		if(connections.empty())
		{
			logNetwork->error("Last connection lost, server will close itself...");
			boost::this_thread::sleep(boost::posix_time::seconds(2)); //we should never be hasty when networking
			state = ENDING_WITHOUT_START;
		}
	}

	logNetwork->info("Thread listening for %s ended", cpc->toString());
	listeningThreads--;
	vstd::clear_pointer(cpc->handler);
}

void CVCMIServer::processPack(CPackForSelectionScreen * pack)
{
	if(dynamic_ptr_cast<CPregamePackToHost>(pack))
	{
		sendPack(host, *pack);
	}
	else if(SelectMap * sm = dynamic_ptr_cast<SelectMap>(pack))
	{
		vstd::clear_pointer(curmap);
		curmap = sm->mapInfo;
		sm->free = false;
		announcePack(*pack);
	}
	else if(UpdateStartOptions * uso = dynamic_ptr_cast<UpdateStartOptions>(pack))
	{
		vstd::clear_pointer(curStartInfo);
		curStartInfo = uso->options;
		uso->free = false;
		announcePack(*pack);
	}
	else if(dynamic_ptr_cast<StartWithCurrentSettings>(pack))
	{
		state = ENDING_AND_STARTING_GAME;
		announcePack(*pack);
	}
	else
		announcePack(*pack);

	delete pack;
}

void CVCMIServer::sendPack(CConnection * pc, const CPackForSelectionScreen & pack)
{
	if(!pc->sendStop)
	{
		logNetwork->info("\tSending pack of type %s to %s", typeid(pack).name(), pc->toString());
		*pc << &pack;
	}

	if(dynamic_ptr_cast<QuitMenuWithoutStarting>(&pack))
	{
		pc->sendStop = true;
	}
	else if(dynamic_ptr_cast<StartWithCurrentSettings>(&pack))
	{
		pc->sendStop = true;
	}
}

void CVCMIServer::announcePack(const CPackForSelectionScreen & pack)
{
	for(CConnection * pc : connections)
		sendPack(pc, pack);
}

void CVCMIServer::announceTxt(const std::string & txt, const std::string & playerName)
{
	logNetwork->info("%s says: %s", playerName, txt);
	ChatMessage cm;
	cm.playerName = playerName;
	cm.message = txt;

	boost::unique_lock<boost::recursive_mutex> queueLock(mx);
	toAnnounce.push_front(new ChatMessage(cm));
}



#if defined(__GNUC__) && !defined(__MINGW32__) && !defined(VCMI_ANDROID)
void handleLinuxSignal(int sig)
{
	const int STACKTRACE_SIZE = 100;
	void * buffer[STACKTRACE_SIZE];
	int ptrCount = backtrace(buffer, STACKTRACE_SIZE);
	char * * strings;

	logGlobal->error("Error: signal %d :", sig);
	strings = backtrace_symbols(buffer, ptrCount);
	if(strings == nullptr)
	{
		logGlobal->error("There are no symbols.");
	}
	else
	{
		for(int i = 0; i < ptrCount; ++i)
		{
			logGlobal->error(strings[i]);
		}
		free(strings);
	}

	_exit(EXIT_FAILURE);
}
#endif

static void handleCommandOptions(int argc, char * argv[], boost::program_options::variables_map & options)
{
	namespace po = boost::program_options;
	po::options_description opts("Allowed options");
	opts.add_options()
	("help,h", "display help and exit")
	("version,v", "display version information and exit")
	("run-by-client", "indicate that server launched by client on same machine")
	("uuid", po::value<std::string>(), "")
	("enable-shm-uuid", "use UUID for shared memory identifier")
	("enable-shm", "enable usage of shared memory")
	("port", po::value<ui16>(), "port at which server will listen to connections from client");

	if(argc > 1)
	{
		try
		{
			po::store(po::parse_command_line(argc, argv, opts), options);
		}
		catch(std::exception & e)
		{
			std::cerr << "Failure during parsing command-line options:\n" << e.what() << std::endl;
		}
	}

	po::notify(options);
	if(options.count("help"))
	{
		auto time = std::time(0);
		printf("%s - A Heroes of Might and Magic 3 clone\n", GameConstants::VCMI_VERSION.c_str());
		printf("Copyright (C) 2007-%d VCMI dev team - see AUTHORS file\n", std::localtime(&time)->tm_year + 1900);
		printf("This is free software; see the source for copying conditions. There is NO\n");
		printf("warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n");
		printf("\n");
		std::cout << opts;
		exit(0);
	}

	if(options.count("version"))
	{
		printf("%s\n", GameConstants::VCMI_VERSION.c_str());
		std::cout << VCMIDirs::get().genHelpString();
		exit(0);
	}
}

int main(int argc, char * argv[])
{
#ifndef VCMI_ANDROID
	// Correct working dir executable folder (not bundle folder) so we can use executable relative paths
	boost::filesystem::current_path(boost::filesystem::system_complete(argv[0]).parent_path());
#endif
	// Installs a sig sev segmentation violation handler
	// to log stacktrace
    #if defined(__GNUC__) && !defined(__MINGW32__) && !defined(VCMI_ANDROID)
	signal(SIGSEGV, handleLinuxSignal);
    #endif

	console = new CConsoleHandler();
	CBasicLogConfigurator logConfig(VCMIDirs::get().userCachePath() / "VCMI_Server_log.txt", console);
	logConfig.configureDefault();
	logGlobal->info(NAME);

	boost::program_options::variables_map opts;
	handleCommandOptions(argc, argv, opts);
	preinitDLL(console);
	settings.init();
	logConfig.configure();

	loadDLLClasses();
	srand((ui32)time(nullptr));
	try
	{
		boost::asio::io_service io_service;
		CVCMIServer server(opts);

		try
		{
			while(!CVCMIServer::shuttingDown)
			{
				server.start();
			}
			io_service.run();
		}
		catch(boost::system::system_error & e) //for boost errors just log, not crash - probably client shut down connection
		{
			logNetwork->error(e.what());
			CVCMIServer::shuttingDown = true;
		}
		catch(...)
		{
			handleException();
		}
	}
	catch(boost::system::system_error & e)
	{
		logNetwork->error(e.what());
		//catch any startup errors (e.g. can't access port) errors
		//and return non-zero status so client can detect error
		throw;
	}
#ifdef VCMI_ANDROID
	CAndroidVMHelper envHelper;
	envHelper.callStaticVoidMethod(CAndroidVMHelper::NATIVE_METHODS_DEFAULT_CLASS, "killServer");
#endif
	vstd::clear_pointer(VLC);
	CResourceHandler::clear();
	return 0;
}

#ifdef VCMI_ANDROID
void CVCMIServer::create()
{
	const char * foo[1] = {"android-server"};
	main(1, const_cast<char * *>(foo));
}
#endif
