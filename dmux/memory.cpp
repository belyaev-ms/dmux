#include "dmux/memory.h"
#include <boost/make_shared.hpp>

namespace dmux
{
namespace memory
{

//==============================================================================
//  shared_memory
//==============================================================================
/**
 * Constructor
 * @param name the name of the memory
 */
shared_memory::shared_memory(const std::string& name) :
    m_name(name)
{
}

/**
 * Destructor
 */
//virtual
shared_memory::~shared_memory()
{
    remove();
}

/**
 * Create the memory
 * @param sz the size of the memory
 * @return the result of the creating
 */
const bool shared_memory::create(const size_t sz)
{
    using namespace boost::interprocess;
    if (!m_pmemory)
    {
        try
        {
            pmemory_type pmemory = boost::make_shared<memory_type>(create_only,
                    m_name.c_str(), read_write);
            pmemory->truncate(sz);
            m_pregion = boost::make_shared<region_type>(*pmemory, read_write);
            m_pmemory = pmemory;
            return true;
        }
        catch (...)
        {
            remove();
        }
    }
    return false;
}

/**
 * Open the memory
 */
const bool shared_memory::open()
{
    using namespace boost::interprocess;
    if (!m_pmemory)
    {
        try
        {
            pmemory_type pmemory = boost::make_shared<memory_type>(open_only,
                m_name.c_str(), read_write);
            m_pregion = boost::make_shared<region_type>(*pmemory, read_write);
            m_pmemory = pmemory;
            return true;
        }
        catch (...)
        {
            remove();
        }
    }
    return false;

}

/**
 * Get the pointer to the memory
 * @return the pointer to the memory
 */
void *shared_memory::get()
{
    return m_pregion ? m_pregion->get_address() : NULL;
}

/**
 * Get the size of the memory
 * @return the size of the memory
 */
const size_t shared_memory::size() const
{
    return m_pregion ? m_pregion->get_size() : 0;
}

/**
 * Remove the memory
 */
void shared_memory::remove()
{
    boost::interprocess::shared_memory_object::remove(m_name.c_str());
}

} //namespace memory
} //namespace dmux