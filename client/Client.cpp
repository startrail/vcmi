/*
 * Client.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Client.h"

#include <SDL.h>

#include "CMusicHandler.h"
#include "../lib/mapping/CCampaignHandler.h"
#include "../CCallback.h"
#include "../lib/CConsoleHandler.h"
#include "CGameInfo.h"
#include "../lib/CGameState.h"
#include "CPlayerInterface.h"
#include "../lib/StartInfo.h"
#include "../lib/battle/BattleInfo.h"
#include "../lib/CModHandler.h"
#include "../lib/CArtHandler.h"
#include "../lib/CGeneralTextHandler.h"
#include "../lib/CHeroHandler.h"
#include "../lib/CTownHandler.h"
#include "../lib/CBuildingHandler.h"
#include "../lib/spells/CSpellHandler.h"
#include "../lib/serializer/CTypeList.h"
#include "../lib/serializer/Connection.h"
#include "../lib/serializer/CLoadIntegrityValidator.h"
#include "../lib/NetPacks.h"
#include "../lib/VCMI_Lib.h"
#include "../lib/VCMIDirs.h"
#include "../lib/mapping/CMap.h"
#include "../lib/JsonNode.h"
#include "mapHandler.h"
#include "../lib/CConfigHandler.h"
#include "pregame/CPreGame.h"
#include "pregame/CCampaignScreen.h"
#include "battle/CBattleInterface.h"
#include "../lib/CThreadHelper.h"
#include "../lib/CScriptingModule.h"
#include "../lib/registerTypes/RegisterTypes.h"
#include "gui/CGuiHandler.h"
#include "CMT.h"
#include "CServerHandler.h"

#ifdef VCMI_ANDROID
#include "lib/CAndroidVMHelper.h"
#endif

#ifdef VCMI_ANDROID
std::atomic_bool androidTestServerReadyFlag;
#endif

ThreadSafeVector<int> CClient::waitingRequest;

CondSh<bool> CServerHandler::serverAlive(false);

template <typename T> class CApplyOnCL;

class CBaseForCLApply
{
public:
	virtual void applyOnClAfter(CClient *cl, void *pack) const =0;
	virtual void applyOnClBefore(CClient *cl, void *pack) const =0;
	virtual ~CBaseForCLApply(){}

	template<typename U> static CBaseForCLApply *getApplier(const U * t=nullptr)
	{
		return new CApplyOnCL<U>();
	}
};

template <typename T> class CApplyOnCL : public CBaseForCLApply
{
public:
	void applyOnClAfter(CClient *cl, void *pack) const override
	{
		T *ptr = static_cast<T*>(pack);
		ptr->applyCl(cl);
	}
	void applyOnClBefore(CClient *cl, void *pack) const override
	{
		T *ptr = static_cast<T*>(pack);
		ptr->applyFirstCl(cl);
	}
};

template <> class CApplyOnCL<CPack> : public CBaseForCLApply
{
public:
	void applyOnClAfter(CClient *cl, void *pack) const override
	{
		logGlobal->error("Cannot apply on CL plain CPack!");
		assert(0);
	}
	void applyOnClBefore(CClient *cl, void *pack) const override
	{
		logGlobal->error("Cannot apply on CL plain CPack!");
		assert(0);
	}
};


static CApplier<CBaseForCLApply> *applier = nullptr;

void CClient::init()
{
	waitingRequest.clear();
	{
		TLockGuard _(connectionHandlerMutex);
		connectionHandler.reset();
	}
	pathInfo = nullptr;
	applier = new CApplier<CBaseForCLApply>();
	registerTypesClientPacks1(*applier);
	registerTypesClientPacks2(*applier);
	IObjectInterface::cb = this;
	gs = nullptr;
	erm = nullptr;
	terminate = false;
}

CClient::CClient(void)
{
	init();
}

CClient::~CClient(void)
{
	delete applier;
}

void CClient::waitForMoveAndSend(PlayerColor color)
{
	try
	{
		setThreadName("CClient::waitForMoveAndSend");
		assert(vstd::contains(battleints, color));
		BattleAction ba = battleints[color]->activeStack(gs->curB->battleGetStackByID(gs->curB->activeStack, false));
		if(ba.actionType != Battle::CANCEL)
		{
			logNetwork->trace("Send battle action to server: %s", ba.toString());
			MakeAction temp_action(ba);
			sendRequest(&temp_action, color);
		}
	}
	catch(boost::thread_interrupted&)
	{
		logNetwork->debug("Wait for move thread was interrupted and no action will be send. Was a battle ended by spell?");
	}
	catch(...)
	{
		handleException();
	}
}

void CClient::run()
{
	setThreadName("CClient::run");
	try
	{
		while(!terminate)
		{
			CPack *pack = CSH->c->retreivePack(); //get the package from the server

			if (terminate)
			{
				vstd::clear_pointer(pack);
				break;
			}

			handlePack(pack);
		}
	}
	//catch only asio exceptions
	catch (const boost::system::system_error& e)
	{
		logNetwork->error("Lost connection to server, ending listening thread!");
		logNetwork->error(e.what());
		if(!terminate) //rethrow (-> boom!) only if closing connection was unexpected
		{
			logNetwork->error("Something wrong, lost connection while game is still ongoing...");
			throw;
		}
	}
}

void CClient::save(const std::string & fname)
{
	if(gs->curB)
	{
		logNetwork->error("Game cannot be saved during battle!");
		return;
	}

	SaveGame save_game(fname);
	sendRequest((CPackForClient*)&save_game, PlayerColor::NEUTRAL);
}

void CClient::endGame(bool closeConnection)
{
	//suggest interfaces to finish their stuff (AI should interrupt any bg working threads)
	for (auto& i : playerint)
		i.second->finish();

	// Game is ending
	// Tell the network thread to reach a stable state
	if (closeConnection)
		stopConnection();
	logNetwork->info("Closed connection.");

	GH.curInt = nullptr;
	{
		boost::unique_lock<boost::recursive_mutex> un(*CPlayerInterface::pim);
		logNetwork->info("Ending current game!");
		if(GH.topInt())
		{
			GH.topInt()->deactivate();
		}
		GH.listInt.clear();
		GH.objsToBlit.clear();
		GH.statusbar = nullptr;
		logNetwork->info("Removed GUI.");

		vstd::clear_pointer(const_cast<CGameInfo*>(CGI)->mh);
		vstd::clear_pointer(gs);

		logNetwork->info("Deleted mapHandler and gameState.");
		LOCPLINT = nullptr;
	}

	playerint.clear();
	battleints.clear();
	callbacks.clear();
	battleCallbacks.clear();
	CGKeys::reset();
	CGMagi::reset();
	CGObelisk::reset();
	logNetwork->info("Deleted playerInts.");
	logNetwork->info("Client stopped.");
}

void CClient::loadGame(StartInfo * si)
{
//	void loadNetworkGame()
//	{
//		client = new CClient();
//		CPlayerInterface::howManyPeople = 1;
//		client->loadGameNetwork();
//}

	if(false)//fname.empty())
	{

		logNetwork->info("MP loading procedure started!");

		try
		{
			recieveCommonState(*CSH->c);
			// logNetwork->info("Loaded common part of save %d ms", tmh.getDiff());
			const_cast<CGameInfo*>(CGI)->mh = new CMapHandler();
			const_cast<CGameInfo*>(CGI)->mh->map = gs->map;
			pathInfo = make_unique<CPathsInfo>(getMapSize());
			CGI->mh->init();
		}
		catch(std::exception &e)
		{
			throw; //obviously we cannot continue here
		}

		CSH->c->addStdVecItems(gs); /*why is this here?*/

		//*loader >> *this;


		CSH->c->enableStackSendingByID();
		CSH->c->disableSmartPointerSerialization();

		auto pid = PlayerColor(1);
		auto nInt = std::make_shared<CPlayerInterface>(pid);

		nInt->dllName = "";
		nInt->human = true;
		nInt->playerID = pid;

		installNewPlayerInterface(nInt, pid);
		return;
	}



	PlayerColor player(); //intentional shadowing
	logNetwork->info("Loading procedure started!");

