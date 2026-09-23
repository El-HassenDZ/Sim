#ifndef ODR_HELPER_H
#define ODR_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/odr-routing-protocol.h"

#include <string>

namespace ns3
{

/**
 * @ingroup odr
 * @brief Installs ODR on nodes through InternetStackHelper and exports its counters.
 *
 * Typical use, from C++ or Python:
 * @code
 *   OdrHelper odr;
 *   odr.Set("ActiveRouteTimeout", TimeValue(Seconds(3)));
 *   InternetStackHelper stack;
 *   stack.SetRoutingHelper(odr);
 *   stack.Install(nodes);
 *   odr.AssignStreams(nodes, 0);
 *   Simulator::Run();
 *   OdrHelper::WriteStatistics(nodes, "odr-stats.csv");
 *   Simulator::Destroy();
 * @endcode
 */
class OdrHelper : public Ipv4RoutingHelper
{
  public:
    OdrHelper();

    /**
     * @brief Clone the helper, as InternetStackHelper keeps its own copy.
     * @return a heap-allocated copy owned by the caller
     */
    OdrHelper* Copy() const override;

    /**
     * @brief Create an ODR instance and aggregate it to the node.
     *
     * Aggregation is what lets the node initialize the protocol at simulation
     * start; InternetStackHelper then attaches it to the IPv4 stack.
     *
     * @param node the node the protocol will run on
     * @return the new routing protocol
     */
    Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const override;

    /**
     * @brief Set an attribute of the ODR instances created afterwards.
     * @param name attribute name, e.g. "ActiveRouteTimeout"
     * @param value attribute value
     */
    void Set(std::string name, const AttributeValue& value);

    /**
     * @brief Fix the random streams of the ODR instances on the given nodes.
     *
     * Without this, stream numbers depend on how many random variables were
     * created before, so adding an unrelated model to the scenario would
     * change the rebroadcast jitter and hence every result.
     *
     * @param nodes nodes with ODR installed
     * @param stream first stream index
     * @return the number of streams consumed
     */
    int64_t AssignStreams(NodeContainer nodes, int64_t stream);

    /**
     * @brief Find the ODR instance of a node, standalone or inside Ipv4ListRouting.
     * @param node the node
     * @return the protocol, or nullptr if ODR is not installed on the node
     */
    static Ptr<odr::RoutingProtocol> GetRoutingProtocol(Ptr<Node> node);

    /**
     * @brief Write one CSV row of ODR counters per node.
     *
     * Call it after Simulator::Run() and before Simulator::Destroy(), which
     * disposes the protocol instances. Columns: node identifier, every field
     * of odr::Statistics (the discovery latency sum in seconds), then the
     * number of data packets still parked for a route, needed to close the
     * end-of-run packet balance. Nodes without ODR are skipped.
     *
     * @param nodes nodes to export
     * @param filename output path; the file is overwritten
     */
    static void WriteStatistics(const NodeContainer& nodes, const std::string& filename);

  private:
    ObjectFactory m_agentFactory; //!< Factory of configured ODR instances
};

} // namespace ns3

#endif /* ODR_HELPER_H */
