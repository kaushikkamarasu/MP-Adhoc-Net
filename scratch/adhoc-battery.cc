/*
 * Modified Ad-Hoc Network Example
 *
 * This script sets up a mobile ad-hoc network to compare the performance
 * of AODV, DSDV, and OLSR using the FlowMonitor module.
 */

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
 
 int main(int argc, char *argv[])
 {
   // Set simulation parameters
   uint32_t numNodes = 10;
   double simTime = 20.0; // Simulation time in seconds
   int protocolChoice = 1;  // Default to AODV
  // Energy model defaults (tuned for small test ad-hoc scenarios)
  double initialEnergyJ = 500.0;           // Joules per node (adjust via --initialEnergyJ)
  double supplyVoltageV = 3.7;             // Li-ion nominal voltage
  double txCurrentA = 0.200;               // 200 mA
  double rxCurrentA = 0.180;               // 180 mA
  double idleCurrentA = 0.050;             // 50 mA
  double sleepCurrentA = 0.0001;           // 0.1 mA
  double ccaBusyCurrentA = 0.060;          // 60 mA when CCA busy
  double switchingCurrentA = 0.100;        // 100 mA during state switching
 
   CommandLine cmd(__FILE__);
   cmd.AddValue("numNodes", "Number of nodes", numNodes);
   cmd.AddValue("simTime", "Simulation time", simTime);
   cmd.AddValue("protocol", "Routing protocol (1=AODV, 2=DSDV, 3=OLSR)", protocolChoice);
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
   NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, nodes);
 
  // --- Energy Model (BasicEnergySource + WifiRadioEnergyModel) ---
BasicEnergySourceHelper basicSourceHelper;
  basicSourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(initialEnergyJ));
  basicSourceHelper.Set("BasicEnergySupplyVoltageV", DoubleValue(supplyVoltageV));
  energy::EnergySourceContainer energySources = basicSourceHelper.Install(nodes);

  WifiRadioEnergyModelHelper radioEnergyHelper;
  // Tune current draws via CLI
  radioEnergyHelper.Set("TxCurrentA", DoubleValue(txCurrentA));
  radioEnergyHelper.Set("RxCurrentA", DoubleValue(rxCurrentA));
  radioEnergyHelper.Set("IdleCurrentA", DoubleValue(idleCurrentA));
  radioEnergyHelper.Set("SleepCurrentA", DoubleValue(sleepCurrentA));
  radioEnergyHelper.Set("CcaBusyCurrentA", DoubleValue(ccaBusyCurrentA));
  radioEnergyHelper.Set("SwitchingCurrentA", DoubleValue(switchingCurrentA));
  // Optional: make Tx current scale with Tx power (TypeId may vary by version)
  // radioEnergyHelper.Set("TxCurrentModel", StringValue("ns3::LinearWifiTxCurrentModel"));
  energy::DeviceEnergyModelContainer deviceModels = radioEnergyHelper.Install(devices, energySources);

   // --- Install Routing and Internet Stack ---
   std::string protocolName = ConfigureRoutingProtocol(nodes, protocolChoice);
 
   // Assign IP addresses to devices
   Ipv4AddressHelper ipv4;
   ipv4.SetBase("10.1.1.0", "255.255.255.0");
   Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
 
   // --- Set up Mobility ---
   MobilityHelper mobility;
   mobility.SetPositionAllocator("ns3::RandomRectanglePositionAllocator",
                                 "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"),
                                 "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"));
   mobility.SetMobilityModel("ns3::RandomWaypointMobilityModel",
                             "Speed", StringValue("ns3::UniformRandomVariable[Min=1.0|Max=10.0]"),
                             "Pause", StringValue("ns3::ConstantRandomVariable[Constant=2.0]"),
                             "PositionAllocator", StringValue("ns3::RandomRectanglePositionAllocator"));
   mobility.Install(nodes);
 
   // --- Install Applications (UDP Echo) ---
   uint16_t port = 9;
   UdpEchoServerHelper echoServer(port);
   ApplicationContainer serverApps = echoServer.Install(nodes.Get(0));
   serverApps.Start(Seconds(1.0));
   serverApps.Stop(Seconds(simTime));
 
   UdpEchoClientHelper echoClient(interfaces.GetAddress(0), port);
   echoClient.SetAttribute("MaxPackets", UintegerValue(320));
   echoClient.SetAttribute("Interval", TimeValue(Seconds(1.0)));
   echoClient.SetAttribute("PacketSize", UintegerValue(1024));
 
   ApplicationContainer clientApps;
   for (uint32_t i = 1; i < numNodes; ++i)
   {
     clientApps.Add(echoClient.Install(nodes.Get(i)));
   }
   clientApps.Start(Seconds(2.0));
   clientApps.Stop(Seconds(simTime));
 
   // --- Install Flow Monitor ---
   FlowMonitorHelper flowHelper;
   Ptr<FlowMonitor> monitor = flowHelper.InstallAll();
 
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
 
 
   // Print results
   std::cout << "\n--- Simulation Results (" << protocolName << ") ---" << std::endl;
   std::cout << "Total Throughput: " << throughput << " Kbps" << std::endl;
   std::cout << "Average Delay: " << avgDelay << " ms" << std::endl;
   std::cout << "Packet Delivery Ratio (PDR): " << pdr << " %" << std::endl;
   std::cout << "Packet Loss Ratio (PLR): " << plr << " %" << std::endl;
   std::cout << "------------------------------------" << std::endl;
   std::cout << "Total Packets Transmitted: " << totalTxPackets << std::endl;
   std::cout << "Total Packets Received: " << totalRxPackets << std::endl;
   std::cout << "Total Packets Lost: " << totalLostPackets << std::endl;
   std::cout << "------------------------------------" << std::endl;

  // --- Energy Results ---
  double totalRemainingJ = 0.0;
  for (uint32_t i = 0; i < energySources.GetN(); ++i)
  {
    Ptr<energy::BasicEnergySource> src = DynamicCast<energy::BasicEnergySource>(energySources.Get(i));
    if (src)
    {
      double remaining = src->GetRemainingEnergy();
      totalRemainingJ += remaining;
      std::cout << "Node " << i << " Remaining Energy: " << remaining << " J" << std::endl;
    }
  }
  double totalInitialJ = initialEnergyJ * numNodes;
  std::cout << "Total Remaining Energy: " << totalRemainingJ << " J" << std::endl;
  std::cout << "Total Energy Consumed: " << (totalInitialJ - totalRemainingJ) << " J" << std::endl;
 
   Simulator::Destroy();
   return 0;
 }