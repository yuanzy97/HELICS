/*
Copyright (C) 2017-2018, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#include "../src/helics/cpp98/Broker.hpp"
#include "cpptestFixtures.hpp"
#include <cctype>
#include <boost/test/unit_test.hpp>

static bool hasIndexCode (const std::string &type_name)
{
    if (std::isdigit (type_name.back ()) != 0)
    {
        if (*(type_name.end () - 2) == '_')
        {  // this setup ignores the setup mode
            return true;
        }
    }
    return false;
}

static auto StartBrokerImp (const std::string &core_type_name, std::string initialization_string)
{
    if (core_type_name.compare(0, 3, "tcp") == 0)
    {
        initialization_string += " --reuse_address";
    }
    if (hasIndexCode (core_type_name))
    {
        std::string new_type (core_type_name.begin (), core_type_name.end () - 2);
        return std::make_shared<helicscpp::Broker> (new_type, std::string (), initialization_string);
    }
    return std::make_shared<helicscpp::Broker> (core_type_name, std::string (), initialization_string);
}

bool FederateTestFixture_cpp::hasIndexCode (const std::string &type_name)
{
    if (std::isdigit (type_name.back ()) != 0)
    {
        if (*(type_name.end () - 2) == '_')
        {  // this setup ignores the setup mode
            return true;
        }
    }
    return false;
}

int FederateTestFixture_cpp::getIndexCode (const std::string &type_name)
{
    return static_cast<int> (type_name.back () - '0');
}

auto FederateTestFixture_cpp::AddBrokerImp (const std::string &core_type_name,
                                            const std::string &initialization_string)
{
    return StartBrokerImp(core_type_name, initialization_string);
    }

FederateTestFixture_cpp::~FederateTestFixture_cpp ()
{
    for (auto &fed : federates)
    {
        fed->finalize ();
    }
    federates.clear ();
    for (auto &broker : brokers)
    {
        if (ctype.compare(0, 3, "tcp") == 0)
        {
            broker->waitForDisconnect(2000);
        }
        else
        {
            broker->waitForDisconnect(200);
        }

        if (broker->isConnected())
        {

        broker->disconnect ();
    }
    }
    brokers.clear ();
    helicsCleanupHelicsLibrary ();
}

std::shared_ptr<helicscpp::Broker> FederateTestFixture_cpp::AddBroker (const std::string &core_type_name, int count)
{
    return AddBroker (core_type_name, std::to_string (count));
}

std::shared_ptr<helicscpp::Broker>
FederateTestFixture_cpp::AddBroker (const std::string &core_type_name, const std::string &initialization_string)
{
    std::shared_ptr<helicscpp::Broker> broker;
    if (extraBrokerArgs.empty ())
    {
        broker = StartBrokerImp (core_type_name, initialization_string);
    }
    else
    {
        broker = StartBrokerImp (core_type_name, initialization_string + " " + extraBrokerArgs);
    }
    if (broker)
    {
        brokers.push_back(broker);
    }
    return broker;
}
