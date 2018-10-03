/*
Copyright © 2017-2018,
Battelle Memorial Institute; Lawrence Livermore National Security, LLC; Alliance for Sustainable Energy, LLC
All rights reserved. See LICENSE file and DISCLAIMER for more details.
*/
#include "TcpComms.h"
#include "../../common/AsioServiceManager.h"
#include "../ActionMessage.hpp"
#include "../NetworkBrokerData.hpp"
#include "TcpHelperClasses.h"
#include <memory>

static const int BEGIN_OPEN_PORT_RANGE = 24228;
static const int BEGIN_OPEN_PORT_RANGE_SUBBROKER = 24328;

static const int DEFAULT_TCP_BROKER_PORT_NUMBER = 24160;

namespace helics
{
namespace tcp
{
using boost::asio::ip::tcp;

static inline auto tcpnet(interface_networks net)
{
	return (net != interface_networks::ipv6) ? tcp::v4() : tcp::v6();
}


TcpComms::TcpComms () noexcept {}

/** load network information into the comms object*/
void TcpComms::loadNetworkInfo (const NetworkBrokerData &netInfo)
{
    CommsInterface::loadNetworkInfo (netInfo);
	if (!propertyLock())
	{
        return;
	}
    brokerPort = netInfo.brokerPort;
    PortNumber = netInfo.portNumber;
    stripProtocol(brokerTarget_);
    if (localTarget_.empty ())
    {
        if ((brokerTarget_ == "127.0.0.1") || (brokerTarget_ == "localhost"))
        {
            localTarget_ = "localhost";
        }
        else if (brokerTarget_.empty ())
        {
            switch (interfaceNetwork)
            {
            case interface_networks::local:
                localTarget_ = "localhost";
                break;
            default:
                localTarget_ = "*";
                break;
            }
        }
        else
        {
			localTarget_ = generateMatchingInterfaceAddress(brokerTarget_, interfaceNetwork);
        }
    }
    else
    {
        stripProtocol(localTarget_);
    }
    if (netInfo.portStart > 0)
    {
        openPortStart = netInfo.portStart;
    }
    if (PortNumber > 0)
    {
        autoPortNumber = false;
    }
    reuse_address = netInfo.reuse_address;
    propertyUnLock ();
}

/** destructor*/
TcpComms::~TcpComms () { disconnect (); }

void TcpComms::setBrokerPort (int brokerPortNumber)
{
    if (propertyLock())
    {
        brokerPort = brokerPortNumber;
        propertyUnLock ();
    }
}

int TcpComms::findOpenPort ()
{
    int start = openPortStart;
    if (openPortStart < 0)
    {
        start = (hasBroker) ? BEGIN_OPEN_PORT_RANGE_SUBBROKER : BEGIN_OPEN_PORT_RANGE;
    }
    while (usedPortNumbers.find (start) != usedPortNumbers.end ())
    {
        ++start;
    }
    usedPortNumbers.insert (start);
    return start;
}

void TcpComms::setPortNumber (int localPortNumber)
{
    if (propertyLock ())
    {
        PortNumber = localPortNumber;
        if (PortNumber > 0)
        {
            autoPortNumber = false;
        }
        propertyUnLock ();
    }
}

void TcpComms::setAutomaticPortStartPort (int startingPort) {
	if (propertyLock())
    {
        openPortStart = startingPort;
        propertyUnLock ();
    }
}

int TcpComms::processIncomingMessage (ActionMessage &M)
{
    if (isProtocolCommand (M))
    {
        switch (M.messageID)
        {
        case CLOSE_RECEIVER:
            return (-1);
        default:
            break;
        }
    }
    ActionCallback (std::move (M));
    return 0;
}

ActionMessage TcpComms::generateReplyToIncomingMessage (ActionMessage &M)
{
    if (isProtocolCommand (M))
    {
        switch (M.messageID)
        {
        case QUERY_PORTS:
        {
            ActionMessage portReply (CMD_PROTOCOL);
            portReply.messageID = PORT_DEFINITIONS;
            portReply.source_id = PortNumber;
            return portReply;
        }
        break;
        case REQUEST_PORTS:
        {
            auto openPort = findOpenPort ();
            ActionMessage portReply (CMD_PROTOCOL);
            portReply.messageID = PORT_DEFINITIONS;
            portReply.source_id = PortNumber;
            portReply.source_handle = openPort;
            return portReply;
        }
        break;
        default:
			break;
        }
    }
    ActionMessage resp (CMD_IGNORE);
    return resp;
}


size_t TcpComms::dataReceive (std::shared_ptr<TcpConnection> connection, const char *data, size_t bytes_received)
{
    size_t used_total = 0;
    while (used_total < bytes_received)
    {
        ActionMessage m;
        auto used = m.depacketize (data + used_total, bytes_received - used_total);
        if (used == 0)
        {
            break;
        }
        if (isProtocolCommand (m))
        {
			//if the reply is not ignored respond with it otherwise
			//forward the original message on to the receiver to handle
            auto rep = generateReplyToIncomingMessage (m);
			if (rep.action() != CMD_IGNORE)
			{
				try
				{
					connection->send(rep.packetize());
				}
				catch (const boost::system::system_error &se)
				{
				}
			}
			else
			{
				rxMessageQueue.push(std::move(m));
			}
            
        }
        else
        {
            if (ActionCallback)
            {
                ActionCallback (std::move (m));
            }
        }
        used_total += used;
    }

    return used_total;
}

bool TcpComms::commErrorHandler (std::shared_ptr<TcpConnection> /*connection*/,
                                 const boost::system::error_code &error)
{
    if (getRxStatus () == connection_status::connected)
    {
        if ((error != boost::asio::error::eof) && (error != boost::asio::error::operation_aborted))
        {
            if (error != boost::asio::error::connection_reset)
            {
                logError (std::string ("error message while connected ") + error.message () + " code " +
                          std::to_string (error.value ()));
            }
        }
    }
    return false;
}

void TcpComms::queue_rx_function ()
{
    while (PortNumber < 0)
    {
        auto message = rxMessageQueue.pop ();
        if (isProtocolCommand (message))
        {
            switch (message.messageID)
            {
            case PORT_DEFINITIONS:
            {
                auto mp = message.source_handle;
                if (openPortStart < 0)
                {
                    if (mp < BEGIN_OPEN_PORT_RANGE)
                    {
                        openPortStart = BEGIN_OPEN_PORT_RANGE;
                    }
                    else if (mp < BEGIN_OPEN_PORT_RANGE_SUBBROKER)
                    {
                        openPortStart = BEGIN_OPEN_PORT_RANGE_SUBBROKER + (mp - BEGIN_OPEN_PORT_RANGE) * 10;
                    }
                    else
                    {
                        openPortStart =
                          BEGIN_OPEN_PORT_RANGE_SUBBROKER + (mp - BEGIN_OPEN_PORT_RANGE_SUBBROKER) * 10 + 10;
                    }
                }
                PortNumber = mp;
            }

            break;
            case CLOSE_RECEIVER:
            case DISCONNECT:
                disconnecting = true;
                setRxStatus (connection_status::terminated);
                return;
            }
        }
    }
    if (PortNumber < 0)
    {
        setRxStatus (connection_status::error);
        return;
    }
    auto ioserv = AsioServiceManager::getServicePointer ();
    auto server = helics::tcp::TcpServer::create (ioserv->getBaseService (), localTarget_, PortNumber,
                                                  reuse_address, maxMessageSize_);
    while (!server->isReady ())
    {
        if ((autoPortNumber) && (hasBroker))
        {  // If we failed and we are on an automatically assigned port number,  just try a different port
            server->close ();
            ++PortNumber;
            server = helics::tcp::TcpServer::create (ioserv->getBaseService (), localTarget_, PortNumber,
                                                     reuse_address, maxMessageSize_);
        }
        else
        {
            logWarning("retrying tcp bind");
            std::this_thread::sleep_for (std::chrono::milliseconds (150));
            auto connected = server->reConnect (connectionTimeout);
            if (!connected)
            {
                logError("unable to bind to tcp connection socket");
                server->close ();
                setRxStatus (connection_status::error);
                return;
            }
        }
    }
    auto serviceLoop = ioserv->runServiceLoop ();
    server->setDataCall ([this](TcpConnection::pointer connection, const char *data, size_t datasize) {
        return dataReceive (connection, data, datasize);
    });
    server->setErrorCall ([this](TcpConnection::pointer connection, const boost::system::error_code &error) {
        return commErrorHandler (connection, error);
    });
    server->start ();
    setRxStatus (connection_status::connected);
    bool loopRunning = true;
    while (loopRunning)
    {
        auto message = rxMessageQueue.pop ();
        if (isProtocolCommand (message))
        {
            switch (message.messageID)
            {
            case CLOSE_RECEIVER:
            case DISCONNECT:
                loopRunning = false;
                break;
            }
        }
    }

    disconnecting = true;
    server->close ();
    setRxStatus (connection_status::terminated);
}

void TcpComms::txReceive (const char *data, size_t bytes_received, const std::string &errorMessage)
{
    if (errorMessage.empty ())
    {
        ActionMessage m (data, bytes_received);
        if (isProtocolCommand (m))
        {
            if (m.messageID == PORT_DEFINITIONS)
            {
                rxMessageQueue.push (m);
            }
            else if (m.messageID == DISCONNECT)
            {
                txQueue.emplace (-1, m);
            }
        }
    }
}


bool TcpComms::establishBrokerConnection(std::shared_ptr<AsioServiceManager> &ioserv, std::shared_ptr<TcpConnection> &brokerConnection)
{
	if (brokerPort < 0)
	{
		brokerPort = DEFAULT_TCP_BROKER_PORT_NUMBER;
	}
	try
	{
		brokerConnection = TcpConnection::create(ioserv->getBaseService(), brokerTarget_,
			std::to_string(brokerPort), maxMessageSize_);
		int cumsleep = 0;
		if (!brokerConnection->waitUntilConnected(connectionTimeout))
		{
			logError("initial connection to broker timed out");
			setTxStatus(connection_status::terminated);
			return false;
		}

		if (PortNumber <= 0)
		{
			ActionMessage m(CMD_PROTOCOL_PRIORITY);
			m.messageID = REQUEST_PORTS;
			try
			{
				brokerConnection->send(m.packetize());
			}
			catch (const boost::system::system_error &error)
			{
				logError(std::string("error in initial send to broker ") + error.what());
				setTxStatus(connection_status::terminated);
				return false;
			}
			std::vector<char> rx(512);
			tcp::endpoint brk;
			brokerConnection->async_receive(rx.data(), 128,
				[this, &rx](const boost::system::error_code &error,
					size_t bytes) {
				if (error != boost::asio::error::operation_aborted)
				{
					if (!error)
					{
						txReceive(rx.data(), bytes, std::string());
					}
					else
					{
						txReceive(rx.data(), bytes, error.message());
					}
				}
			});
			cumsleep = 0;
			while (PortNumber < 0)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				auto mess = txQueue.try_pop();
				if (mess)
				{
					if (isProtocolCommand(mess->second))
					{
						if (mess->second.messageID == PORT_DEFINITIONS)
						{
							rxMessageQueue.push(mess->second);
						}
						else if (mess->second.messageID == DISCONNECT)
						{
							brokerConnection->cancel();
							setTxStatus(connection_status::terminated);
							return false;
						}
					}
				}
				cumsleep += 100;
				if (cumsleep >= connectionTimeout)
				{
					brokerConnection->cancel();
					logError("port number query to broker timed out");
					setTxStatus(connection_status::terminated);
					return false;
				}
			}
		}
	}
	catch (std::exception &e)
	{
		logError(e.what());
	}
	return true;
}

void TcpComms::queue_tx_function ()
{
    std::vector<char> buffer;
    auto ioserv = AsioServiceManager::getServicePointer ();
    auto serviceLoop = ioserv->runServiceLoop ();
    TcpConnection::pointer brokerConnection;

    std::map<int, TcpConnection::pointer> routes;  // for all the other possible routes
    if (!brokerTarget_.empty ())
    {
        hasBroker = true;
    }
    if (hasBroker)
    {
		if (!establishBrokerConnection(ioserv, brokerConnection))
		{
			return;
		}
    }
    else
    {
        if (PortNumber < 0)
        {
            PortNumber = DEFAULT_TCP_BROKER_PORT_NUMBER;
            ActionMessage m (CMD_PROTOCOL);
            m.messageID = PORT_DEFINITIONS;
            m.source_handle = PortNumber;
            rxMessageQueue.push (m);
        }
    }
    setTxStatus (connection_status::connected);

    //  std::vector<ActionMessage> txlist;
    while (true)
    {
        int route_id;
        ActionMessage cmd;

        std::tie (route_id, cmd) = txQueue.pop ();
        bool processed = false;
        if (isProtocolCommand (cmd))
        {
            if (route_id == -1)
            {
                switch (cmd.messageID)
                {
                case NEW_ROUTE:
                {
                    auto &newroute = cmd.payload;

                    try
                    {
                        std::string interface;
                        std::string port;
                        std::tie (interface, port) = extractInterfaceandPortString (newroute);
                        auto new_connect = TcpConnection::create (ioserv->getBaseService (), interface, port);

                        routes.emplace (cmd.dest_id, std::move (new_connect));
                    }
                    catch (std::exception &e)
                    {
                        // TODO:: do something???
                    }
                    processed = true;
                }
                break;
                case CLOSE_RECEIVER:
                    rxMessageQueue.push (cmd);
                    processed = true;
                    break;
                case DISCONNECT:
                    goto CLOSE_TX_LOOP;  // break out of loop
                }
            }
        }
        if (processed)
        {
            continue;
        }

        if (route_id == 0)
        {
            if (hasBroker)
            {
                try
                {
                    brokerConnection->send (cmd.packetize ());
                }
                catch (const boost::system::system_error &se)
                {
                    if (se.code () != boost::asio::error::connection_aborted)
                    {
                        if (!isDisconnectCommand (cmd))
                        {
                            logError (std::string ("broker send 0 ") + actionMessageType (cmd.action ()) + ':' +
                                      se.what ());
                        }
                    }
                }

                // if (error)
                {
                    //     std::cerr << "transmit failure to broker " << error.message() << '\n';
                }
            }
        }
        else if (route_id == -1)
        {  // send to rx thread loop
            rxMessageQueue.push (cmd);
        }
        else
        {
            //  txlist.push_back(cmd);
            auto rt_find = routes.find (route_id);
            if (rt_find != routes.end ())
            {
                try
                {
                    rt_find->second->send (cmd.packetize ());
                }
                catch (const boost::system::system_error &se)
                {
                    if (se.code () != boost::asio::error::connection_aborted)
                    {
                        if (!isDisconnectCommand (cmd))
                        {
                            logError(std::string("rt send ")+std::to_string(route_id)+"::"+se.what ());
                        }
                    }
                }
            }
            else
            {
                if (hasBroker)
                {
                    try
                    {
                        brokerConnection->send (cmd.packetize ());
                    }
                    catch (const boost::system::system_error &se)
                    {
                        if (se.code () != boost::asio::error::connection_aborted)
                        {
                            if (!isDisconnectCommand (cmd))
                            {
                                logError(std::string("broker send")+std::to_string(route_id)+ " ::" + se.what ());
                            }
                        }
                    }
                }
                else
                {
                    assert (false);
                }
            }
        }
    }
CLOSE_TX_LOOP:
    for (auto &rt : routes)
    {
        rt.second->close ();
    }
    routes.clear ();
    if (getRxStatus () == connection_status::connected)
    {
        closeReceiver ();
    }
    setTxStatus (connection_status::terminated);
}

void TcpComms::closeReceiver ()
{
    ActionMessage cmd (CMD_PROTOCOL);
    cmd.messageID = CLOSE_RECEIVER;
    rxMessageQueue.push (cmd);
}

std::string TcpComms::getAddress () const { return makePortAddress (localTarget_, PortNumber); }

}  // namespace tcp
}  // namespace helics