////	CStopWatch tmh;
	std::unique_ptr<CLoadFile> loader;
	try
	{
		boost::filesystem::path clientSaveName = *CResourceHandler::get("local")->getResourceName(ResourceID(si->mapname, EResType::CLIENT_SAVEGAME));
		boost::filesystem::path controlServerSaveName;

		if (CResourceHandler::get("local")->existsResource(ResourceID(si->mapname, EResType::SERVER_SAVEGAME)))
		{
			controlServerSaveName = *CResourceHandler::get("local")->getResourceName(ResourceID(si->mapname, EResType::SERVER_SAVEGAME));
		}
		else// create entry for server savegame. Triggered if save was made after launch and not yet present in res handler
		{
			controlServerSaveName = boost::filesystem::path(clientSaveName).replace_extension(".vsgm1");
			CResourceHandler::get("local")->createResource(controlServerSaveName.string(), true);
		}

		if(clientSaveName.empty())
			throw std::runtime_error("Cannot open client part of " + si->mapname);
		if(controlServerSaveName.empty() || !boost::filesystem::exists(controlServerSaveName))
			throw std::runtime_error("Cannot open server part of " + si->mapname);

		{
			CLoadIntegrityValidator checkingLoader(clientSaveName, controlServerSaveName, MINIMAL_SERIALIZATION_VERSION);
			loadCommonState(checkingLoader);
			loader = checkingLoader.decay();
		}

	}
	catch(std::exception &e)
	{
		logGlobal->error("Cannot load game %s. Error: %s", si->mapname, e.what());
		throw; //obviously we cannot continue here
	}

