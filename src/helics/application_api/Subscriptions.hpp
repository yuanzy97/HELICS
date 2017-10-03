/*

Copyright (C) 2017, Battelle Memorial Institute
All rights reserved.

This software was co-developed by Pacific Northwest National Laboratory, operated by the Battelle Memorial
Institute; the National Renewable Energy Laboratory, operated by the Alliance for Sustainable Energy, LLC; and the
Lawrence Livermore National Laboratory, operated by Lawrence Livermore National Security, LLC.

*/
#ifndef _HELICS_SUBSCRIPTION_H_
#define _HELICS_SUBSCRIPTION_H_
#pragma once

#include "HelicsPrimaryTypes.h"
#include "ValueFederate.h"
#include "helicsTypes.hpp"
#include <algorithm>
#include <array>
#include <boost/lexical_cast.hpp>
namespace helics
{
void valueExtract (const defV &dv, std::string &val);

void valueExtract (const defV &dv, std::complex<double> &val);

void valueExtract (const defV &dv, std::vector<double> &val);

void valueExtract (const data_view &dv, helicsType_t baseType, std::string &val);

void valueExtract (const data_view &dv, helicsType_t baseType, std::vector<double> &val);

void valueExtract (const data_view &dv, helicsType_t baseType, std::complex<double> &val);

/** assume it is some numeric type (int or double)*/
template <class X>
void valueExtract (const defV &dv, X &val)
{
    switch (dv.which ())
    {
    case doubleLoc:  // double
        val = static_cast<X> (boost::get<double> (dv));
        break;
    case intLoc:  // int64_t
        val = static_cast<X> (boost::get<int64_t> (dv));
        break;
    case stringLoc:  // string
    default:
        val = boost::lexical_cast<X> (boost::get<std::string> (dv));
        break;
    case complexLoc:  // complex
        val = static_cast<X> (std::abs (boost::get<std::complex<double>> (dv)));
        break;
    case vectorLoc:  // vector
    {
        auto &vec = boost::get<std::vector<double>> (dv);
        if (!vec.empty ())
        {
            val = static_cast<X> (vec.front ());
        }
        else
        {
            val = std::numeric_limits<X>::min ();
        }
        break;
    }
    }
}

/** assume it is some numeric type (int or double)*/
template <class X>
void valueExtract (const data_view &dv, helicsType_t baseType, X &val)
{
    switch (baseType)
    {
    case helicsType_t::helicsString:
    {
        val = static_cast<X>(boost::lexical_cast<double> (dv.string ()));
        break;
    }
    case helicsType_t::helicsDouble:
    {
        auto V = ValueConverter<double>::interpret (dv);
        val = static_cast<X> (V);
        break;
    }
    case helicsType_t::helicsInt:
    {
        auto V = ValueConverter<int64_t>::interpret (dv);
        val = static_cast<X> (V);
        break;
    }

    case helicsType_t::helicsVector:
    {
        auto V = ValueConverter<std::vector<double>>::interpret (dv);
        if (!V.empty ())
        {
            val = static_cast<X> (V[0]);
        }
        else
        {
            val = 0.0;
        }
        break;
    }
    case helicsType_t::helicsComplex:
    {
        auto V = ValueConverter<std::complex<double>>::interpret (dv);
        val = static_cast<X> (std::abs (V));
        break;
    }
    case helicsType_t::helicsComplexVector:
    {
        auto V = ValueConverter<std::vector<std::complex<double>>>::interpret (dv);
        if (!V.empty ())
        {
            val = static_cast<X> (std::abs (V.front ()));
        }
        else
        {
            val = 0.0;
        }
        break;
    }
    case helicsType_t::helicsInvalid:
        break;
    }
}
class SubscriptionBase
{
protected:
	ValueFederate *fed = nullptr;  //!< reference to the value federate
	std::string key_;  //!< the name of the subscription
	std::string type_; //!< the requested type of the subscription
	std::string units_;  //!< the defined units of the subscription
	subscription_id_t id;  //!< the id of the federate
public:
	SubscriptionBase() = default;

