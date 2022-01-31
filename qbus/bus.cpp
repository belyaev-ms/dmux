#include "qbus/bus.h"
#include "qbus/common.h"
#include <boost/lexical_cast.hpp>
#include <boost/interprocess/detail/atomic.hpp>

namespace qbus
{

namespace bus
{

//==============================================================================
//  base_bus
//==============================================================================
/**
 * Constructor
 * @param name the name of the bus
 */
base_bus::base_bus(const std::string& name) :
    m_name(name),
    m_opened(false)
{
}

/**
 * Destructor
 */
//virtual
base_bus::~base_bus()
{
    close_bus();
}

/**
 * Get the name of the bus
 * @return the name of the bus
 */
const std::string& base_bus::name() const
{
    return m_name;
}

/**
 * Create the bus
 * @param spec the specification of the bus
 * @return the result of the creating
 */
bool base_bus::create(const specification_type& spec)
{
    if (!m_opened)
    {
        m_opened = do_create(spec);
        return m_opened;
    }
    return false;
}

/**
 * Open the bus
 * @return the result of the opening
 */
bool base_bus::open()
{
    if (!m_opened)
    {
        m_opened = do_open();
        return m_opened;
    }
    return false;
}

/**
 * Check if the bus is enabled
 * @return result of the checking
 */
bool base_bus::enabled() const
{
    return m_opened;
}

/**
 * Make new connector
 * @param id the identifier of the connector
 * @return new connector
 */
pconnector_type base_bus::make_connector(const id_type id) const
{
    pconnector_type pconnector = make_connector(m_name + boost::lexical_cast<std::string>(id));
    if (pconnector->open())
    {
        return pconnector;
    }
    const specification_type sp = spec();
    if (m_pconnectors.empty() || sp.capacity_factor > 0)
    {
        size_type old_capacity = !m_pconnectors.empty() ? output_connector()->capacity() : 0;
        size_type new_capacity = std::max(sp.min_capacity, old_capacity * (sp.capacity_factor + 100) / 100);
        new_capacity = std::min(new_capacity, sp.max_capacity);
        if (new_capacity > old_capacity && pconnector->create(sp.id, new_capacity))
        {
            return pconnector;
        }
    }
    return NULL;
}

/**
 * Add new connector to the bus
 * @return result of the adding
 */
//virtual
bool base_bus::add_connector() const
{
    controlblock_type& cb = get_controlblock();
    if (cb.output_id + 1 != cb.input_id)
    {
        pconnector_type pconnector = make_connector(cb.output_id + 1);
        if (pconnector)
        {
            m_pconnectors.push_front(pconnector);
            ++cb.output_id;
            return true;
        }
    }
    return false;
}

/**
 * Remove the back connector from the bus
 * @return result of the removing
 */
//virtual
bool base_bus::remove_connector() const
{
    controlblock_type& cb = get_controlblock();
    if (cb.input_id != cb.output_id)
    {
        m_pconnectors.pop_back();
        ++cb.input_id;
        if (m_pconnectors.empty())
        {
            pconnector_type pconnector = make_connector(cb.input_id);
            if (!pconnector)
            {
                return false;
            }
            m_pconnectors.push_back(pconnector);
        }
        return true;
    }
    return false;
}

/**
 * Get the output connector
 * @return the output connector
 */
pconnector_type base_bus::output_connector() const
{
    return m_pconnectors.front();
}

/**
 * Get the input connector
 * @return the input connector
 */
pconnector_type base_bus::input_connector() const
{
    return m_pconnectors.back();
}

/**
 * Get the specification of the bus
 * @return the specification of the bus
 */
const specification_type& base_bus::spec() const
{
    return get_spec();
}

/**
 * Push data to the bus
 * @param tag the tag of the data
 * @param data the data
 * @param size the size of the data
 * @return result of the pushing
 */
bool base_bus::push(const tag_type tag, const void *data, const size_t size)
{
    if (m_opened)
    {
        while (!do_push(tag, data, size))
        {
            if (!add_connector())
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

/**
 * Push data to the bus
 * @param tag the tag of the data
 * @param data the data
 * @param size the size of the data
 * @param timeout the allowable timeout of the pushing
 * @return result of the pushing
 */
bool base_bus::push(const tag_type tag, const void *data, const size_t size,
    const struct timespec& timeout)
{
    if (m_opened)
    {
        bool result = do_timed_push(tag, data, size, timeout);
        if (!result)
        {
            const struct timespec limit = get_monotonic_time() + timeout;
            do
            {
                if (!add_connector())
                {
                    return false;
                }
                const struct timespec now = get_monotonic_time();
                if (now >= limit)
                {
                    return false;
                }
                result = do_timed_push(tag, data, size, limit - now);
            } while (!result);
        }
        return true;
    }
    return false;
}

/**
 * Get the next message from the bus
 * @return the message
 */
const pmessage_type base_bus::get() const
{
    if (m_opened)
    {
        pmessage_type pmessage = do_get();
        while (!pmessage)
        {
            if (!remove_connector())
            {
                return pmessage_type();
            }
            pmessage = do_get();
        }
        return pmessage;
    }
    return pmessage_type();
}

/**
 * Get the next message from the bus
 * @param timeout the allowable timeout of the getting
 * @return the message
 */
const pmessage_type base_bus::get(const struct timespec& timeout) const
{
    if (m_opened)
    {
        pmessage_type pmessage = do_timed_get(timeout);
        if (!pmessage)
        {
            const struct timespec limit = get_monotonic_time() + timeout;
            do
            {
                if (!remove_connector())
                {
                    return pmessage_type();
                }
                const struct timespec now = get_monotonic_time();
                if (now >= limit)
                {
                    return pmessage_type();
                }
                pmessage = do_timed_get(limit - now);
            } while (!pmessage);
        }
        return pmessage;
    }
    return pmessage_type();;
}

/**
 * Remove the next message from the bus
 * @return the result of the removing
 */
bool base_bus::pop()
{
    if (m_opened)
    {
        while (!do_pop())
        {
            if (!remove_connector())
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

/**
 * Remove the next message from the bus
 * @param timeout the allowable timeout of the removing
 * @return the result of the removing
 */
bool base_bus::pop(const struct timespec& timeout)
{
    if (m_opened)
    {
        bool result = do_timed_pop(timeout);
        if (!result)
        {
            const struct timespec limit = get_monotonic_time() + timeout;
            do
            {
                if (!remove_connector())
                {
                    return false;
                }
                const struct timespec now = get_monotonic_time();
                if (now >= limit)
                {
                    return false;
                }
                result = do_timed_pop(limit - now);
            } while (!result);
        }
        return true;
    }
    return false;
}

/**
 * Push data to the bus
 * @param tag the tag of the data
 * @param data the data
 * @param size the size of the data
 * @return result of the pushing
 */
//virtual
bool base_bus::do_push(const tag_type tag, const void *data, const size_t size)
{
    return output_connector()->push(tag, data, size);
}

/**
 * Push data to the bus
 * @param tag the tag of the data
 * @param data the data
 * @param size the size of the data
 * @param timeout the allowable timeout of the pushing
 * @return result of the pushing
 */
//virtual
bool base_bus::do_timed_push(const tag_type tag, const void *data, const size_t size,
    const struct timespec& timeout)
{
    return output_connector()->push(tag, data, size, timeout);
}

/**
 * Get the next message from the bus
 * @return the message
 */
//virtual
const pmessage_type base_bus::do_get() const
{
    return input_connector()->get();
}

/**
 * Get the next message from the bus
 * @param timeout the allowable timeout of the getting
 * @return the message
 */
//virtual
const pmessage_type base_bus::do_timed_get(const struct timespec& timeout) const
{
    return input_connector()->get(timeout);
}

/**
 * Remove the next message from the bus
 * @return the result of the removing
 */
//virtual
bool base_bus::do_pop()
{
    return input_connector()->pop();
}

/**
 * Remove the next message from the bus
 * @param timeout the allowable timeout of the removing
 * @return the result of the removing
 */
//virtual
bool base_bus::do_timed_pop(const struct timespec& timeout)
{
    return input_connector()->pop(timeout);
}

/**
 * Create the bus
 * @param spec the specification of the bus
 * @return the result of the creating
 */
//virtual
bool base_bus::do_create(const specification_type& spec)
{
    pconnector_type pconnector = make_connector(0);
    if (pconnector)
    {
        m_pconnectors.push_front(pconnector);
        return true;
    }
    return false;
}

/**
 * Open the bus
 * @return the result of the opening
 */
//virtual
bool base_bus::do_open()
{
    const controlblock_type& cb = get_controlblock();
    id_type id = cb.input_id;
    do
    {
        pconnector_type pconnector = make_connector(id);
        if (!pconnector)
        {
            m_pconnectors.clear();
            return false;
        }
        m_pconnectors.push_front(pconnector);
    } while (id++ != cb.output_id);
    return true;
}

/**
 * Close the bus
 */
void base_bus::close_bus()
{
    m_pconnectors.clear();
}

//==============================================================================
//  shared_bus
//==============================================================================
/**
 * Constructor
 * @param name the name of the bus
 */
shared_bus::shared_bus(const std::string& name) :
    base_bus(name),
    m_status(US_NONE)
{
}

/**
 * Create the shared memory
 * @return the result of the creating
 */
bool shared_bus::create_memory()
{
    m_pmemory = boost::make_shared<shared_memory_type>(name());
    return m_pmemory->create(memory_size());
}

/**
 * Open the shared memory
 * @return the result of the opening
 */
bool shared_bus::open_memory()
{
    m_pmemory = boost::make_shared<shared_memory_type>(name());
    return m_pmemory->open();
}

/**
 * Free the shared memory
 */
void shared_bus::free_memory()
{
    m_pmemory.reset();
}

/**
 * Get the size of the shared memory
 * @return the size of the shared memory
 */
//virtual
size_t shared_bus::memory_size() const
{
    return sizeof(bus_body);
}

/**
 * Get the pointer to the shared memory
 * @return the pointer to the shared memory
 */
//virtual
void *shared_bus::get_memory() const
{
    return m_pmemory->get();
}

/**
 * Create the bus
 * @param spec the specification of the bus
 * @return the result of the creating
 */
//virtual
bool shared_bus::do_create(const specification_type& spec)
{
    return create_memory() && create_body(spec);
}

/**
 * Create the body of the bus
 * @param spec the specification of the bus
 * @return the result of the creating
 */
bool shared_bus::create_body(const specification_type& spec)
{
    bus_body *pbody = reinterpret_cast<bus_body*>(get_memory());
    pbody->spec = spec;
    pbody->controlblock.epoch = 0;
    pbody->controlblock.output_id = 0;
    pbody->controlblock.input_id = 0;
    m_controlblock = pbody->controlblock;
    if (base_type::do_create(spec))
    {
        return true;
    }
    free_memory();
    return false;
}

/**
 * Open the bus
 * @return the result of the opening
 */
//virtual
bool shared_bus::do_open()
{
    return open_memory() && attach_body();
}

/**
 * Attach the body of the bus
 * @return the result of the attaching
 */
bool shared_bus::attach_body()
{
    if (base_type::do_open())
    {
        m_controlblock = get_controlblock();
        return true;
    }
    free_memory();
    return false;
}

/**
 * Push data to the bus
 * @param tag the tag of the data
 * @param data the data
 * @param size the size of the data
 * @return result of the pushing
 */
//virtual
bool shared_bus::do_push(const tag_type tag, const void *data, const size_t size)
{
    if (update_output_connector())
    {
        do
        {
            if (!base_type::do_push(tag, data, size))
            {
                return false;
            }
        } while (!update_output_connector());
        return true;
    }
    return base_type::do_push(tag, data, size);
}

/**
 * Push data to the bus
 * @param tag the tag of the data
 * @param data the data
 * @param size the size of the data
 * @param timeout the allowable timeout of the pushing
 * @return result of the pushing
 */
//virtual
bool shared_bus::do_timed_push(const tag_type tag, const void *data, const size_t size,
    const struct timespec& timeout)
{
    return base_type::do_timed_push(tag, data, size, timeout);
}

/**
 * Get the next message from the bus
 * @return the message
 */
//virtual
const pmessage_type shared_bus::do_get() const
{
    return base_type::do_get();
}

/**
 * Get the next message from the bus
 * @param timeout the allowable timeout of the getting
 * @return the message
 */
//virtual
const pmessage_type shared_bus::do_timed_get(const struct timespec& timeout) const
{
    return base_type::do_timed_get(timeout);
}

/**
 * Remove the next message from the bus
 * @return the result of the removing
 */
//virtual
bool shared_bus::do_pop()
{
    return base_type::do_pop();
}

/**
 * Remove the next message from the bus
 * @param timeout the allowable timeout of the removing
 * @return the result of the removing
 */
//virtual
bool shared_bus::do_timed_pop(const struct timespec& timeout)
{
    return base_type::do_timed_pop(timeout);
}

/**
 * Get the specification of the bus
 * @return the specification of the bus
 */
//virtual
const specification_type& shared_bus::get_spec() const
{
    return reinterpret_cast<bus_body*>(get_memory())->spec;
}

/**
 * Get the control block of the bus
 * @return the control block of the bus
 */
//virtual
controlblock_type& shared_bus::get_controlblock() const
{
    return m_status != US_NONE ? m_controlblock :
        reinterpret_cast<bus_body*>(get_memory())->controlblock;
}

/**
 * Check if the bus is updated
 * @return result of this checking
 */
bool shared_bus::is_updated() const
{
    controlblock_type& cb = get_controlblock();
    const uint32_t epoch = boost::interprocess::ipcdetail::atomic_read32(&cb.epoch);
    if (epoch != m_controlblock.epoch)
    {
        m_controlblock.epoch = epoch;
        return false;
    }
    return true;
}

/**
 * Update the connectors
 * @return status of update
 */
shared_bus::update_status shared_bus::update_connectors() const
{
    if (!is_updated())
    {
        rollback<update_status> st(m_status);
        controlblock_type& cb = get_controlblock();
        const id_type output_id = boost::interprocess::ipcdetail::atomic_read32(&cb.output_id);
        if (output_id != m_controlblock.output_id)
        {
            while (m_controlblock.output_id != output_id)
            {
                add_connector();
            }
            m_status = US_OUTPUT;
        }
        const id_type input_id = boost::interprocess::ipcdetail::atomic_read32(&cb.input_id);
        if (input_id != m_controlblock.output_id)
        {
            while (m_controlblock.output_id != output_id)
            {
                remove_connector();
            }
            m_status = US_OUTPUT == m_status ? US_BOTH : US_INPUT;
        }
        return m_status;
    }
    return US_NONE;
}

/**
 * Update the input connector
 * @return false if the bus has the latest input connectors
 */
bool shared_bus::update_input_connector() const
{
    return update_connectors() & US_INPUT;
}

/**
 * Update the output connector
 * @return false if the bus has the latest output connectors
 */
bool shared_bus::update_output_connector() const
{
    return update_connectors() & US_OUTPUT;
}

} //namespace bus

} //namespace qbus