/*
	if(!server)
		 player = PlayerColor(player_);
*/

	//*loader >> *this;

/////	if(server)
////	{
////		ui8 pom8;
////		*CSH->c << ui8(3) << ui8(loadNumPlayers); //load game; one client if single-player
////		*CSH->c << fname;
		//recieveCommonState(*serv);
////		logNetwork->info("Loaded common part of save %d ms", tmh.getDiff());
		const_cast<CGameInfo*>(CGI)->mh = new CMapHandler();
		const_cast<CGameInfo*>(CGI)->mh->map = gs->map;
		pathInfo = make_unique<CPathsInfo>(getMapSize());
		CGI->mh->init();
////		logNetwork->info("Initing maphandler: %d ms", tmh.getDiff());
////		*CSH->c >> pom8;
////		if(pom8)
////			throw std::runtime_error("Server cannot open the savegame!");
////		else
////			logNetwork->info("Server opened savegame properly.");
////	}

/*
		std::vector<std::pair<PlayerColor, PlayerSettings>> humanPlayerInfos;
		auto selector = [](std::pair<PlayerColor, PlayerSettings> x) -> bool { return x.second.playerID > 0;};
		std::copy_if(options->playerInfos.begin(), options->playerInfos.end(), std::back_inserter(humanPlayerInfos), selector);

		std::vector<int> humanPlayerIndices;
		for(auto playerInfo : humanPlayerInfos)
			humanPlayerIndices.push_back(playerInfo.first.getNum());
*/
////	if(server)
////	{
		std::set<PlayerColor> clientPlayers;
		for(auto & elem : gs->scenarioOps->playerInfos)
		{
////			if(!std::count(humanplayerindices.begin(),humanplayerindices.end(),elem.first.getNum()) || elem.first==player)
				clientPlayers.insert(elem.first);
		}
		clientPlayers.insert(PlayerColor::NEUTRAL);
////	}
//	else
//	{
//		clientPlayers.insert(player);
//	}

	serialize(loader->serializer, loader->serializer.fileVersion, clientPlayers);
	*CSH->c << clientPlayers;
	CSH->c->addStdVecItems(gs); /*why is this here?*/

	//*loader >> *this;
////	logNetwork->info("Loaded client part of save %d ms", tmh.getDiff());

////	logNetwork->info("Sent info to server: %d ms", tmh.getDiff());

	//*serv << clientPlayers;
	CSH->c->enableStackSendingByID();
	CSH->c->disableSmartPointerSerialization();
}

