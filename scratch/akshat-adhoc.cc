/*
 * Modified Ad-Hoc Network Example
 *
 * This script sets up a mobile ad-hoc network to compare the performance
 * of AODV, DSDV, and OLSR using the FlowMonitor module.
 */
#include <random>
#include <vector>
 #include "ns3/core-module.h"
 #include "ns3/network-module.h"
 #include "ns3/internet-module.h"
 #include "ns3/wifi-module.h"
 #include "ns3/mobility-module.h"
 #include "ns3/applications-module.h"
 #include "ns3/energy-module.h"
 
 // Added modules for new routing protocols and monitoring
 #include "ns3/aodv-module.h"
 #include "ns3/dsdv-module.h"
 #include "ns3/olsr-module.h"
 #include "ns3/flow-monitor-module.h"
 
#include <iostream>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>
   #include "ns3/generic-battery-model-helper.h"
  #include "ns3/generic-battery-model.h"
  #include "ns3/energy-source-container.h"
  #include "ns3/wifi-radio-energy-model-helper.h"
  
  #include "ns3/energy-module.h"
  using namespace ns3;
  using namespace ns3::energy;
 using namespace ns3;
 
 NS_LOG_COMPONENT_DEFINE("AdHocNetworkComparison");
 
 // Enum to define the routing protocol choices
 enum RoutingProtocol
 {
   AODV,
   DSDV,
   OLSR
 };
 
 // Helper function to configure the routing protocol
 std::string ConfigureRoutingProtocol(NodeContainer &nodes, int protocolChoice)
 {
   InternetStackHelper internet;
   std::string protocolName;
 
   switch (protocolChoice)
   {
   case 1: // AODV
   {
     AodvHelper aodv;
     internet.SetRoutingHelper(aodv);
     internet.Install(nodes);
     protocolName = "AODV";
     break;
   }
   case 2: // DSDV
   {
     DsdvHelper dsdv;
     internet.SetRoutingHelper(dsdv);
     internet.Install(nodes);
     protocolName = "DSDV";
     break;
   }
   case 3: // OLSR
   {
     OlsrHelper olsr;
     internet.SetRoutingHelper(olsr);
     internet.Install(nodes);
     protocolName = "OLSR";
     break;
   }
  default:
    NS_FATAL_ERROR("Invalid routing protocol choice");
  }
  std::cout << "Using " << protocolName << " routing protocol" << std::endl;
  return protocolName;
}

// --- Global helpers for energy metrics ---
static std::vector<double> g_nodeDeathTimes;
static uint32_t g_numNodesGlobal = 0;
static bool g_allDeadRecorded = false;
static double g_timeAllDead = 0.0;
static uint32_t g_deadCount = 0;
static double g_timeNst50 = 0.0;

static void
EnergyDepletedCallback(uint32_t nodeId)
{
  double t = Simulator::Now().GetSeconds();
  if (nodeId < g_nodeDeathTimes.size() && g_nodeDeathTimes[nodeId] < 0.0)
  {
    g_nodeDeathTimes[nodeId] = t;
    g_deadCount++;
    if (g_deadCount >= (uint32_t)std::ceil(0.5 * g_numNodesGlobal) && g_timeNst50 <= 0.0)
    {
      g_timeNst50 = t;
    }
    if (g_deadCount == g_numNodesGlobal && !g_allDeadRecorded)
    {
      g_allDeadRecorded = true;
      g_timeAllDead = t;
    }
  }
}

// Define this helper above main(), outside of it:
static void
RemainingEnergyTrace(uint32_t nodeId, double oldVal, double newVal)
{
    // std::cout << Simulator::Now().GetSeconds()
    //           << "s: Node " << nodeId
    //           << " Remaining = " << newVal << " J" << std::endl;
}


static void
ConnectEnergyDepletionTraces(const energy::EnergySourceContainer &energySources)
{
  for (uint32_t i = 0; i < energySources.GetN(); ++i)
  {
    Ptr<energy::BasicEnergySource> src = DynamicCast<energy::BasicEnergySource>(energySources.Get(i));
    if (!src)
    {
      continue;
    }
    uint32_t nodeId = src->GetNode()->GetId();
    src->TraceConnectWithoutContext("EnergyDepleted", MakeBoundCallback(&EnergyDepletedCallback, nodeId));
  }
}

