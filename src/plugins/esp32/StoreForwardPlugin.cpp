#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include <Arduino.h>
#include <map>

#define STOREFORWARD_MAX_PACKETS 0
#define STOREFORWARD_SEND_HISTORY_SHORT 600

StoreForwardPlugin *storeForwardPlugin;

int32_t StoreForwardPlugin::runOnce()
{

#ifndef NO_ESP32

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (radioConfig.preferences.is_router) {
            // Maybe some cleanup functions?
            this->sawNodeReport();
            this->historyReport();
            return (10 * 1000);
        } else {
            /*
             * If the plugin is turned on and is_router is not enabled, then we'll send a heartbeat every
             * few minutes.
             *
             * This behavior is expected to change. It's only here until we come up with something better.
             */

            DEBUG_MSG("Store & Forward Plugin - Sending heartbeat\n");

            storeForwardPlugin->sendPayload();

            return (4 * 60 * 1000);
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");

        return (INT32_MAX);
    }

#endif
    return (INT32_MAX);
}

void StoreForwardPlugin::populatePSRAM()
{
    /*
    For PSRAM usage, see:
        https://learn.upesy.com/en/programmation/psram.html#psram-tab
    */

    DEBUG_MSG("Before PSRAM initilization:\n");

    DEBUG_MSG("  Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("  Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("  Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("  Free PSRAM: %d\n", ESP.getFreePsram());

    // PacketHistoryStruct *packetHistory = (PacketHistoryStruct *)ps_calloc(STOREFORWARD_MAX_PACKETS,
    // sizeof(PacketHistoryStruct));

    // Use a maximum of half the available PSRAM unless otherwise specified.
    uint32_t numberOfPackets =
        STOREFORWARD_MAX_PACKETS ? STOREFORWARD_MAX_PACKETS : ((ESP.getPsramSize() / 2) / sizeof(PacketHistoryStruct));

    this->packetHistory = (PacketHistoryStruct *)ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct));
    DEBUG_MSG("After PSRAM initilization:\n");

    DEBUG_MSG("  Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("  Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("  Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("  Free PSRAM: %d\n", ESP.getFreePsram());
    DEBUG_MSG("Store and Forward Stats:\n");
    DEBUG_MSG("  numberOfPackets - %u\n", numberOfPackets);
}

// We saw a node.
uint32_t StoreForwardPlugin::sawNode(uint32_t node)
{

    /*
    TODO: Move receivedRecord into the PSRAM

    TODO: Gracefully handle the case where we run out of records.
            Maybe replace the oldest record that hasn't been seen in a while and assume they won't be back.

    TODO: Implment this as a std::map for quicker lookups (maybe it doesn't matter?).
    */
    // DEBUG_MSG("%s (id=0x%08x Fr0x%02x To0x%02x, WantAck%d, HopLim%d", prefix, p->id, p->from & 0xff, p->to & 0xff,
    // p->want_ack, p->hop_limit);
    DEBUG_MSG("looking for node - from-0x%08x\n", node);
    for (int i = 0; i < 50; i++) {
        // DEBUG_MSG("Iterating through the seen nodes - %u %u %u\n", i, receivedRecord[i][0], receivedRecord[i][1]);
        // First time seeing that node.
        if (receivedRecord[i][0] == 0) {
            // DEBUG_MSG("New node! Woohoo! Win!\n");
            receivedRecord[i][0] = node;
            receivedRecord[i][1] = millis();

            return receivedRecord[i][1];
        }

        // We've seen this node before.
        if (receivedRecord[i][0] == node) {
            // DEBUG_MSG("We've seen this node before\n");
            uint32_t lastSaw = receivedRecord[i][1];
            receivedRecord[i][1] = millis();
            return lastSaw;
        }
    }

    return 0;
}

void StoreForwardPlugin::historyReport()
{
    DEBUG_MSG("Iterating through the message history...\n");
    DEBUG_MSG("Message history contains %u records\n", this->packetHistoryCurrent);
    uint32_t startTimer = millis();
    for (int i = 0; i < this->packetHistoryCurrent; i++) {
        if (this->packetHistory[i].time) {
            // DEBUG_MSG("... time-%u to-0x%08x\n", this->packetHistory[i].time, this->packetHistory[i].to & 0xffffffff);
        }
    }
    DEBUG_MSG("StoreForwardPlugin::historyReport runtime - %u ms\n", millis() - startTimer);
}
void StoreForwardPlugin::historySend(uint32_t msAgo, uint32_t to)
{
    for (int i = 0; i < this->packetHistoryCurrent; i++) {
        if (this->packetHistory[i].time) {
            // DEBUG_MSG("... time-%u to-0x%08x\n", this->packetHistory[i].time, this->packetHistory[i].to & 0xffffffff);
        }
    }
}

void StoreForwardPlugin::historyAdd(const MeshPacket *mp)
{
    auto &p = mp;

    static uint8_t bytes[MAX_RHPACKETLEN];
    size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), Data_fields, &p->decoded);
    assert(numbytes <= MAX_RHPACKETLEN);

    DEBUG_MSG("MP numbytes %u\n", numbytes);

    // destination, source, bytes
    // memcpy(p->encrypted.bytes, bytes, numbytes);
    memcpy(this->packetHistory[this->packetHistoryCurrent].bytes, bytes, MAX_RHPACKETLEN);
    this->packetHistory[this->packetHistoryCurrent].time = millis();
    this->packetHistory[this->packetHistoryCurrent].to = mp->to;
    this->packetHistoryCurrent++;
}