void CClient::newGame(StartInfo *si)
{
	enum {SINGLE, HOST, GUEST} networkMode = SINGLE;

	networkMode = CSH->c->isHost() ? HOST : GUEST;
	CStopWatch tmh;
	logNetwork->info("\tSending/Getting info to/from the server: %d ms", tmh.getDiff());
	CSH->c->enableStackSendingByID();
	CSH->c->disableSmartPointerSerialization();

	// Initialize game state
	gs = new CGameState();
	logNetwork->info("\tCreating gamestate: %i",tmh.getDiff());

	*CSH->c >> si;
	gs->init(si, settings["general"]["saveRandomMaps"].Bool());
	logNetwork->info("Initializing GameState (together): %d ms", tmh.getDiff());

	// Now after possible random map gen, we know exact player count.
	// Inform server about how many players client handles
	std::set<PlayerColor> myPlayers;
	for(auto & elem : gs->scenarioOps->playerInfos)
	{
		if((networkMode != SINGLE && vstd::contains(CSH->myPlayers, elem.second.playerID))      //multi - client has only "its players"
		   || (networkMode == HOST && elem.second.playerID == PlayerSettings::PLAYER_AI))//multi - host has all AI players
		{
			myPlayers.insert(elem.first); //add player
			logGlobal->warn("MY player %d", elem.first);
		}
	}
	logGlobal->warn("MY connectionId %d", CSH->c->connectionID);
	if(networkMode != GUEST)
		myPlayers.insert(PlayerColor::NEUTRAL);

	*CSH->c << myPlayers;

	// Init map handler
	if(gs->map)
	{
		if(!settings["session"]["headless"].Bool())
		{
			const_cast<CGameInfo*>(CGI)->mh = new CMapHandler();
			CGI->mh->map = gs->map;
			logNetwork->info("Creating mapHandler: %d ms", tmh.getDiff());
			CGI->mh->init();
		}
		pathInfo = make_unique<CPathsInfo>(getMapSize());
		logNetwork->info("Initializing mapHandler (together): %d ms", tmh.getDiff());
	}

	int humanPlayers = 0;
	for(auto & elem : gs->scenarioOps->playerInfos)//initializing interfaces for players
	{
		PlayerColor color = elem.first;
		gs->currentPlayer = color;
		if(!vstd::contains(myPlayers, color))
			continue;

		logNetwork->trace("Preparing interface for player %s", color.getStr());
		if(elem.second.playerID == PlayerSettings::PLAYER_AI)
		{
			auto AiToGive = aiNameForPlayer(elem.second, false);
			logNetwork->info("Player %s will be lead by %s", color, AiToGive);
			installNewPlayerInterface(CDynLibHandler::getNewAI(AiToGive), color);
		}
		else
		{
			installNewPlayerInterface(std::make_shared<CPlayerInterface>(color), color);
			humanPlayers++;
		}
	}

	if(settings["session"]["spectate"].Bool())
	{
		installNewPlayerInterface(std::make_shared<CPlayerInterface>(PlayerColor::SPECTATOR), PlayerColor::SPECTATOR, true);
	}
	loadNeutralBattleAI();

	CSH->c->addStdVecItems(gs);
}

void CClient::serialize(BinarySerializer & h, const int version)
{
	assert(h.saving);
	bool hotSeat = 1;//TODO:COMPATIBILITY
	h & hotSeat;
	{
		ui8 players = playerint.size();
		h & players;

		for(auto i = playerint.begin(); i != playerint.end(); i++)
		{
			LOG_TRACE_PARAMS(logGlobal, "Saving player %s interface", i->first);
			assert(i->first == i->second->playerID);
			h & i->first;
			h & i->second->dllName;
			h & i->second->human;
			i->second->saveGame(h, version);
		}
	}
}