	SubscriptionBase(ValueFederate *valueFed, const std::string &key, const std::string &type="def",const std::string &units = "")
		: fed(valueFed), key_(key), type_(type), units_(units)
	{
		id = fed->registerRequiredSubscription(key_, type_, units_);
	}

	SubscriptionBase(bool required, ValueFederate *valueFed, const std::string &key, const std::string &type="def", const std::string &units = "")
		: fed(valueFed), key_(key), type_(type), units_(units)
	{
		if (required)
		{
			id = fed->registerRequiredSubscription(key_, type_, units_);
		}
		else
		{
			id = fed->registerOptionalSubscription(key_, type_, units_);
		}
	}
	virtual ~SubscriptionBase() = default;
	/** get the time of the last update
	@return the time of the last update
	*/
	Time getLastUpdate() const { return fed->getLastUpdateTime(id); }
	/** check if the value has subscription has been updated*/
	bool isUpdated() const { return fed->isUpdated(id); }
	subscription_id_t getID() const { return id; }

	/** register a callback for an update notification
	@details the callback is called in the just before the time request function returns
	@param[in] callback a function with signature void( Time time)
	time is the time the value was updated  This callback is a notification callback and doesn't return the value
	*/
	void registerCallback(std::function<void(Time)> callback)
	{
		fed->registerSubscriptionNotificationCallback(id, [=](subscription_id_t, Time time) { callback(time); });
	}
	/** get the key for the subscription*/
	const std::string &getKey() const
	{
		return key_;
	}
	/** get the key for the subscription*/
	std::string getType() const
	{
		return fed->getPublicationType(id);
	}
	const std::string &getUnits() const
	{
		return units_;
	}
};
// template<class X, typename std::enable_if<helicsType<X>() != helicsType_t::helicsInvalid, bool>::type>
class Subscription: public SubscriptionBase
{
  public:
    boost::variant<std::function<void(const std::string &, Time)>,
                   std::function<void(const double &, Time)>,
                   std::function<void(const int64_t &, Time)>,
                   std::function<void(const std::complex<double> &, Time)>,
                   std::function<void(const std::vector<double> &, Time)>>
      value_callback;  //!< callback function for the federate
    
    mutable helicsType_t type = helicsType_t::helicsInvalid;  //!< the underlying type the publication is using
    defV lastValue;  //!< the last value updated
  public:
    Subscription (ValueFederate *valueFed, const std::string &key, const std::string &units = ""):SubscriptionBase(valueFed,key,"def",units)
    {

    }

    Subscription (bool required,ValueFederate *valueFed, const std::string &key, const std::string &units = "") :SubscriptionBase(required,valueFed, key, "def", units)
    {

    }

    /** store the value in the given variable
    @param[out] out the location to store the value
    */
    template <class X>
    typename std::enable_if<helicsType<X> () != helicsType_t::helicsInvalid, void>::type getValue (X &out)
    {
        if (fed->isUpdated (id))
        {
            auto dv = fed->getValueRaw (id);
            if (type == helicsType_t::helicsInvalid)
            {
                type = getTypeFromString (fed->getPublicationType (id));
            }
            valueExtract (dv, type, out);
            lastValue = out;
        }
        else
        {
            valueExtract (lastValue, out);
        }
    }
    /** get the most recent value
    @return the value*/
    template <class X>
    typename std::enable_if<helicsType<X> () != helicsType_t::helicsInvalid, X>::type getValue ()
    {
        X val;
        getValue (val);
        return val;
    }

	using SubscriptionBase::registerCallback;
    /** register a callback for the update
    @details the callback is called in the just before the time request function returns
    @param[in] callback a function with signature void(X val, Time time)
    val is the new value and time is the time the value was updated
    */
    template <class X>
    typename std::enable_if<helicsType<X> () != helicsType_t::helicsInvalid, void>::type
    registerCallback (std::function<void(const X &, Time)> callback)
    {
        value_callback = callback;
        fed->registerSubscriptionNotificationCallback (id, [=](subscription_id_t, Time time) {
            handleCallback (time);
        });
    }
   
