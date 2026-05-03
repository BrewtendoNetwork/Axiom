#include <3ds.h>
#include <CTRPluginFramework.hpp>
#include "common.hpp"
#include "pia.hpp"
#include "PIALogger.hpp"
#include "teeny-sha1.h"

namespace CTRPluginFramework
{
    u32 piaConnectionInfoTableCreateInstanceAddr = 0;
    nn::pia::transport::StationConnectionInfoTable** piaConnectionInfoTableAddr = 0;
    Mutex piaConnectionInfoTableCreateInstanceMutex;

    u32 piaIdentificationInfoTableCreateInstanceAddr = 0;
    nn::pia::transport::IdentificationInfoTable** piaIdentificationInfoTableAddr = 0;
    Mutex piaIdentificationInfoTableCreateInstanceMutex;

    // The pattern is located inside the nn::pia::transport::StationConnectionInfoTable::CreateInstance function,
    // from there we can extract the pointer to the actual instance being used
    bool extractPiaConnectionInfoTable(u32 addr) {
        Lock lock(piaConnectionInfoTableCreateInstanceMutex);
        if (piaConnectionInfoTableCreateInstanceAddr) return true;
        if (!addr) return false;
        piaConnectionInfoTableCreateInstanceAddr = addr;
        piaConnectionInfoTableAddr = *(nn::pia::transport::StationConnectionInfoTable***)(addr + 0x18);
        return true;
    }

    // The pattern is located inside the nn::pia::transport::IdentificationInfoTable::CreateInstance function,
    // from there we can extract the pointer to the actual instance being used
    bool extractPiaIdentificationInfoTable(u32 addr) {
        Lock lock(piaIdentificationInfoTableCreateInstanceMutex);
        if (piaIdentificationInfoTableCreateInstanceAddr) return true;
        if (!addr) return false;
        piaIdentificationInfoTableCreateInstanceAddr = addr;
        piaIdentificationInfoTableAddr = *(nn::pia::transport::IdentificationInfoTable***)(addr + 0x14);
        return true;
    }

