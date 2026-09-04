/*
 * Copyright (c) 2026 MTC-AODV
 *
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Sonde de portée radio.
 *
 * Le dimensionnement du scénario MANET dépend d'une grandeur qui n'apparaît nulle part
 * dans la configuration : la distance au-delà de laquelle un lien cesse d'être
 * utilisable. Elle dépend conjointement de la puissance d'émission, du modèle de
 * propagation et — c'est le point souvent négligé — du débit de modulation, une
 * modulation rapide exigeant un rapport signal sur bruit bien supérieur.
 *
 * Ce programme mesure, pour chaque débit 802.11b, le taux de livraison à distance
 * croissante entre deux nœuds fixes. Il ne produit aucune métrique du framework : c'est
 * un instrument de calibration, dont les résultats sont consignés dans docs/PARAMETERS.md.
 */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"

#include <iomanip>
#include <iostream>
#include <vector>

using namespace ns3;

namespace
{

/// Nombre de paquets sondes par point de mesure.
constexpr uint32_t PROBE_PACKET_COUNT = 100;
constexpr uint32_t PROBE_PACKET_BYTES = 512;
constexpr uint16_t PROBE_PORT = 9500;

uint32_t g_receivedPackets = 0;

void
CountReception(Ptr<Socket> socket)
{
    Ptr<Packet> packet;
    Address from;
    while ((packet = socket->RecvFrom(from)))
    {
        if (packet->GetSize() > 0)
        {
            ++g_receivedPackets;
        }
    }
}

/**
 * \brief Mesure le taux de livraison à une distance donnée.
 * \return la fraction de paquets sondes reçus, dans [0,1]
 *
 * Le routage est statique : la sonde mesure le lien radio, pas le protocole.
 */
double
MeasureDeliveryRatio(double distanceMeters,
                     const std::string& dataRate,
                     double txPowerDbm,
                     double pathLossExponent)
{
    g_receivedPackets = 0;

    NodeContainer nodes;
    nodes.Create(2);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                               "Exponent", DoubleValue(pathLossExponent));

    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(txPowerDbm));
    phy.Set("TxPowerEnd", DoubleValue(txPowerDbm));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue(dataRate),
                                 "ControlMode", StringValue(dataRate));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    MobilityHelper mobility;
    Ptr<ListPositionAllocator> positions = CreateObject<ListPositionAllocator>();
    positions->Add(Vector(0.0, 0.0, 0.0));
    positions->Add(Vector(distanceMeters, 0.0, 0.0));
    mobility.SetPositionAllocator(positions);
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(nodes);

    InternetStackHelper internet;
    internet.Install(nodes);

    Ipv4AddressHelper addresses;
    addresses.SetBase("10.9.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = addresses.Assign(devices);

    Ptr<Socket> receiver = Socket::CreateSocket(nodes.Get(1), UdpSocketFactory::GetTypeId());
    receiver->Bind(InetSocketAddress(Ipv4Address::GetAny(), PROBE_PORT));
    receiver->SetRecvCallback(MakeCallback(&CountReception));

    Ptr<Socket> sender = Socket::CreateSocket(nodes.Get(0), UdpSocketFactory::GetTypeId());
    sender->Connect(InetSocketAddress(interfaces.GetAddress(1), PROBE_PORT));

    // Envois espacés de 20 ms : assez pour éviter toute file d'attente, donc toute perte
    // qui ne serait pas due au canal.
    for (uint32_t i = 0; i < PROBE_PACKET_COUNT; ++i)
    {
        Simulator::Schedule(Seconds(1.0 + 0.02 * i), [sender]() {
            sender->Send(Create<Packet>(PROBE_PACKET_BYTES));
        });
    }

    Simulator::Stop(Seconds(1.0 + 0.02 * PROBE_PACKET_COUNT + 1.0));
    Simulator::Run();
    Simulator::Destroy();

    return static_cast<double>(g_receivedPackets) / PROBE_PACKET_COUNT;
}

} // namespace

int
main(int argc, char* argv[])
{
    double txPowerDbm = 16.0;
    double pathLossExponent = 3.0;
    double maximumDistance = 400.0;
    double distanceStep = 20.0;

    CommandLine commandLine(__FILE__);
    commandLine.AddValue("txPowerDbm", "Puissance d'émission (dBm)", txPowerDbm);
    commandLine.AddValue("pathLossExponent", "Exposant de perte de parcours", pathLossExponent);
    commandLine.AddValue("maximumDistance", "Distance maximale sondée (m)", maximumDistance);
    commandLine.AddValue("distanceStep", "Pas de distance (m)", distanceStep);
    commandLine.Parse(argc, argv);

    const std::vector<std::string> rates = {"DsssRate1Mbps",
                                            "DsssRate2Mbps",
                                            "DsssRate5_5Mbps",
                                            "DsssRate11Mbps"};

    std::cout << "# sonde de portée : txPower=" << txPowerDbm << " dBm, exposant="
              << pathLossExponent << '\n';
    std::cout << std::left << std::setw(10) << "dist(m)";
    for (const std::string& rate : rates)
    {
        std::cout << std::setw(16) << rate;
    }
    std::cout << '\n';

    for (double distance = distanceStep; distance <= maximumDistance; distance += distanceStep)
    {
        std::cout << std::left << std::setw(10) << distance;
        for (const std::string& rate : rates)
        {
            const double ratio = MeasureDeliveryRatio(distance, rate, txPowerDbm, pathLossExponent);
            std::cout << std::setw(16) << std::fixed << std::setprecision(2) << ratio;
        }
        std::cout << '\n';
    }

    return 0;
}