    template <class X>
    typename std::enable_if<helicsType<X> () != helicsType_t::helicsInvalid, void>::type setDefault (const X &val)
    {
        lastValue = val;
    }

  private:
    void handleCallback (Time time);
};

/** class to handle a subscription
@tparam X the class of the value associated with a subscription*/
template <class X>
class SubscriptionT:public SubscriptionBase
{
  private:
    std::function<void(X, Time)> value_callback;  //!< callback function for the federate
  public:
	  SubscriptionT() = default;
    /**constructor to build a subscription object
    @param[in] valueFed  the ValueFederate to use
    @param[in] name the name of the subscription
    @param[in] units the units associated with a Federate
    */
	SubscriptionT(ValueFederate *valueFed, const std::string &name, const std::string &units = "")
		: SubscriptionBase(valueFed, name, ValueConverter<X>::type(), units)
    {
        
    }
    /**constructor to build a subscription object
    @param[in] required a flag indicating that the subscription is required to have a matching publication
    @param[in] valueFed  the ValueFederate to use
    @param[in] name the name of the subscription
    @param[in] units the units associated with a Federate
    */
    SubscriptionT (bool required, ValueFederate *valueFed, const std::string &name, const std::string &units = "")
		: SubscriptionBase(required,valueFed, name, ValueConverter<X>::type(), units)
    {
       
    }

    /** get the most recent value
    @return the value*/
    X getValue () const { return fed->getValue<X> (id); }
    /** store the value in the given variable
    @param[out] out the location to store the value
    */
    void getValue (X &out) const { fed->getValue (id, out); }

	using SubscriptionBase::registerCallback;
    /** register a callback for the update
    @details the callback is called in the just before the time request function returns
    @param[in] callback a function with signature void(X val, Time time)
    val is the new value and time is the time the value was updated
    */
    void registerCallback (std::function<void(X, Time)> callback)
    {
        value_callback = callback;
        fed->registerSubscriptionNotificationCallback (id, [=](subscription_id_t, Time time) {
            handleCallback (time);
        });
    }

  private:
    void handleCallback (Time time)
    {
        X out;
        fed->getValue (id, out);
        value_callback (out, time);
    }
};

/** class to handle a Vector Subscription
@tparam X the class of the value associated with the vector subscription*/
template <class X>
class VectorSubscription
{
  private:
    ValueFederate *fed = nullptr;  //!< reference to the value federate
    std::string m_name;  //!< the name of the subscription
    std::string m_units;  //!< the defined units of the federate
    std::vector<subscription_id_t> ids;  //!< the id of the federate
    std::function<void(int, Time)> update_callback;  //!< callback function for when a value is updated
    std::vector<X> vals;  //!< storage for the values
  public:
    VectorSubscription () noexcept {};