    bool onPiaPlayersFolderAction(MenuFolder& folder, ActionType type) {
        if (type == ActionType::Opening) {
            if (*piaConnectionInfoTableAddr == nullptr) {
                MessageBox("PIA Connection Info Table not initialized yet")();
                return false;
            }

            if (*piaIdentificationInfoTableAddr == nullptr) {
                MessageBox("PIA Identification Info Table not initialized yet")();
                return false;
            }

            auto* connectionInfoTable = *piaConnectionInfoTableAddr;
            auto* identificationInfoTable = *piaIdentificationInfoTableAddr;
            Lock connLock(connectionInfoTable->m_cs);
            Lock identLock(identificationInfoTable->m_cs);
            u32 playerCount = 0;
            for (u32 i = 0; i < connectionInfoTable->m_participantSlotCount; i++) {
                ++playerCount;

                auto station = connectionInfoTable->m_participantStations[i];
                if (station == 0) continue; // This would technically be: if (station == nullptr) continue;

                nn::pia::transport::Station::IdentificationInfo* identificationInfo;
                for (u32 j = 0; j < identificationInfoTable->m_participantSlotCount; j++) {
                    if (station == identificationInfoTable->m_participantStations[j]) {
                        identificationInfo = &identificationInfoTable->m_participantIdentificationInfos[j];
                        break;
                    }
                }

                const u32 constantID = connectionInfoTable->m_participantConnectionInfos[i].m_publicLocation.m_constantID;

                std::string playerName;
                if (identificationInfo != nullptr && identificationInfo->m_playerName.m_length > 0) {
                    // Simple UTF16 to UTF8 conversion for player names
                    const char16_t* utf16 = identificationInfo->m_playerName.m_name;
                    for (size_t j = 0; j < identificationInfo->m_playerName.m_length && utf16[j] != 0; ++j) {
                        char16_t ch = utf16[j];
                        if (ch < 0x80) {
                            playerName += static_cast<char>(ch);
                        } else if (ch < 0x800) {
                            playerName += static_cast<char>(0xC0 | (ch >> 6));
                            playerName += static_cast<char>(0x80 | (ch & 0x3F));
                        } else {
                            playerName += static_cast<char>(0xE0 | (ch >> 12));
                            playerName += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
                            playerName += static_cast<char>(0x80 | (ch & 0x3F));
                        }
                    }
                    playerName = Utils::Format("\"%s\"", playerName.c_str());
                }

                if (playerName.length() == 0) {
                    playerName = Utils::Format("Player %u", playerCount);
                }

                std::string title = Utils::Format("%u. %s", playerCount, playerName.c_str());

                if (constantID == connectionInfoTable->m_localConnectionInfo.m_publicLocation.m_constantID) {
                    title += " [You]";

                    // If there is no remote connection, then we are the host
                    if (connectionInfoTable->m_remoteConnectionInfo.m_publicLocation.m_constantID == 0) {
                        title += " [Host]";
                    }
                }

                if (constantID == connectionInfoTable->m_remoteConnectionInfo.m_publicLocation.m_constantID) {
                    title += " [Host]";
                }

                u8 pidHash[20] = {};
                sha1digest(pidHash, nullptr, reinterpret_cast<const u8*>(&constantID), sizeof(u32));

                const u32 variableID = connectionInfoTable->m_participantConnectionInfos[i].m_publicLocation.m_variableID;
                const u32 serviceVariableID = connectionInfoTable->m_participantConnectionInfos[i].m_publicLocation.m_serviceVariableID;

                std::string friendCode = Utils::Format("%012llu", ((static_cast<u64>(pidHash[0]) >> 1) << 32) | constantID);
                const char* rawFriendCode = friendCode.c_str();
                std::string formattedFriendCode = Utils::Format("%.4s-%.4s-%.4s", rawFriendCode, rawFriendCode + 4, rawFriendCode + 8);

                const u8 rawNatMapping = connectionInfoTable->m_participantConnectionInfos[i].m_publicLocation.m_natMapping;
                std::string natMapping = rawNatMapping <= 2 ? PiaNatMapping[rawNatMapping] : Utils::Format("%d", rawNatMapping);

                const u8 rawNatFiltering = connectionInfoTable->m_participantConnectionInfos[i].m_publicLocation.m_natFiltering;
                std::string natFiltering = rawNatFiltering <= 2 ? PiaNatFiltering[rawNatFiltering] : Utils::Format("%d", rawNatFiltering);

                std::string note = Utils::Format(
                    "PID: %u\n"
                    "CID: %u\n"
                    "RVCID: %u\n"
                    "Friend code: %s\n"
                    "NAT mapping: %s\n"
                    "NAT filtering: %s\n",
                    constantID,
                    variableID,
                    serviceVariableID,
                    formattedFriendCode.c_str(),
                    natMapping.c_str(),
                    natFiltering.c_str()
                );

                folder.Append(new MenuEntry(title, note));
            }
        } else if (type == ActionType::Closing) {
            folder.Clear();
        }

        return true;
    }

    void initPiaLogger(PatternManager& pm) {
        const u8 piaConnectionTableCreateInstancePat[] = { 0xF4, 0x00, 0x40, 0xE2, 0x00, 0x00, 0x84, 0xE5, 0x00, 0x00, 0xA0, 0xE3 };
        pm.Add(piaConnectionTableCreateInstancePat, sizeof(piaConnectionTableCreateInstancePat), extractPiaConnectionInfoTable);

        const u8 piaIdentificationTableCreateInstancePat[] = { 0x00, 0x00, 0xA0, 0xE3, 0x00, 0x40, 0x85, 0xE5, 0x70, 0x80, 0xBD, 0xE8 };
        pm.Add(piaIdentificationTableCreateInstancePat, sizeof(piaIdentificationTableCreateInstancePat), extractPiaIdentificationInfoTable);
    }

    PatternStatus getPiaLoggerStatus() {
        if (!piaConnectionInfoTableCreateInstanceAddr || !piaConnectionInfoTableCreateInstanceAddr) {
            return PatternStatus::NotFound;
        }

        return PatternStatus::Active;
    }
}
