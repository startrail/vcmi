/*
 * Connection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "Connection.h"

#include "../registerTypes/RegisterTypes.h"
#include "../mapping/CMap.h"
#include "../CGameState.h"

#include <boost/asio.hpp>

using namespace boost;
using namespace boost::asio::ip;

#if defined(__hppa__) || \
	defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
	(defined(__MIPS__) && defined(__MISPEB__)) || \
	defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
	defined(__sparc__)
#define BIG_ENDIAN
#else
#define LIL_ENDIAN
#endif


void CConnection::init()
{
	socket->set_option(boost::asio::ip::tcp::no_delay(true));
	socket->set_option(boost::asio::socket_base::send_buffer_size(4194304));
	socket->set_option(boost::asio::socket_base::receive_buffer_size(4194304));

	enableSmartPointerSerialization();
	disableStackSendingByID();
	registerTypes(iser);
	registerTypes(oser);
#ifdef LIL_ENDIAN
	myEndianess = true;
#else
	myEndianess = false;
#endif
	connected = true;
	std::string pom;
	//we got connection
	oser & std::string("Aiya!\n") & uuid & myEndianess; //identify ourselves
	iser & pom & pom & contactEndianess;
	logNetwork->info("Established connection with %s", pom);
	wmx = new boost::mutex();
	rmx = new boost::mutex();

	handler = nullptr;
	receivedStop = sendStop = false;
	static int cid = 1;
	connectionID = cid++;
	iser.fileVersion = SERIALIZATION_VERSION;
}

CConnection::CConnection(std::string host, ui16 port, std::string Name)
:iser(this), oser(this), io_service(new asio::io_service), uuid(Name)
{
	int i;
	boost::system::error_code error = asio::error::host_not_found;
	socket = new tcp::socket(*io_service);
	tcp::resolver resolver(*io_service);
	tcp::resolver::iterator end, pom, endpoint_iterator = resolver.resolve(tcp::resolver::query(host, std::to_string(port)),error);
	if(error)
	{
		logNetwork->error("Problem with resolving: \n%s", error.message());
		goto connerror1;
	}
	pom = endpoint_iterator;
	if(pom != end)
		logNetwork->info("Found endpoints:");
	else
	{
		logNetwork->error("Critical problem: No endpoints found!");
		goto connerror1;
	}
	i=0;
	while(pom != end)
	{
		logNetwork->info("\t%d:%s", i, (boost::asio::ip::tcp::endpoint&)*pom);
		pom++;
	}
	i=0;
	while(endpoint_iterator != end)
	{
		logNetwork->info("Trying connection to %s(%d)", (boost::asio::ip::tcp::endpoint&)*endpoint_iterator, i++);
		socket->connect(*endpoint_iterator, error);
		if(!error)
		{
			init();
			return;
		}
		else
		{
			logNetwork->error("Problem with connecting: %s", error.message());
		}
		endpoint_iterator++;
	}

	//we shouldn't be here - error handling
connerror1:
	logNetwork->error("Something went wrong... checking for error info");
	if(error)
		logNetwork->error(error.message());
	else
		logNetwork->error("No error info. ");
	delete io_service;
	//delete socket;
	throw std::runtime_error("Can't establish connection :(");
}
CConnection::CConnection(TSocket * Socket, std::string Name )
	:iser(this), oser(this), socket(Socket),io_service(&Socket->get_io_service()), uuid(Name)//, send(this), rec(this)
{
	init();
}
CConnection::CConnection(TAcceptor * acceptor, boost::asio::io_service *Io_service, std::string Name)
: iser(this), oser(this), uuid(Name)//, send(this), rec(this)
{
	boost::system::error_code error = asio::error::host_not_found;
	socket = new tcp::socket(*io_service);
	acceptor->accept(*socket,error);
	if (error)
	{
		logNetwork->error("Error on accepting: %s", error.message());
		delete socket;
		throw std::runtime_error("Can't establish connection :(");
	}
	init();
}
int CConnection::write(const void * data, unsigned size)
{
	try
	{
		int ret;
		ret = asio::write(*socket,asio::const_buffers_1(asio::const_buffer(data,size)));
		return ret;
	}
	catch(...)
	{
		//connection has been lost
		connected = false;
		throw;
	}
}
int CConnection::read(void * data, unsigned size)
{
	try
	{
		int ret = asio::read(*socket,asio::mutable_buffers_1(asio::mutable_buffer(data,size)));
		return ret;
	}
	catch(...)
	{
		//connection has been lost
		connected = false;
		throw;
	}
}
CConnection::~CConnection(void)
{
	if(handler)
		handler->join();

	delete handler;

	close();
	delete io_service;
	delete wmx;
	delete rmx;
}

template<class T>
CConnection & CConnection::operator&(const T &t) {
//	throw std::exception();
//XXX this is temporaly ? solution to fix gcc (4.3.3, other?) compilation
//    problem for more details contact t0@czlug.icis.pcz.pl or impono@gmail.com
//    do not remove this exception it shoudnt be called
	return *this;
}

void CConnection::close()
{
	if(socket)
	{
		socket->close();
		vstd::clear_pointer(socket);
	}
}

bool CConnection::isOpen() const
{
	return socket && connected;
}

bool CConnection::isHost() const
{
	return connectionID == 1;
}

void CConnection::reportState(vstd::CLoggerBase * out)
{
	out->debug("CConnection");
	if(socket && socket->is_open())
	{
		out->debug("\tWe have an open and valid socket");
		out->debug("\t %d bytes awaiting", socket->available());
	}
}

CPack * CConnection::retreivePack()
{
	CPack *ret = nullptr;
	boost::unique_lock<boost::mutex> lock(*rmx);
	logNetwork->trace("Listening... ");
	iser & ret;
	logNetwork->trace("\treceived server message of type %s", (ret? typeid(*ret).name() : "nullptr"));
	return ret;
}

void CConnection::sendPackToServer(const CPack &pack, PlayerColor player, ui32 requestID)
{
	boost::unique_lock<boost::mutex> lock(*wmx);
	logNetwork->trace("Sending to server a pack of type %s", typeid(pack).name());
	oser & player & requestID & &pack; //packs has to be sent as polymorphic pointers!
}

void CConnection::disableStackSendingByID()
{
	CSerializer::sendStackInstanceByIds = false;
}

void CConnection::enableStackSendingByID()
{
	CSerializer::sendStackInstanceByIds = true;
}

void CConnection::disableSmartPointerSerialization()
{
	iser.smartPointerSerialization = oser.smartPointerSerialization = false;
}

void CConnection::enableSmartPointerSerialization()
{
	iser.smartPointerSerialization = oser.smartPointerSerialization = true;
}

void CConnection::prepareForSendingHeroes()
{
	iser.loadedPointers.clear();
	oser.savedPointers.clear();
	disableSmartVectorMemberSerialization();
	enableSmartPointerSerialization();
	disableStackSendingByID();
}

void CConnection::enterPregameConnectionMode()
{
	iser.loadedPointers.clear();
	oser.savedPointers.clear();
	disableSmartVectorMemberSerialization();
	disableSmartPointerSerialization();
}

void CConnection::disableSmartVectorMemberSerialization()
{
	CSerializer::smartVectorMembersSerialization = false;
}

void CConnection::enableSmartVectorMemberSerializatoin()
{
	CSerializer::smartVectorMembersSerialization = true;
}

std::string CConnection::toString() const
{
	boost::format fmt("Connection with %s (ID: %d)");
	fmt % uuid % connectionID;
	return fmt.str();
}