    /**constructor to build a subscription object
    @param[in] required a flag indicating that the subscription is required to have a matching publication
    @param[in] valueFed  the ValueFederate to use
    @param[in] name the name of the subscription
    @param[in] units the units associated with a Federate
    */
    VectorSubscription (bool required,
                        ValueFederate *valueFed,
                        std::string name,
                        int startIndex,
                        int count,
                        const X &defValue,
                        const std::string &units = "")
        : fed (valueFed), m_name (std::move (name)), m_units (units)
    {
        ids.reserve (count);
        vals.resize (count, defValue);
        if (required)
        {
            for (auto ind = startIndex; ind < startIndex + count; ++ind)
            {
                auto id = fed->registerRequiredSubscription<X> (m_name, ind, m_units);
                ids.push_back (id);
            }
        }
        else
        {
            for (auto ind = startIndex; ind < startIndex + count; ++ind)
            {
                auto id = fed->registerOptionalSubscription<X> (m_name, ind, m_units);
                ids.push_back (id);
            }
        }
        fed->registerSubscriptionNotificationCallback (ids, [this](subscription_id_t id, Time tm) {
            handleCallback (id, tm);
        });
    }
    /**constructor to build a subscription object
    @param[in] valueFed  the ValueFederate to use
    @param[in] name the name of the subscription
    @param[in] units the units associated with a Federate
    */
    VectorSubscription (ValueFederate *valueFed,
                        std::string name,
                        int startIndex,
                        int count,
                        const X &defValue,
                        std::string units = "")
        : VectorSubscription (false, valueFed, name, startIndex, count, defValue, units)
    {
    }
    /** move constructor*/
    VectorSubscription (VectorSubscription &&vs) noexcept
        : fed (vs.fed), m_name (std::move (vs.m_name)), m_units (std::move (vs.m_units)), ids (std::move (vs.ids)),
          update_callback (std::move (vs.update_callback)), vals (std::move (vs.vals))
    {
        // need to transfer the callback to the new object
        fed->registerSubscriptionNotificationCallback (ids, [this](subscription_id_t id, Time tm) {
            handleCallback (id, tm);
        });
    };
    /** move assignment*/
    VectorSubscription &operator= (VectorSubscription &&vs) noexcept
    {
        fed = vs.fed;
        m_name = std::move (vs.m_name);
        m_units = std::move (vs.m_units);
        ids = std::move (vs.ids);
        update_callback = std::move (vs.update_callback);
        vals = std::move (vs.vals);
        // need to transfer the callback to the new object
        fed->registerSubscriptionNotificationCallback (ids, [this](subscription_id_t id, Time tm) {
            handleCallback (id, tm);
        });
        return *this;
    }
    /** get the most recent value
    @return the value*/
    const std::vector<X> &getVals () const { return vals; }
    /** store the value in the given variable
    @param[out] out the location to store the value
    */
    const X &operator[] (int index) const { return vals[index]; }
    /** register a callback for the update
    @details the callback is called in the just before the time request function returns
    @param[in] callback a function with signature void(X val, Time time)
    val is the new value and time is the time the value was updated
    */
    void registerCallback (std::function<void(int, Time)> callback) { update_callback = std::move (callback); }

  private:
    void handleCallback (subscription_id_t id, Time time)
    {
        X out;
        auto res = std::lower_bound (ids.begin (), ids.end (), id);
        int index = static_cast<int> (res - ids.begin ());
        fed->getValue (ids[index], out);
        vals[index] = out;
        if (update_callback)
        {
            update_callback (index, time);
        }
    }
};

/** class to handle a Vector Subscription
@tparam X the class of the value associated with the vector subscription*/
template <class X>
class VectorSubscription2d
{
  private:
    ValueFederate *fed = nullptr;  //!< reference to the value federate
    std::string m_name;  //!< the name of the subscription
    std::string m_units;  //!< the defined units of the federate
    std::vector<subscription_id_t> ids;  //!< the id of the federate
    std::function<void(int, Time)> update_callback;  //!< callback function for when a value is updated
    std::vector<X> vals;  //!< storage for the values
    std::array<int, 4> indices;  //!< storage for the indices and start values
  public:
    VectorSubscription2d () noexcept {};