// Install UDP traffic with seed-based randomized senders and intervals
static ApplicationContainer
InstallRandomizedUdpTraffic(const NodeContainer &nodes,
                            const Ipv4InterfaceContainer &ifaces,
                            uint16_t port,
                            double startTime,
                            double stopTime,
                            double heavyFraction,
                            double heavyTrafficShare,
                            double meanLightIntervalSeconds,
                            uint32_t maxPacketsPerSender,
                            uint32_t packetSize,
                            uint32_t sinkNodeId)
{
  ApplicationContainer allApps;
  UdpEchoServerHelper echoServer(port);
  allApps.Add(echoServer.Install(nodes.Get(sinkNodeId)));

  Ptr<UniformRandomVariable> uniform = CreateObject<UniformRandomVariable>();
  // Build sender list excluding sink
  std::vector<uint32_t> senderIds;
  senderIds.reserve(nodes.GetN());
  for (uint32_t i = 0; i < nodes.GetN(); ++i)
  {
    if (i == sinkNodeId)
    {
      continue;
    }
    senderIds.push_back(i);
  }
  // Shuffle for random heavy selection
  // Shuffle for random heavy selection
// We seed a C++11 engine with a value from the ns-3 deterministic RNG
// to keep the shuffle reproducible with the global simulation seed.
std::mt19937 g((uint32_t)uniform->GetInteger(0, 0xFFFFFFFF));
std::shuffle(senderIds.begin(), senderIds.end(), g);

uint32_t totalSenders = senderIds.size();
  uint32_t heavyCount = std::max<uint32_t>(1, (uint32_t)std::floor(heavyFraction * totalSenders));
  uint32_t lightCount = totalSenders - heavyCount;
  // Compute rates so heavy group contributes heavyTrafficShare of total expected traffic
  // Let r_l = 1/meanLight, r_h solved by: H*r_h / (H*r_h + L*r_l) = S_h
  double r_l = 1.0 / std::max(1e-6, meanLightIntervalSeconds);
  double r_h;
  if (lightCount == 0)
  {
    r_h = r_l; // edge case: all heavy -> equal
  }
  else
  {
    double ratio = (heavyTrafficShare / std::max(1e-9, (1.0 - heavyTrafficShare))) * ((double)lightCount / (double)heavyCount);
    r_h = std::max(1e-6, ratio * r_l);
  }
  double meanHeavyIntervalSeconds = 1.0 / r_h;
  Ptr<ExponentialRandomVariable> expHeavy = CreateObject<ExponentialRandomVariable>();
  expHeavy->SetAttribute("Mean", DoubleValue(meanHeavyIntervalSeconds));
  Ptr<ExponentialRandomVariable> expLight = CreateObject<ExponentialRandomVariable>();
  expLight->SetAttribute("Mean", DoubleValue(meanLightIntervalSeconds));

  for (uint32_t idx = 0; idx < totalSenders; ++idx)
  {
    uint32_t nodeId = senderIds[idx];
    bool isHeavy = idx < heavyCount;
    double interval = isHeavy ? std::max(0.01, expHeavy->GetValue())
                              : std::max(0.01, expLight->GetValue());
    UdpEchoClientHelper client(ifaces.GetAddress(sinkNodeId), port);
    client.SetAttribute("MaxPackets", UintegerValue(maxPacketsPerSender));
    client.SetAttribute("Interval", TimeValue(Seconds(interval)));
    client.SetAttribute("PacketSize", UintegerValue(packetSize));

    ApplicationContainer app = client.Install(nodes.Get(nodeId));
    double base = isHeavy ? meanHeavyIntervalSeconds : meanLightIntervalSeconds;
    double jitter = uniform->GetValue(0.0, 0.5 * base);
    app.Start(Seconds(startTime + jitter));
    app.Stop(Seconds(stopTime));
    allApps.Add(app);
  }

  allApps.Get(0)->SetStartTime(Seconds(1.0));
  allApps.Get(0)->SetStopTime(Seconds(stopTime));
  return allApps;
}