// We saw a node.
void StoreForwardPlugin::sawNodeReport()
{

    /*
    TODO: Move receivedRecord into the PSRAM

    TODO: Gracefully handle the case where we run out of records.
            Maybe replace the oldest record that hasn't been seen in a while and assume they won't be back.

    TODO: Implment this as a std::map for quicker lookups (maybe it doesn't matter?).
    */

    DEBUG_MSG("Iterating through the seen nodes in receivedRecord...\n");
    for (int i = 0; i < 50; i++) {
        if (receivedRecord[i][1]) {
            DEBUG_MSG("... record-%u from-0x%08x secAgo-%u\n", i, receivedRecord[i][0], (millis() - receivedRecord[i][1]) / 1000);
        }
    }
}

MeshPacket *StoreForwardPlugin::allocReply()
{
    auto reply = allocDataPacket(); // Allocate a packet for sending
    return reply;
}

void StoreForwardPlugin::sendPayload(NodeNum dest, bool wantReplies)
{
    DEBUG_MSG("Sending S&F Payload\n");
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = true;
    /*
     */
    static char heartbeatString[20];
    snprintf(heartbeatString, sizeof(heartbeatString), "1");

    p->decoded.payload.size = strlen(heartbeatString); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, "1", 1);

    service.sendToMesh(p);
}