void CClient::serialize(BinaryDeserializer & h, const int version)
{
	assert(!h.saving);
	bool hotSeat = 1;//TODO:COMPATIBILITY
	h & hotSeat;
	{
		ui8 players = 0; //fix for uninitialized warning
		h & players;

		for(int i=0; i < players; i++)
		{
			std::string dllname;
			PlayerColor pid;
			bool isHuman = false;

			h & pid;
			h & dllname;
			h & isHuman;
			LOG_TRACE_PARAMS(logGlobal, "Loading player %s interface", pid);

			std::shared_ptr<CGameInterface> nInt;
			if(dllname.length())
			{
				if(pid == PlayerColor::NEUTRAL)
				{
					installNewBattleInterface(CDynLibHandler::getNewBattleAI(dllname), pid);
					//TODO? consider serialization
					continue;
				}
				else
				{
					assert(!isHuman);
					nInt = CDynLibHandler::getNewAI(dllname);
				}
			}
			else
			{
				assert(isHuman);
				nInt = std::make_shared<CPlayerInterface>(pid);
			}

			nInt->dllName = dllname;
			nInt->human = isHuman;
			nInt->playerID = pid;

			installNewPlayerInterface(nInt, pid);
			nInt->loadGame(h, version); //another evil cast, check above
		}

		if(!vstd::contains(battleints, PlayerColor::NEUTRAL))
			loadNeutralBattleAI();
	}
}

void CClient::serialize(BinarySerializer & h, const int version, const std::set<PlayerColor> & playerIDs)
{
	assert(h.saving);
	bool hotSeat = 1;//TODO:COMPATIBILITY
	h & hotSeat;
	{
		ui8 players = playerint.size();
		h & players;

		for(auto i = playerint.begin(); i != playerint.end(); i++)
		{
			LOG_TRACE_PARAMS(logGlobal, "Saving player %s interface", i->first);
			assert(i->first == i->second->playerID);
			h & i->first;
			h & i->second->dllName;
			h & i->second->human;
			i->second->saveGame(h, version);
		}
	}
}

void CClient::serialize(BinaryDeserializer & h, const int version, const std::set<PlayerColor> & playerIDs)
{
	assert(!h.saving);
	bool hotSeat = 1;//TODO:COMPATIBILITY
	h & hotSeat;
	{
		ui8 players = 0; //fix for uninitialized warning
		h & players;

		for(int i=0; i < players; i++)
		{
			std::string dllname;
			PlayerColor pid;
			bool isHuman = false;

			h & pid;
			h & dllname;
			h & isHuman;
			LOG_TRACE_PARAMS(logGlobal, "Loading player %s interface", pid);

			std::shared_ptr<CGameInterface> nInt;
			if(dllname.length())
			{
				if(pid == PlayerColor::NEUTRAL)
				{
					if(playerIDs.count(pid))
						installNewBattleInterface(CDynLibHandler::getNewBattleAI(dllname), pid);
					//TODO? consider serialization
					continue;
				}
				else
				{
					assert(!isHuman);
					nInt = CDynLibHandler::getNewAI(dllname);
				}
			}
			else
			{
				assert(isHuman);
				nInt = std::make_shared<CPlayerInterface>(pid);
			}

			nInt->dllName = dllname;
			nInt->human = isHuman;
			nInt->playerID = pid;

			nInt->loadGame(h, version);
			if(settings["session"]["onlyai"].Bool() && isHuman)
			{
				removeGUI();
				nInt.reset();
				dllname = aiNameForPlayer(false);
				nInt = CDynLibHandler::getNewAI(dllname);
				nInt->dllName = dllname;
				nInt->human = false;
				nInt->playerID = pid;
				installNewPlayerInterface(nInt, pid);
				GH.totalRedraw();
			}
			else
			{
				if(playerIDs.count(pid))
					installNewPlayerInterface(nInt, pid);
			}
		}
		if(settings["session"]["spectate"].Bool())
		{
			removeGUI();
			auto p = std::make_shared<CPlayerInterface>(PlayerColor::SPECTATOR);
			installNewPlayerInterface(p, PlayerColor::SPECTATOR, true);
			GH.curInt = p.get();
			LOCPLINT->activateForSpectator();
			GH.totalRedraw();
		}

		if(playerIDs.count(PlayerColor::NEUTRAL))
			loadNeutralBattleAI();
	}
}