// store initial energy per node for later comparison
static std::vector<double> g_initialEnergyJ;


 int main(int argc, char *argv[])
 {
   // Set simulation parameters
   
   uint32_t numNodes = 1;
  std::vector<uint32_t> num_of_nodes = {5, 10, 15, 20};
  
  //iterate over different node counts
  for(auto it : num_of_nodes){
    numNodes = it;

    //start simulation
    double simTime = 300.0; // Simulation time in seconds
   int protocolChoice = 1;  // Default to AODV
    uint32_t rngSeed = 12345;
    uint32_t rngRun = 1;
    // Traffic knobs (80/20 split)
    double heavyFraction = 0.2;              // ~20% nodes are heavy senders
    double heavyTrafficShare = 0.8;          // heavy group generates ~80% of traffic
    double meanLightIntervalSeconds = 1.0;   // mean interval for light senders
    uint32_t maxPacketsPerSender = 320;      // cap per-sender packets
    uint32_t packetSize = 512;               // bytes
    uint32_t sinkNodeId = 0;                 // default sink node
    // Energy model defaults (tuned for small test ad-hoc scenarios)
    double initialEnergyJ = 500.0;           // Joules per node (adjust via --initialEnergyJ)
    double supplyVoltageV = 3.7;             // Li-ion nominal voltage
    double txCurrentA = 0.800;               // 800 mA
    double rxCurrentA = 0.250;               // 250 mA
    double idleCurrentA = 0.080;             // 80 mA
    double sleepCurrentA = 0.01;           // 10 mA
    double ccaBusyCurrentA = 0.060;          // 60 mA when CCA busy
    double switchingCurrentA = 0.100;        // 100 mA during state switching
    
    CommandLine cmd(__FILE__);
    cmd.AddValue("numNodes", "Number of nodes", numNodes);
    cmd.AddValue("simTime", "Simulation time", simTime);
    cmd.AddValue("protocol", "Routing protocol (1=AODV, 2=DSDV, 3=OLSR)", protocolChoice);
    // RNG
    cmd.AddValue("rngSeed", "RNG seed (ns-3 RngSeedManager)", rngSeed);
    cmd.AddValue("rngRun", "RNG run (ns-3 RngSeedManager)", rngRun);
    // Traffic (80/20)
    cmd.AddValue("heavyFraction", "Fraction of heavy sending nodes [0-1]", heavyFraction);
    cmd.AddValue("heavyTrafficShare", "Share of total traffic by heavy group [0-1]", heavyTrafficShare);
    cmd.AddValue("meanLightIntervalSeconds", "Mean inter-packet interval for light senders (s)", meanLightIntervalSeconds);
    cmd.AddValue("maxPacketsPerSender", "Max packets per active sender", maxPacketsPerSender);
    cmd.AddValue("packetSize", "Packet size (bytes)", packetSize);
    cmd.AddValue("sinkNodeId", "Node ID to act as sink/server", sinkNodeId);
    // Energy-related CLI flags
    cmd.AddValue("initialEnergyJ", "Initial energy per node (J)", initialEnergyJ);
    cmd.AddValue("supplyVoltageV", "Supply voltage for energy source (V)", supplyVoltageV);
    cmd.AddValue("txCurrentA", "WiFi radio Tx current (A)", txCurrentA);
    cmd.AddValue("rxCurrentA", "WiFi radio Rx current (A)", rxCurrentA);
    cmd.AddValue("idleCurrentA", "WiFi radio Idle current (A)", idleCurrentA);
    cmd.AddValue("sleepCurrentA", "WiFi radio Sleep current (A)", sleepCurrentA);
    cmd.AddValue("ccaBusyCurrentA", "WiFi radio CCA Busy current (A)", ccaBusyCurrentA);
    cmd.AddValue("switchingCurrentA", "WiFi radio Switching current (A)", switchingCurrentA);
    cmd.Parse(argc, argv);

    // Apply RNG configuration for reproducible randomness
    RngSeedManager::SetSeed(rngSeed);
    RngSeedManager::SetRun(rngRun);
    
    // --- Setup Nodes and Channel ---
    NodeContainer nodes;
    nodes.Create(numNodes);
    
    // Set up WiFi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    wifiChannel.AddPropagationLoss("ns3::FriisPropagationLossModel");
    wifiPhy.SetChannel(wifiChannel.Create());
    
    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");
    wifiPhy.Set("TxPowerStart", DoubleValue(5.0));  // dBm
    wifiPhy.Set("TxPowerEnd", DoubleValue(5.0));    // dBm
    wifiPhy.Set("RxSensitivity", DoubleValue(-90.0)); // dBm
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);
    


    
    // inside your main()
    GenericBatteryModelHelper batteryHelper;
    
    // Install on your nodes (you can pass NodeContainer)
    Ptr<energy::EnergySourceContainer> sourcesPtr = batteryHelper.Install(nodes);
    energy::EnergySourceContainer energySources = *sourcesPtr;

    
    // Now override the attributes for custom capacity and voltage
    for (uint32_t i = 0; i < energySources.GetN(); ++i)
    {
        Ptr<energy::GenericBatteryModel> battery = DynamicCast<energy::GenericBatteryModel>(energySources.Get(i));
        battery->SetAttribute("NominalVoltage", DoubleValue(3.0));    // Volts
        battery->SetAttribute("FullVoltage", DoubleValue(3.0));       // Volts
        battery->SetAttribute("CutoffVoltage", DoubleValue(2.7));     // Volts
        battery->SetAttribute("NominalCapacity", DoubleValue(0.02));   // Amp-hours
        battery->SetAttribute("MaxCapacity", DoubleValue(0.02));       // Amp-hours
        battery->SetAttribute("InternalResistance", DoubleValue(0.05)); // Ohms
    }
    

    // Record initial remaining energy for every installed energy source
    g_initialEnergyJ.clear();
    g_initialEnergyJ.resize(energySources.GetN(), 0.0);

    for (uint32_t i = 0; i < energySources.GetN(); ++i)
    {
        // Try GenericBatteryModel first
        Ptr<energy::GenericBatteryModel> gb = DynamicCast<energy::GenericBatteryModel>(energySources.Get(i));
        if (gb)
        {
            g_initialEnergyJ[i] = gb->GetRemainingEnergy(); // initial remaining J
        }
        else
        {
            // Fallback to energy::BasicEnergySource
            Ptr<energy::BasicEnergySource> bs = DynamicCast<energy::BasicEnergySource>(energySources.Get(i));
            if (bs)
            {
                g_initialEnergyJ[i] = bs->GetRemainingEnergy();
            }
            else
            {
                // Unknown energy source type — set to 0 and warn
                g_initialEnergyJ[i] = 0.0;
                std::cout << "Warning: unknown energy source type at index " << i << std::endl;
            }
        }
    }

    
    // Attach Wifi radio energy model to use with the GenericBatteryModel
    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(txCurrentA));
    radioEnergyHelper.Set("RxCurrentA", DoubleValue(rxCurrentA));
    radioEnergyHelper.Set("IdleCurrentA", DoubleValue(idleCurrentA));
    radioEnergyHelper.Set("SleepCurrentA", DoubleValue(sleepCurrentA));
    energy::DeviceEnergyModelContainer deviceModels =
        radioEnergyHelper.Install(devices, energySources);
    
    // Optional: trace remaining energy for each node
    for (uint32_t i = 0; i < energySources.GetN(); ++i)
    {
        Ptr<energy::GenericBatteryModel> battery =
            DynamicCast<energy::GenericBatteryModel>(energySources.Get(i));

        battery->TraceConnectWithoutContext(
            "RemainingEnergy",
            MakeBoundCallback(&RemainingEnergyTrace, i));
    }
    

    // --- Install Routing and Internet Stack ---
    std::string protocolName = ConfigureRoutingProtocol(nodes, protocolChoice);
    
    // Assign IP addresses to devices
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    
    // --- Set up Mobility ---
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                    "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=250.0]"),
                                    "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=250.0]"));
    mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                                "Speed", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=1.0]"),
                                "Pause", StringValue("ns3::ConstantRandomVariable[Constant=1.0]"),
                                "PositionAllocator", StringValue("ns3::RandomRectanglePositionAllocator"));
    mobility.Install(nodes);
    
    // --- Install Applications (UDP Echo with 80/20 sender split) ---
    uint16_t port = 9;
    ApplicationContainer apps = InstallRandomizedUdpTraffic(
        nodes,
        interfaces,
        port,
        2.0,
        simTime,
        heavyFraction,
        heavyTrafficShare,
        meanLightIntervalSeconds,
        maxPacketsPerSender,
        packetSize,
        sinkNodeId);
    
    // --- Install Flow Monitor ---
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
    
    // --- Energy Depletion Tracing & Init ---
    g_numNodesGlobal = numNodes;
    g_nodeDeathTimes.assign(numNodes, -1.0);
    g_allDeadRecorded = false;
    g_deadCount = 0;
    g_timeAllDead = 0.0;
    g_timeNst50 = 0.0;
    ConnectEnergyDepletionTraces(energySources);

    // --- Run Simulation ---
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();

    
    // --- Performance Analysis ---
    monitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats();
    
    double totalDelay = 0;
    uint32_t totalRxPackets = 0;
    uint32_t totalTxPackets = 0;
    uint32_t totalLostPackets = 0;
    double totalRxBytes = 0;
    double firstTxTime = simTime;
    double lastRxTime = 0.0;
    
    for (auto const &[flowId, flowStats] : stats)
    {
        // Aggregate stats
        totalDelay += flowStats.delaySum.GetSeconds();
        totalRxPackets += flowStats.rxPackets;
        totalTxPackets += flowStats.txPackets;
        totalLostPackets += flowStats.lostPackets;
        totalRxBytes += flowStats.rxBytes;
    
        // Find the actual duration of data transmission
        if (flowStats.txPackets > 0)
        {
        firstTxTime = std::min(firstTxTime, flowStats.timeFirstTxPacket.GetSeconds());
        }
        if (flowStats.rxPackets > 0)
        {
        lastRxTime = std::max(lastRxTime, flowStats.timeLastRxPacket.GetSeconds());
        }
    }
    
    // Calculate metrics
    double totalDuration = lastRxTime - firstTxTime;
    if (totalDuration <= 0)
    {
        totalDuration = simTime - 2.0; // Fallback if no packets were tx/rx
    }
    
    // Throughput (Kbps)
    double throughput = (totalRxBytes * 8.0) / (totalDuration * 1000.0);
    
    // Average Delay (ms)
    double avgDelay = (totalRxPackets > 0) ? (totalDelay / totalRxPackets) * 1000.0 : 0.0;
    
    // Packet Delivery Ratio (%)
    double pdr = (totalTxPackets > 0) ? ((double)totalRxPackets / totalTxPackets) * 100.0 : 0.0;
    
    // Packet Loss Ratio (%)
    double plr = (totalTxPackets > 0) ? ((double)totalLostPackets / totalTxPackets) * 100.0 : 0.0;
    
    
    // Print traffic results
    std::cout << "\n--- Simulation Results (" << protocolName << ") ---" << std::endl;
    std::cout << "Total Throughput: " << throughput << " Kbps" << std::endl;
    std::cout << "Average Delay: " << avgDelay << " ms" << std::endl;
    std::cout << "Packet Delivery Success Rate (PDR): " << pdr << " %" << std::endl;
    std::cout << "Packet Loss Ratio (PLR): " << plr << " %" << std::endl;
    std::cout << "------------------------------------" << std::endl;
    std::cout << "Total Packets Transmitted: " << totalTxPackets << std::endl;
    std::cout << "Total Packets Received: " << totalRxPackets << std::endl;
    std::cout << "Total Packets Lost: " << totalLostPackets << std::endl;
    std::cout << "------------------------------------" << std::endl;

    // --- Accurate Energy Results (per-node + totals) ---
    double totalInitialJ = 0.0;
    double totalRemainingJ = 0.0;
    uint32_t nSources = energySources.GetN();
    for (uint32_t i = 0; i < nSources; ++i)
    {
        // Try GenericBatteryModel
        Ptr<energy::GenericBatteryModel> gb = DynamicCast<energy::GenericBatteryModel>(energySources.Get(i));
        if (gb)
        {
            double rem = gb->GetRemainingEnergy();
            double init = g_initialEnergyJ.size() > i ? g_initialEnergyJ[i] : 0.0;
            totalInitialJ += init;
            totalRemainingJ += rem;
            std::cout << "Node " << i << " Remaining Energy: " << rem << " J"
                    << " | SoC: " << gb->GetStateOfCharge() << " %"
                    << " | Initial: " << init << " J" << std::endl;
            continue;
        }

        // Fallback: energy::BasicEnergySource
        Ptr<energy::BasicEnergySource> bs = DynamicCast<energy::BasicEnergySource>(energySources.Get(i));
        if (bs)
        {
            double rem = bs->GetRemainingEnergy();
            double init = g_initialEnergyJ.size() > i ? g_initialEnergyJ[i] : 0.0;
            totalInitialJ += init;
            totalRemainingJ += rem;
            std::cout << "Node " << i << " Remaining Energy (energy::BasicEnergySource): " << rem << " J"
                    << " | Initial: " << init << " J" << std::endl;
            continue;
        }

        // Unknown type
        std::cout << "Node " << i << " has unknown energy source type; cannot report remaining energy." << std::endl;
    }

    // Totals & consumed
    double totalConsumedJ = totalInitialJ - totalRemainingJ;
    std::cout << "Total Initial Energy (sum recorded): " << totalInitialJ << " J" << std::endl;
    std::cout << "Total Remaining Energy: " << totalRemainingJ << " J" << std::endl;
    std::cout << "Total Energy Consumed: " << totalConsumedJ << " J" << std::endl;

    // Survival time metrics
    if (g_timeNst50 <= 0.0)
    {
        std::cout << "NST (50% nodes dead): not reached within simulation (>= " << simTime << " s)" << std::endl;
    }
    else
    {
        std::cout << "NST (50% nodes dead): " << g_timeNst50 << " s" << std::endl;
    }
    if (!g_allDeadRecorded)
    {
        std::cout << "NST (all nodes dead): not reached within simulation (>= " << simTime << " s)" << std::endl;
    }
    else
    {
        std::cout << "NST (all nodes dead): " << g_timeAllDead << " s" << std::endl;
    }
    
    Simulator::Destroy();
    }
   
   return 0;
 }