bool StoreForwardPlugin::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32
    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (getFrom(&mp) != nodeDB.getNodeNum()) {
            // DEBUG_MSG("Store & Forward Plugin -- Print Start ---------- ---------- ---------- ---------- ----------\n\n\n");
            // DEBUG_MSG("%s (id=0x%08x Fr0x%02x To0x%02x, WantAck%d, HopLim%d", prefix, p->id, p->from & 0xff, p->to & 0xff,
            // p->want_ack, p->hop_limit);
            printPacket("----- PACKET FROM RADIO -----", &mp);
            uint32_t sawTime = storeForwardPlugin->sawNode(getFrom(&mp) & 0xffffffff);
            DEBUG_MSG("We last saw this node (%u), %u sec ago\n", mp.from & 0xffffffff, (millis() - sawTime) / 1000);
            DEBUG_MSG("    --------------   ");
            if (mp.decoded.portnum == PortNum_UNKNOWN_APP) {
                DEBUG_MSG("Packet came from - PortNum_UNKNOWN_APP\n");
            } else if (mp.decoded.portnum == PortNum_TEXT_MESSAGE_APP) {
                DEBUG_MSG("Packet came from - PortNum_TEXT_MESSAGE_APP\n");

                storeForwardPlugin->historyAdd(&mp);

            } else if (mp.decoded.portnum == PortNum_REMOTE_HARDWARE_APP) {
                DEBUG_MSG("Packet came from - PortNum_REMOTE_HARDWARE_APP\n");
            } else if (mp.decoded.portnum == PortNum_POSITION_APP) {
                DEBUG_MSG("Packet came from - PortNum_POSITION_APP\n");
            } else if (mp.decoded.portnum == PortNum_NODEINFO_APP) {
                DEBUG_MSG("Packet came from - PortNum_NODEINFO_APP\n");
            } else if (mp.decoded.portnum == PortNum_ROUTING_APP) {
                DEBUG_MSG("Packet came from - PortNum_ROUTING_APP\n");
            } else if (mp.decoded.portnum == PortNum_ADMIN_APP) {
                DEBUG_MSG("Packet came from - PortNum_ADMIN_APP\n");
            } else if (mp.decoded.portnum == PortNum_REPLY_APP) {
                DEBUG_MSG("Packet came from - PortNum_REPLY_APP\n");
            } else if (mp.decoded.portnum == PortNum_IP_TUNNEL_APP) {
                DEBUG_MSG("Packet came from - PortNum_IP_TUNNEL_APP\n");
            } else if (mp.decoded.portnum == PortNum_SERIAL_APP) {
                DEBUG_MSG("Packet came from - PortNum_SERIAL_APP\n");
            } else if (mp.decoded.portnum == PortNum_STORE_FORWARD_APP) {
                DEBUG_MSG("Packet came from - PortNum_STORE_FORWARD_APP\n");
            } else if (mp.decoded.portnum == PortNum_RANGE_TEST_APP) {
                DEBUG_MSG("Packet came from - PortNum_RANGE_TEST_APP\n");
            } else if (mp.decoded.portnum == PortNum_PRIVATE_APP) {
                DEBUG_MSG("Packet came from - PortNum_PRIVATE_APP\n");
            } else if (mp.decoded.portnum == PortNum_RANGE_TEST_APP) {
                DEBUG_MSG("Packet came from - PortNum_RANGE_TEST_APP\n");
            } else if (mp.decoded.portnum == PortNum_ATAK_FORWARDER) {
                DEBUG_MSG("Packet came from - PortNum_ATAK_FORWARDER\n");
            } else {
                DEBUG_MSG("Packet came from an unknown port %u\n", mp.decoded.portnum);
            }

            if ((millis() - sawTime) > STOREFORWARD_SEND_HISTORY_SHORT) {
                // Node has been away for a while.
                storeForwardPlugin->historySend(sawTime, mp.from);
            }
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}

StoreForwardPlugin::StoreForwardPlugin()
    : SinglePortPlugin("StoreForwardPlugin", PortNum_STORE_FORWARD_APP), concurrency::OSThread("StoreForwardPlugin")
{

#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.

    */
    radioConfig.preferences.store_forward_plugin_enabled = 1;
    radioConfig.preferences.is_router = 1;

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        // Router
        if (radioConfig.preferences.is_router) {
            DEBUG_MSG("Initializing Store & Forward Plugin - Enabled as Router\n");
            if (ESP.getPsramSize()) {
                if (ESP.getFreePsram() >= 1024 * 1024) {

                    // Do the startup here

                    this->populatePSRAM();
                } else {
                    DEBUG_MSG("Device has less than 1M of PSRAM free. Aborting startup.\n");
                    DEBUG_MSG("Store & Forward Plugin - Aborting Startup.\n");
                }

            } else {
                DEBUG_MSG("Device doesn't have PSRAM.\n");
                DEBUG_MSG("Store & Forward Plugin - Aborting Startup.\n");
            }

        // Client
        } else {
            DEBUG_MSG("Initializing Store & Forward Plugin - Enabled as Client\n");
        }
    }
#endif
}