void CClient::handlePack( CPack * pack )
{
	if(pack == nullptr)
	{
		logNetwork->error("Dropping nullptr CPack! You should check whether client and server ABI matches.");
		return;
	}
	CBaseForCLApply *apply = applier->getApplier(typeList.getTypeID(pack)); //find the applier
	if(apply)
	{
		boost::unique_lock<boost::recursive_mutex> guiLock(*CPlayerInterface::pim);
		apply->applyOnClBefore(this, pack);
		logNetwork->trace("\tMade first apply on cl");
		gs->apply(pack);
		logNetwork->trace("\tApplied on gs");
		apply->applyOnClAfter(this, pack);
		logNetwork->trace("\tMade second apply on cl");
	}
	else
	{
		logNetwork->error("Message %s cannot be applied, cannot find applier!", typeList.getTypeInfo(pack)->name());
	}
	delete pack;
}

void CClient::finishCampaign( std::shared_ptr<CCampaignState> camp )
{
}

void CClient::proposeNextMission(std::shared_ptr<CCampaignState> camp)
{
	GH.pushInt(new CBonusSelection(camp));
}

void CClient::stopConnection()
{
	terminate = true;

	if(CSH->c)
	{
		boost::unique_lock<boost::mutex>(*CSH->c->wmx);
		if(CSH->c->isHost()) //request closing connection
		{
			logNetwork->info("Connection has been requested to be closed.");
			CloseServer close_server;
			sendRequest(&close_server, PlayerColor::NEUTRAL);
			logNetwork->info("Sent closing signal to the server");
		}
		else
		{
			LeaveGame leave_Game;
			sendRequest(&leave_Game, PlayerColor::NEUTRAL);
			logNetwork->info("Sent leaving signal to the server");
		}
	}

	{
		TLockGuard _(connectionHandlerMutex);
		if(connectionHandler)//end connection handler
		{
			if(connectionHandler->get_id() != boost::this_thread::get_id())
				connectionHandler->join();

			logNetwork->info("Connection handler thread joined");
			connectionHandler.reset();
		}
	}


	if (CSH->c) //and delete connection
	{
		CSH->c->close();
		vstd::clear_pointer(CSH->c);
		logNetwork->warn("Our socket has been closed.");
	}
}

void CClient::battleStarted(const BattleInfo * info)
{
	for(auto &battleCb : battleCallbacks)
	{
		if(vstd::contains_if(info->sides, [&](const SideInBattle& side) {return side.color == battleCb.first; })
			||  battleCb.first >= PlayerColor::PLAYER_LIMIT)
		{
			battleCb.second->setBattle(info);
		}
	}
// 	for(ui8 side : info->sides)
// 		if(battleCallbacks.count(side))
// 			battleCallbacks[side]->setBattle(info);

	std::shared_ptr<CPlayerInterface> att, def;
	auto &leftSide = info->sides[0], &rightSide = info->sides[1];


	//If quick combat is not, do not prepare interfaces for battleint
	if(!settings["adventure"]["quickCombat"].Bool())
	{
		if(vstd::contains(playerint, leftSide.color) && playerint[leftSide.color]->human)
			att = std::dynamic_pointer_cast<CPlayerInterface>( playerint[leftSide.color] );

		if(vstd::contains(playerint, rightSide.color) && playerint[rightSide.color]->human)
			def = std::dynamic_pointer_cast<CPlayerInterface>( playerint[rightSide.color] );
	}

	if(!settings["session"]["headless"].Bool())
	{
		if(!!att || !!def)
		{
			boost::unique_lock<boost::recursive_mutex> un(*CPlayerInterface::pim);
			auto bi = new CBattleInterface(leftSide.armyObject, rightSide.armyObject, leftSide.hero, rightSide.hero,
				Rect((screen->w - 800)/2,
					 (screen->h - 600)/2, 800, 600), att, def);

			GH.pushInt(bi);
		}
		else if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		{
			//TODO: This certainly need improvement
			auto spectratorInt = std::dynamic_pointer_cast<CPlayerInterface>(playerint[PlayerColor::SPECTATOR]);
			spectratorInt->cb->setBattle(info);
			boost::unique_lock<boost::recursive_mutex> un(*CPlayerInterface::pim);
			auto bi = new CBattleInterface(leftSide.armyObject, rightSide.armyObject, leftSide.hero, rightSide.hero,
				Rect((screen->w - 800)/2,
					 (screen->h - 600)/2, 800, 600), att, def, spectratorInt);

			GH.pushInt(bi);
		}
	}

	auto callBattleStart = [&](PlayerColor color, ui8 side){
		if(vstd::contains(battleints, color))
			battleints[color]->battleStart(leftSide.armyObject, rightSide.armyObject, info->tile, leftSide.hero, rightSide.hero, side);
	};

	callBattleStart(leftSide.color, 0);
	callBattleStart(rightSide.color, 1);
	callBattleStart(PlayerColor::UNFLAGGABLE, 1);
	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		callBattleStart(PlayerColor::SPECTATOR, 1);

	if(info->tacticDistance && vstd::contains(battleints,info->sides[info->tacticsSide].color))
	{
		boost::thread(&CClient::commenceTacticPhaseForInt, this, battleints[info->sides[info->tacticsSide].color]);
	}
}