    /**constructor to build a subscription object
    @param[in] required a flag indicating that the subscription is required to have a matching publication
    @param[in] valueFed  the ValueFederate to use
    @param[in] name the name of the subscription
    @param[in] units the units associated with a Federate
    */
    VectorSubscription2d (bool required,
                          ValueFederate *valueFed,
                          std::string name,
                          int startIndex_x,
                          int count_x,
                          int startIndex_y,
                          int count_y,
                          const X &defValue,
                          const std::string &units = "")
        : fed (valueFed), m_name (std::move (name)), m_units (units)
    {
        ids.reserve (count_x * count_y);
        vals.resize (count_x * count_y, defValue);
        if (required)
        {
            for (auto ind_x = startIndex_x; ind_x < startIndex_x + count_x; ++ind_x)
            {
                for (auto ind_y = startIndex_y; ind_y < startIndex_y + count_y; ++ind_y)
                {
                    auto id = fed->registerRequiredSubscriptionIndexed<X> (m_name, ind_x, ind_y, m_units);
                    ids.push_back (id);
                }
            }
        }
        else
        {
            for (auto ind_x = startIndex_x; ind_x < startIndex_x + count_x; ++ind_x)
            {
                for (auto ind_y = startIndex_y; ind_y < startIndex_y + count_y; ++ind_y)
                {
                    auto id = fed->registerOptionalSubscriptionIndexed<X> (m_name, ind_x, ind_y, m_units);
                    ids.push_back (id);
                }
            }
        }
        indices[0] = startIndex_x;
        indices[1] = count_x;
        indices[2] = startIndex_y;
        indices[3] = count_y;
        fed->registerSubscriptionNotificationCallback (ids, [this](subscription_id_t id, Time tm) {
            handleCallback (id, tm);
        });
    }

    /**constructor to build a subscription object
    @param[in] valueFed  the ValueFederate to use
    @param[in] name the name of the subscription
    @param[in] units the units associated with a Federate
    */
    VectorSubscription2d (ValueFederate *valueFed,
                          const std::string &name,
                          int startIndex_x,
                          int count_x,
                          int startIndex_y,
                          int count_y,
                          const X &defValue,
                          const std::string &units = "")
        : VectorSubscription2d (false,
                                valueFed,
                                name,
                                startIndex_x,
                                count_x,
                                startIndex_y,
                                count_y,
                                defValue,
                                units)
    {
    }
    /** move constructor*/
    VectorSubscription2d (VectorSubscription2d &&vs) noexcept
        : fed (vs.fed), m_name (std::move (vs.m_name)), m_units (std::move (vs.m_units)), ids (std::move (vs.ids)),
          update_callback (std::move (vs.update_callback)), vals (std::move (vs.vals))
    {
        indices = vs.indices;
        // need to transfer the callback to the new object
        fed->registerSubscriptionNotificationCallback (ids, [this](subscription_id_t id, Time tm) {
            handleCallback (id, tm);
        });
    };

    /** move assignment*/
    VectorSubscription2d &operator= (VectorSubscription2d &&vs) noexcept
    {
        fed = vs.fed;
        m_name = std::move (vs.m_name);
        m_units = std::move (vs.m_units);
        ids = std::move (vs.ids);
        update_callback = std::move (vs.update_callback);
        vals = std::move (vs.vals);
        // need to transfer the callback to the new object
        fed->registerSubscriptionNotificationCallback (ids, [this](subscription_id_t id, Time tm) {
            handleCallback (id, tm);
        });
        indices = vs.indices;
        return *this;
    }
    /** get the most recent value
    @return the value*/
    const std::vector<X> &getVals () const { return vals; }
    /** get the value in the given variable
    @param[out] out the location to store the value
    */
    const X &operator[] (int index) const { return vals[index]; }

    /** get the value in the given variable
    @param[out] out the location to store the value
    */
    const X &at (int index_x, int index_y) const
    {
        return vals[(index_x - indices[0]) * indices[3] + (index_y - indices[2])];
    }
    /** register a callback for the update
    @details the callback is called in the just before the time request function returns
    @param[in] callback a function with signature void(X val, Time time)
    val is the new value and time is the time the value was updated
    */
    void registerCallback (std::function<void(int, Time)> callback) { update_callback = std::move (callback); }

  private:
    void handleCallback (subscription_id_t id, Time time)
    {
        auto res = std::lower_bound (ids.begin (), ids.end (), id);
        int index = static_cast<int> (res - ids.begin ());
        fed->getValue (ids[index], vals[index]);
        if (update_callback)
        {
            update_callback (index, time);
        }
    }
};

}  // namespace helics
#endif