void CClient::battleFinished()
{
	stopAllBattleActions();
	for(auto & side : gs->curB->sides)
		if(battleCallbacks.count(side.color))
			battleCallbacks[side.color]->setBattle(nullptr);

	if(settings["session"]["spectate"].Bool() && !settings["session"]["spectate-skip-battle"].Bool())
		battleCallbacks[PlayerColor::SPECTATOR]->setBattle(nullptr);
}

void CClient::loadNeutralBattleAI()
{
	installNewBattleInterface(CDynLibHandler::getNewBattleAI(settings["server"]["neutralAI"].String()), PlayerColor::NEUTRAL);
}

void CClient::commitPackage( CPackForClient *pack )
{
	CommitPackage cp;
	cp.freePack = false;
	cp.packToCommit = pack;
	sendRequest(&cp, PlayerColor::NEUTRAL);
}

PlayerColor CClient::getLocalPlayer() const
{
	if(LOCPLINT)
		return LOCPLINT->playerID;
	return getCurrentPlayer();
}

void CClient::commenceTacticPhaseForInt(std::shared_ptr<CBattleGameInterface> battleInt)
{
	setThreadName("CClient::commenceTacticPhaseForInt");
	try
	{
		battleInt->yourTacticPhase(gs->curB->tacticDistance);
		if(gs && !!gs->curB && gs->curB->tacticDistance) //while awaiting for end of tactics phase, many things can happen (end of battle... or game)
		{
			MakeAction ma(BattleAction::makeEndOFTacticPhase(gs->curB->playerToSide(battleInt->playerID).get()));
			sendRequest(&ma, battleInt->playerID);
		}
	}
	catch(...)
	{
		handleException();
	}
}

void CClient::invalidatePaths()
{
	// turn pathfinding info into invalid. It will be regenerated later
	boost::unique_lock<boost::mutex> pathLock(pathInfo->pathMx);
	pathInfo->hero = nullptr;
}

const CPathsInfo * CClient::getPathsInfo(const CGHeroInstance *h)
{
	assert(h);
	boost::unique_lock<boost::mutex> pathLock(pathInfo->pathMx);
	if (pathInfo->hero != h)
	{
		gs->calculatePaths(h, *pathInfo.get());
	}
	return pathInfo.get();
}

int CClient::sendRequest(const CPack *request, PlayerColor player)
{
	static ui32 requestCounter = 0;

	ui32 requestID = requestCounter++;
	logNetwork->trace("Sending a request \"%s\". It'll have an ID=%d.", typeid(*request).name(), requestID);

	waitingRequest.pushBack(requestID);
	CSH->c->sendPackToServer(*request, player, requestID);
	if(vstd::contains(playerint, player))
		playerint[player]->requestSent(dynamic_cast<const CPackForServer*>(request), requestID);

	return requestID;
}

void CClient::campaignMapFinished( std::shared_ptr<CCampaignState> camp )
{
	endGame(false);

	GH.curInt = CGPreGame::create();
	auto & epilogue = camp->camp->scenarios[camp->mapsConquered.back()].epilog;
	auto finisher = [=]()
	{
		if(camp->mapsRemaining.size())
			proposeNextMission(camp);
		else
			finishCampaign(camp);
	};
	if(epilogue.hasPrologEpilog)
	{
		GH.pushInt(new CPrologEpilogVideo(epilogue, finisher));
	}
	else
	{
		finisher();
	}
}

void CClient::installNewPlayerInterface(std::shared_ptr<CGameInterface> gameInterface, boost::optional<PlayerColor> color, bool battlecb)
{
	boost::unique_lock<boost::recursive_mutex> un(*CPlayerInterface::pim);
	PlayerColor colorUsed = color.get_value_or(PlayerColor::UNFLAGGABLE);

	if(!color)
		privilagedGameEventReceivers.push_back(gameInterface);

	playerint[colorUsed] = gameInterface;

	logGlobal->trace("\tInitializing the interface for player %s", colorUsed);
	auto cb = std::make_shared<CCallback>(gs, color, this);
	callbacks[colorUsed] = cb;
	battleCallbacks[colorUsed] = cb;
	gameInterface->init(cb);

	installNewBattleInterface(gameInterface, color, battlecb);
}

void CClient::installNewBattleInterface(std::shared_ptr<CBattleGameInterface> battleInterface, boost::optional<PlayerColor> color, bool needCallback)
{
	boost::unique_lock<boost::recursive_mutex> un(*CPlayerInterface::pim);
	PlayerColor colorUsed = color.get_value_or(PlayerColor::UNFLAGGABLE);

	if(!color)
		privilagedBattleEventReceivers.push_back(battleInterface);

	battleints[colorUsed] = battleInterface;

	if(needCallback)
	{
		logGlobal->trace("\tInitializing the battle interface for player %s", *color);
		auto cbc = std::make_shared<CBattleCallback>(gs, color, this);
		battleCallbacks[colorUsed] = cbc;
		battleInterface->init(cbc);
	}
}

std::string CClient::aiNameForPlayer(const PlayerSettings &ps, bool battleAI)
{
	if(ps.name.size())
	{
		const boost::filesystem::path aiPath = VCMIDirs::get().fullLibraryPath("AI", ps.name);
		if (boost::filesystem::exists(aiPath))
			return ps.name;
	}

	return aiNameForPlayer(battleAI);
}

std::string CClient::aiNameForPlayer(bool battleAI)
{
	const int sensibleAILimit = settings["session"]["oneGoodAI"].Bool() ? 1 : PlayerColor::PLAYER_LIMIT_I;
	std::string goodAI = battleAI ? settings["server"]["neutralAI"].String() : settings["server"]["playerAI"].String();
	std::string badAI = battleAI ? "StupidAI" : "EmptyAI";


	//TODO what about human players
	if(battleints.size() >= sensibleAILimit)
		return badAI;

	return goodAI;
}

void CClient::startPlayerBattleAction(PlayerColor color)
{
	stopPlayerBattleAction(color);

	if(vstd::contains(battleints, color))
	{
		auto thread = std::make_shared<boost::thread>(std::bind(&CClient::waitForMoveAndSend, this, color));
		playerActionThreads[color] = thread;
	}
}

void CClient::stopPlayerBattleAction(PlayerColor color)
{
	if(vstd::contains(playerActionThreads, color))
	{
		auto thread = playerActionThreads.at(color);
		if(thread->joinable())
		{
			thread->interrupt();
			thread->join();
		}
		playerActionThreads.erase(color);
	}

}

void CClient::stopAllBattleActions()
{
	while(!playerActionThreads.empty())
		stopPlayerBattleAction(playerActionThreads.begin()->first);
}

#ifdef VCMI_ANDROID
extern "C" JNIEXPORT void JNICALL Java_eu_vcmi_vcmi_NativeMethods_notifyServerReady(JNIEnv * env, jobject cls)
{
	logNetwork->info("Received server ready signal");
	androidTestServerReadyFlag.store(true);
}

extern "C" JNIEXPORT bool JNICALL Java_eu_vcmi_vcmi_NativeMethods_tryToSaveTheGame(JNIEnv * env, jobject cls)
{
	logGlobal->info("Received emergency save game request");
	if(!LOCPLINT || !LOCPLINT->cb)
	{
		return false;
	}

	LOCPLINT->cb->save("Saves/_Android_Autosave");
	return true;
}
#endif
